/* login.c — KratosOS User Login Manager (/bin/login)
 *
 * Responsabilità:
 *   1. Chiede l'username e la password (disabilitando l'echo del terminale)
 *   2. Verifica l'utente su /etc/passwd e /etc/shadow usando kratos_crypt() (SHA-512)
 *   3. Imposta i gruppi supplementari, GID e UID dell'utente (drop dei privilegi root)
 *   4. Prepara l'ambiente (HOME, USER, LOGNAME, SHELL, PATH) e lancia la login shell.
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /bin/login login.c kratos-crypt.c
 */

#define _GNU_SOURCE

#include "kratos-crypt.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

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
        return 1;
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
        if (!encrypted || strcmp(encrypted, hash) != 0) {
            printf("Login incorrect\n\n");
            return 1;
        }
    } else {
        printf("\n");
    }

    printf("Last login: Sun Aug  9 18:38:00 2026 on %s\n", ttyname(STDIN_FILENO) ? ttyname(STDIN_FILENO) : "tty1");

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
    execl(shell, "bash", "--login", (char *)NULL);

    perror("[login] exec shell failed");
    return 1;
}
