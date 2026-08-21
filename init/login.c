/* login.c — KratosOS User Login Manager (/bin/login)
 *
 * Responsabilità:
 *   1. Chiede l'username e la password (disabilitando l'echo del terminale)
 *   2. Verifica l'utente su /etc/passwd e /etc/shadow usando kratos_crypt() (SHA-512)
 *   3. Imposta i gruppi supplementari, GID e UID dell'utente (drop dei privilegi root)
 *   4. Prepara l'ambiente (HOME, USER, LOGNAME, SHELL, PATH) e lancia la login shell.
 *
 * Retry behaviour:
 *   Up to 3 attempts are allowed. After each failure a 1-second delay is
 *   inserted to slow brute-force attacks on the physical console.
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /bin/login login.c kratos-crypt.c
 */

#define _GNU_SOURCE

#include "kratos-crypt.h"
#include <errno.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>

#define MAX_RETRIES 3

/* Constant-time string comparison: always walks the full length of the
 * longer string, so the time taken does not depend on where the first
 * differing byte is. Prevents a timing side-channel on password hash
 * verification (a plain strcmp()/memcmp() can leak how many leading
 * characters matched via response-time differences). */
static int constant_time_streq(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t max = la > lb ? la : lb;

    unsigned char diff = (unsigned char)(la != lb);
    for (size_t i = 0; i < max; i++) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ca ^ cb);
    }
    return diff == 0;
}

static void disable_echo(struct termios *old_t)
{
    struct termios new_t;
    tcgetattr(STDIN_FILENO, old_t);
    new_t = *old_t;
    new_t.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
}

static void restore_echo(const struct termios *old_t)
{
    tcsetattr(STDIN_FILENO, TCSANOW, old_t);
}

static void restore_sane_termios(void)
{
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_iflag |= (ICRNL | IXON);
        t.c_oflag |= (OPOST | ONLCR);
        t.c_lflag |= (ECHO | ECHOE | ECHOK | ICANON | ISIG);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }

    /* Stessa fix di tty.c: senza questa la shell lanciata dopo il
     * login eredita una winsize 0x0 su seriale e mostra gli stessi
     * artefatti (cursore fuori posto, righe che si "cancellano"). */
    struct winsize ws = { .ws_row = 24, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 };
    ioctl(STDIN_FILENO, TIOCSWINSZ, &ws);
}

#include <sys/utsname.h>

static void print_issue(void)
{
    FILE *f = fopen("/etc/issue", "r");
    if (!f) return;

    struct utsname uts;
    uname(&uts);

    char host[256];
    if (gethostname(host, sizeof(host)) != 0) strcpy(host, "kratos");

    const char *tty = ttyname(STDIN_FILENO);
    if (!tty) tty = "tty";
    if (strncmp(tty, "/dev/", 5) == 0) tty += 5;

    /* Clear screen and home cursor for a clean login experience */
    printf("\033[H\033[J");

    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\\') {
            c = fgetc(f);
            switch (c) {
                case 's': fputs(uts.sysname, stdout); break;
                case 'n': fputs(host, stdout); break;
                case 'r': fputs(uts.release, stdout); break;
                case 'v': fputs(uts.version, stdout); break;
                case 'm': fputs(uts.machine, stdout); break;
                case 'l': fputs(tty, stdout); break;
                case '\\': putchar('\\'); break;
                default:
                    if (c != EOF) {
                        putchar('\\');
                        putchar(c);
                    }
                    break;
            }
        } else {
            putchar(c);
        }
    }
    fclose(f);
    fflush(stdout);
}

/* Build the "<hostname> login: " prompt from the real system hostname
 * (set via sethostname() in services.c) instead of a hardcoded string, so
 * the prompt can never drift out of sync with the actual machine name. */
