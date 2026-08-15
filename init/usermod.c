/* usermod.c — KratosOS Native User Account Modification Utility (/usr/sbin/usermod)
 *
 * Usage:
 *   usermod [options] <username>
 *
 * Options:
 *   -u, --uid <uid>       Set numerical user ID
 *   -g, --gid <gid>       Set primary group ID or name
 *   -d, --home <dir>      Set home directory
 *   -s, --shell <shell>   Set login shell
 *   -l, --login <name>    Change login name (username)
 *   -h, --help            Show this help message
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PASSWD_FILE "/etc/passwd"
#define SHADOW_FILE "/etc/shadow"
#define GROUP_FILE  "/etc/group"

static void show_help(const char *prog) {
    printf("Usage: %s [options] <username>\n", prog);
    printf("Options:\n");
    printf("  -u, --uid <uid>       Set numerical user ID\n");
    printf("  -g, --gid <gid>       Set primary group ID or name\n");
    printf("  -d, --home <dir>      Set home directory\n");
    printf("  -s, --shell <shell>   Set login shell\n");
    printf("  -l, --login <name>    Change login name (username)\n");
    printf("  -h, --help            Show this help message\n");
}

static int is_valid_name(const char *name) {
    if (!name || name[0] == '\0') return 0;
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') return 0;
    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.') {
            return 0;
        }
    }
    return (strlen(name) < 32);
}

static int parse_gid(const char *arg, gid_t *out_gid) {
    if (isdigit((unsigned char)arg[0])) {
        *out_gid = (gid_t)atoi(arg);
        return 0;
    }
    struct group *gr = getgrnam(arg);
    if (!gr) return -1;
    *out_gid = gr->gr_gid;
    return 0;
}

int main(int argc, char *argv[]) {
    if (getuid() != 0) {
        fprintf(stderr, "usermod: only root may modify user accounts.\n");
        return 1;
    }
    const char *username = NULL;
    uid_t new_uid = (uid_t)-1;
    gid_t new_gid = (gid_t)-1;
    char new_home[256] = "";
    char new_shell[256] = "";
    char new_login[256] = "";
    int change_login = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--uid") == 0) && i + 1 < argc) {
            new_uid = (uid_t)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gid") == 0) && i + 1 < argc) {
            if (parse_gid(argv[++i], &new_gid) != 0) {
                fprintf(stderr, "usermod: invalid group '%s'\n", argv[i]);
                return 2;
            }
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--home") == 0) && i + 1 < argc) {
            strncpy(new_home, argv[++i], sizeof(new_home) - 1);
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shell") == 0) && i + 1 < argc) {
            strncpy(new_shell, argv[++i], sizeof(new_shell) - 1);
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--login") == 0) && i + 1 < argc) {
            strncpy(new_login, argv[++i], sizeof(new_login) - 1);
            change_login = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            username = argv[i];
        } else {
            fprintf(stderr, "usermod: unknown option %s\n", argv[i]);
            return 3;
        }
    }
    if (!username) {
        show_help(argv[0]);
        return 4;
    }
    if (!is_valid_name(username)) {
        fprintf(stderr, "usermod: invalid username '%s'\n", username);
        return 5;
    }
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "usermod: user '%s' does not exist\n", username);
        return 6;
    }
    /* Load /etc/passwd lines */
    FILE *src = fopen(PASSWD_FILE, "r");
    if (!src) { perror("usermod: fopen /etc/passwd"); return 7; }
    char **lines = NULL; size_t nlines = 0; char linebuf[1024];
    while (fgets(linebuf, sizeof(linebuf), src)) {
        lines = realloc(lines, (nlines + 1) * sizeof(char*));
        lines[nlines++] = strdup(linebuf);
    }
    fclose(src);
    /* Rewrite with modifications */
    FILE *dst = fopen(PASSWD_FILE, "w");
    if (!dst) { perror("usermod: fopen for rewrite"); return 8; }
    for (size_t i = 0; i < nlines; i++) {
        char *ln = lines[i];
        char *save = strdup(ln); // keep original for non‑matching lines
        /* Strip the trailing newline before tokenizing: strtok's last
         * field would otherwise retain it, and the explicit \n added by
         * fprintf() below would then produce a blank line after the
         * rewritten entry. */
        size_t lnlen = strlen(ln);
        while (lnlen > 0 && (ln[lnlen-1] == '\n' || ln[lnlen-1] == '\r')) ln[--lnlen] = '\0';
        char *tok = strtok(ln, ":");
        if (!tok) { fputs(save, dst); free(save); free(lines[i]); continue; }
        if (strcmp(tok, username) == 0) {
            char *fields[7];
            fields[0] = change_login && new_login[0] ? new_login : tok;
            for (int f = 1; f < 7; f++) {
                char *next = strtok(NULL, ":");
                fields[f] = next ? next : "";
            }
            if (new_uid != (uid_t)-1) {
                static char uidbuf[32];
                sprintf(uidbuf, "%u", new_uid);
                fields[2] = uidbuf;
            }
            if (new_gid != (gid_t)-1) {
                static char gidbuf[32];
                sprintf(gidbuf, "%u", new_gid);
                fields[3] = gidbuf;
            }
            if (new_home[0]) fields[5] = new_home;
            if (new_shell[0]) fields[6] = new_shell;
            fprintf(dst, "%s:%s:%s:%s:%s:%s:%s\n",
                    fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6]);
            free(save);
        } else {
            fputs(save, dst);
            free(save);
        }
        free(lines[i]);
    }
    free(lines);
    fclose(dst);
    printf("[usermod] Modified user '%s'\n", username);
    return 0;
}
