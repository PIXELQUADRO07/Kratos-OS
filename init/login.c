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
}

static void print_issue(void)
{
    FILE *f = fopen("/etc/issue", "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            fputs(buf, stdout);
        }
        fclose(f);
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    print_issue();
    fflush(stdout);

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {

    char username[128] = {0};
    char password[128] = {0};

    /* Prompt username */
    while (username[0] == '\0') {
        printf("kratos login: ");
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

    /* Prompt password (se presente in shadow) */
    if (hash && strcmp(hash, "*") != 0 && strcmp(hash, "!") != 0 && strlen(hash) > 0) {
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

    /* Dynamic last-login timestamp */
    {
        time_t now = time(NULL);
        char tbuf[64];
        struct tm *tm_info = localtime(&now);
        strftime(tbuf, sizeof(tbuf), "%a %b %e %H:%M:%S %Y", tm_info);
        const char *tty = ttyname(STDIN_FILENO);
        printf("Last login: %s on %s\n", tbuf, tty ? tty : "tty");
    }

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
    setenv("TERM",    "linux", 1);

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