static void print_login_prompt(void)
{
    char host[256];
    if (gethostname(host, sizeof(host)) != 0 || host[0] == '\0') {
        strncpy(host, "kratos", sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }
    printf("%s login: ", host);
}

int main(int argc, char *argv[])
{
    const char *forced_user = NULL;
    int force_login = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            force_login = 1;
            forced_user = argv[++i];
        } else if (argv[i][0] != '-') {
            forced_user = argv[i];
        }
    }

    /* Check if Live environment is active from /proc/cmdline */
    if (!forced_user) {
        FILE *cmd = fopen("/proc/cmdline", "r");
        if (cmd) {
            char line[1024];
            if (fgets(line, sizeof(line), cmd)) {
                if (strstr(line, "kratos.live=1") || strstr(line, "kratos.live")) {
                    const char *tty = ttyname(STDIN_FILENO);
                    if (tty && (strcmp(tty, "/dev/tty1") == 0 || strcmp(tty, "/dev/tty0") == 0)) {
                        forced_user = "kratos-live";
                        force_login = 1;
                    }
                }
            }
            fclose(cmd);
        }
    }

    if (!force_login) {
        print_issue();
        fflush(stdout);
    }

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {

    char username[128] = {0};
    char password[128] = {0};

    if (forced_user) {
        strncpy(username, forced_user, sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
    }

    /* Prompt username */
    while (username[0] == '\0') {
        print_login_prompt();
        fflush(stdout);

        if (!fgets(username, sizeof(username), stdin)) {
            return 1;
        }

        size_t len = strlen(username);
        while (len > 0 && (username[len - 1] == '\n' || username[len - 1] == '\r' || username[len - 1] == ' ')) {
            username[--len] = '\0';
        }
    }

    /* Read user info from /etc/passwd */
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        /* Disabilita echo per la password anche se l'utente non esiste (anti-timing) */
        struct termios old_t;
        disable_echo(&old_t);
        printf("Password: ");
        fflush(stdout);
        if (fgets(password, sizeof(password), stdin)) {}
        restore_echo(&old_t);
        printf("\nLogin incorrect\n\n");
        sleep(1);
        continue;
    }

    /* Read shadow entry */
    struct spwd *sp = getspnam(username);
    const char *hash = sp ? sp->sp_pwdp : pw->pw_passwd;

    /* A missing shadow entry, or a hash of "*" / "!", means this account is
     * LOCKED per standard shadow(5) semantics — daemon/bin/nobody and every
     * other system account on a normal distro carry one of these markers
     * specifically to say "no password login, ever". Treat it as a hard
     * deny, not as "no password required": letting anyone log straight
     * into a locked account without a password is a full authentication
     * bypass the moment a locked system account exists. */
    if (!hash || strcmp(hash, "*") == 0 || strcmp(hash, "!") == 0) {
        printf("\nLogin incorrect\n\n");
        sleep(1);
        continue;
    }

    /* An explicitly empty hash ("") is the deliberate, documented KratosOS
     * convention for "no password set" (see create-etc-skeleton.sh) — that
     * one case, and only that one (or force_login), skips the password prompt. */
    if (!force_login && strlen(hash) > 0) {
        struct termios old_t;
        disable_echo(&old_t);
        printf("Password: ");
        fflush(stdout);

        if (!fgets(password, sizeof(password), stdin)) {
            restore_echo(&old_t);
            return 1;
        }
        restore_echo(&old_t);
        printf("\n");

        size_t len = strlen(password);
        while (len > 0 && (password[len - 1] == '\n' || password[len - 1] == '\r')) {
            password[--len] = '\0';
        }

        /* Verify password hash via kratos_crypt */
        char *encrypted = kratos_crypt(password, hash);
        if (!encrypted || !constant_time_streq(encrypted, hash)) {
            printf("Login incorrect\n\n");
            sleep(1);
            continue;
        }
    } else {
        printf("\n");
    }

    /* ----------------------------------------------------------------
     * Authentication successful
     * ---------------------------------------------------------------- */

    const char *tty = ttyname(STDIN_FILENO);

    /* Dynamic last-login timestamp */
    {
        time_t now = time(NULL);
        char tbuf[64];
        struct tm *tm_info = localtime(&now);
        strftime(tbuf, sizeof(tbuf), "%a %b %e %H:%M:%S %Y", tm_info);
        printf("Last login: %s on %s\n", tbuf, tty ? tty : "tty");
    }

    /* Save TERM before clearing environment */
    const char *parent_term = getenv("TERM");
    char *term_copy = parent_term ? strdup(parent_term) : NULL;

    clearenv();

    /* Drop privileges & setup user environment */
    if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
        perror("[login] initgroups failed");
    }
    if (setgid(pw->pw_gid) < 0) {
        perror("[login] setgid failed");
    }
    if (setuid(pw->pw_uid) < 0) {
        perror("[login] setuid failed");
    }

    if (chdir(pw->pw_dir) < 0) {
        if (chdir("/") < 0) {}
    }

    setenv("USER",    pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    setenv("HOME",    pw->pw_dir,  1);
    setenv("SHELL",   pw->pw_shell[0] ? pw->pw_shell : "/bin/bash", 1);
    setenv("PATH",    "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);

    if (term_copy && term_copy[0] != '\0') {
        setenv("TERM", term_copy, 1);
        free(term_copy);
    } else {
        int is_serial = (tty && strncmp(tty, "/dev/ttyS", 9) == 0);
        setenv("TERM", is_serial ? "vt100" : "linux", 1);
    }

    const char *shell = pw->pw_shell[0] ? pw->pw_shell : "/bin/bash";

    /* argv[0] must be "-basename" (dash prefix) to signal a login shell.
     * Using a hardcoded "bash" would break if the user's shell is not bash,
     * and would also lose the dash prefix that many shells use to detect
     * login-shell mode and source /etc/profile. */
    char shell_copy[256];
    strncpy(shell_copy, shell, sizeof(shell_copy) - 1);
    shell_copy[sizeof(shell_copy) - 1] = '\0';
    char *base = basename(shell_copy);

    char argv0[258];
    argv0[0] = '-';
    strncpy(argv0 + 1, base, sizeof(argv0) - 2);
    argv0[sizeof(argv0) - 1] = '\0';

    restore_sane_termios();
    execl(shell, argv0, (char *)NULL);

    perror("[login] exec shell failed");
    return 1;

    } /* end retry loop */

    fprintf(stderr, "[login] Maximum login attempts reached. Disconnecting.\n");
    return 1;
}
