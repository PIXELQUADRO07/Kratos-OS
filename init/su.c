/* su.c — KratosOS Native Switch User Utility (/bin/su)
 *
 * Usage:
 *   su [-] [username]
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

static void get_password(char *buf, size_t size)
{
    struct termios old_t, new_t;
    tcgetattr(STDIN_FILENO, &old_t);
    new_t = old_t;
    new_t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_t);

    printf("Password: ");
    fflush(stdout);

    if (fgets(buf, (int)size, stdin) != NULL) {
        size_t l = strlen(buf);
        while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = '\0';
    } else {
        buf[0] = '\0';
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    printf("\n");
}

int main(int argc, char *argv[])
{
    const char *target_user = "root";
    int login_shell = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0 || strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--login") == 0) {
            login_shell = 1;
        } else if (argv[i][0] != '-') {
            target_user = argv[i];
        }
    }

    struct passwd *pw = getpwnam(target_user);
    if (!pw) {
        fprintf(stderr, "su: user '%s' does not exist\n", target_user);
        return 1;
    }

    /* If caller is not root, ask for target user's password */
    if (getuid() != 0) {
        struct spwd *sp = getspnam(target_user);
        if (!sp || !sp->sp_pwdp) {
            fprintf(stderr, "su: authentication failed\n");
            return 1;
        }

        /* Check for locked or disabled account */
        if (sp->sp_pwdp[0] == '!' || sp->sp_pwdp[0] == '*' || sp->sp_pwdp[0] == '\0') {
            fprintf(stderr, "su: account is locked\n");
            return 1;
        }

        char pass[128];
        get_password(pass, sizeof(pass));

        char *computed = kratos_crypt(pass, sp->sp_pwdp);
        if (!computed || constant_time_streq(computed, sp->sp_pwdp) == 0) {
            fprintf(stderr, "su: Authentication failure\n");
            return 1;
        }
    }

    /* Setup supplementary groups, GID, and UID */
    if (initgroups(target_user, pw->pw_gid) != 0) {
        perror("su: initgroups");
        return 1;
    }

    if (setgid(pw->pw_gid) != 0) {
        perror("su: setgid");
        return 1;
    }

    if (setuid(pw->pw_uid) != 0) {
        perror("su: setuid");
        return 1;
    }

    /* Environment setup */
    if (login_shell) {
        clearenv();
        setenv("HOME", pw->pw_dir, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("SHELL", pw->pw_shell, 1);
        setenv("PATH", (pw->pw_uid == 0) ? "/sbin:/bin:/usr/sbin:/usr/bin" : "/bin:/usr/bin", 1);
        setenv("TERM", "xterm-256color", 1);
        if (chdir(pw->pw_dir) < 0) {
            if (chdir("/") < 0) {}
        }
    } else {
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("HOME", pw->pw_dir, 1);
    }

    const char *shell = pw->pw_shell[0] ? pw->pw_shell : "/bin/bash";
    char shell_arg0[64];
    if (login_shell) {
        snprintf(shell_arg0, sizeof(shell_arg0), "-%s", (strrchr(shell, '/') ? strrchr(shell, '/') + 1 : shell));
    } else {
        snprintf(shell_arg0, sizeof(shell_arg0), "%s", (strrchr(shell, '/') ? strrchr(shell, '/') + 1 : shell));
    }

    execl(shell, shell_arg0, (char *)NULL);
    perror("su: exec shell failed");
    return 1;
}
