/* passwd.c — KratosOS Password Update Utility (/usr/bin/passwd)
 *
 * Responsabilità:
 *   1. Permette all'utente root (o all'utente corrente) di cambiare password
 *   2. Genera un hash SHA-512 ($6$) e aggiorna atomicamente /etc/shadow
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /usr/bin/passwd passwd.c kratos-crypt.c
 */

#define _GNU_SOURCE

#include "kratos-crypt.h"
#include <errno.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
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

int main(int argc, char *argv[])
{
    const char *username = "root";

    if (argc >= 2) {
        username = argv[1];
    } else {
        struct passwd *pw = getpwuid(getuid());
        if (pw) username = pw->pw_name;
    }

    if (getuid() != 0) {
        fprintf(stderr, "[passwd] Only root can change passwords.\n");
        return 1;
    }

    printf("Changing password for %s.\n", username);

    struct termios old_t;
    char pass1[128] = {0};
    char pass2[128] = {0};

    disable_echo(&old_t);
    printf("New password: ");
    fflush(stdout);
    if (!fgets(pass1, sizeof(pass1), stdin)) { restore_echo(&old_t); return 1; }
    restore_echo(&old_t);
    printf("\n");

    disable_echo(&old_t);
    printf("Retype new password: ");
    fflush(stdout);
    if (!fgets(pass2, sizeof(pass2), stdin)) { restore_echo(&old_t); return 1; }
    restore_echo(&old_t);
    printf("\n");

    size_t len1 = strlen(pass1);
    while (len1 > 0 && (pass1[len1-1] == '\n' || pass1[len1-1] == '\r')) pass1[--len1] = '\0';

    size_t len2 = strlen(pass2);
    while (len2 > 0 && (pass2[len2-1] == '\n' || pass2[len2-1] == '\r')) pass2[--len2] = '\0';

    if (strcmp(pass1, pass2) != 0) {
        fprintf(stderr, "Sorry, passwords do not match.\n");
        return 1;
    }

    if (len1 == 0) {
        fprintf(stderr, "Password cannot be empty.\n");
        return 1;
    }

    /* Generate SHA-512 salt & hash via kratos_crypt */
    char salt[64];
    kratos_gensalt(salt, sizeof(salt));

    char *hash = kratos_crypt(pass1, salt);
    if (!hash) {
        fprintf(stderr, "[passwd] Error generating password hash.\n");
        return 1;
    }

    /* Update /etc/shadow */
    const char *shadow_file = "/etc/shadow";
    const char *shadow_tmp  = "/etc/shadow.tmp";

    FILE *fin = fopen(shadow_file, "r");
    FILE *fout = fopen(shadow_tmp, "w");

    if (!fin || !fout) {
        perror("[passwd] Cannot open /etc/shadow");
        if (fin) fclose(fin);
        if (fout) fclose(fout);
        return 1;
    }

    char line[512];
    int updated = 0;
    long days = time(NULL) / 86400;

    while (fgets(line, sizeof(line), fin)) {
        char copy[512];
        snprintf(copy, sizeof(copy), "%s", line);

        char *token = strtok(copy, ":");
        if (token && strcmp(token, username) == 0) {
            fprintf(fout, "%s:%s:%ld:0:99999:7:::\n", username, hash, days);
            updated = 1;
        } else {
            fputs(line, fout);
        }
    }

    if (!updated) {
        fprintf(fout, "%s:%s:%ld:0:99999:7:::\n", username, hash, days);
    }

    fclose(fin);
    fclose(fout);

    if (rename(shadow_tmp, shadow_file) < 0) {
        perror("[passwd] Cannot replace /etc/shadow");
        return 1;
    }

    printf("passwd: password updated successfully\n");
    return 0;
}
