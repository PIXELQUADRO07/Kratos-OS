/* useradd.c — KratosOS Native User Account Creation Utility (/usr/sbin/useradd)
 *
 * Usage:
 *   useradd [options] <username>
 *
 * Options:
 *   -u, --uid <uid>       Specify user ID
 *   -g, --gid <gid>       Specify initial login group ID or name
 *   -d, --home <dir>      Home directory for the new user (default: /home/<username>)
 *   -s, --shell <shell>   Login shell for the new user (default: /bin/bash)
 *   -m, --create-home     Create the user's home directory
 *   -r, --system          Create a system account (UID < 1000)
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define PASSWD_FILE "/etc/passwd"
#define SHADOW_FILE "/etc/shadow"
#define GROUP_FILE  "/etc/group"

static void show_help(const char *prog)
{
    printf("Usage: %s [options] <username>\n\n", prog);
    printf("Options:\n");
    printf("  -u, --uid <uid>       Specify numerical user ID\n");
    printf("  -g, --gid <gid>       Specify numerical primary group ID or group name\n");
    printf("  -d, --home <dir>      Home directory (default: /home/<username>)\n");
    printf("  -s, --shell <shell>   Login shell (default: /bin/bash)\n");
    printf("  -m, --create-home     Create user home directory\n");
    printf("  -r, --system          Create system account (UID 100-999)\n");
    printf("  -h, --help            Show this help message\n");
}

static int is_valid_name(const char *name)
{
    if (!name || name[0] == '\0') return 0;
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') return 0;

    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.') {
            return 0;
        }
    }
    return (strlen(name) < 32);
}

static uid_t find_next_uid(int is_system)
{
    uid_t min_uid = is_system ? 100 : 1000;
    uid_t max_uid = is_system ? 999 : 60000;
    uid_t next_uid = min_uid;

    FILE *f = fopen(PASSWD_FILE, "r");
    if (!f) return next_uid;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = strchr(line, ':');
        if (!p) continue;
        p = strchr(p + 1, ':');
        if (!p) continue;

        uid_t u = (uid_t)strtoul(p + 1, NULL, 10);
        if (u >= next_uid && u <= max_uid) {
            next_uid = u + 1;
        }
    }
    fclose(f);
    return next_uid;
}

static gid_t find_next_gid(int is_system)
{
    gid_t min_gid = is_system ? 100 : 1000;
    gid_t max_gid = is_system ? 999 : 60000;
    gid_t next_gid = min_gid;

    FILE *f = fopen(GROUP_FILE, "r");
    if (!f) return next_gid;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = strchr(line, ':');
        if (!p) continue;
        p = strchr(p + 1, ':');
        if (!p) continue;

        gid_t g = (gid_t)strtoul(p + 1, NULL, 10);
        if (g >= next_gid && g <= max_gid) {
            next_gid = g + 1;
        }
    }
    fclose(f);
    return next_gid;
}

static int user_exists(const char *username)
{
    struct passwd *pw = getpwnam(username);
    return (pw != NULL);
}

static int group_exists(const char *groupname)
{
    struct group *gr = getgrnam(groupname);
    return (gr != NULL);
}

int main(int argc, char *argv[])
{
    if (getuid() != 0) {
        fprintf(stderr, "useradd: only root may add a user to the system.\n");
        return 1;
    }

    const char *username = NULL;
    uid_t uid = (uid_t)-1;
    gid_t gid = (gid_t)-1;
    char home_dir[256] = "";
    char shell[256] = "/bin/bash";
    int create_home = 0;
    int is_system = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--uid") == 0) && i + 1 < argc) {
            uid = (uid_t)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--gid") == 0) && i + 1 < argc) {
            const char *gstr = argv[++i];
            if (isdigit((unsigned char)gstr[0])) {
                gid = (gid_t)atoi(gstr);
            } else {
                struct group *gr = getgrnam(gstr);
                if (gr) {
                    gid = gr->gr_gid;
                } else {
                    fprintf(stderr, "useradd: group '%s' does not exist\n", gstr);
                    return 6;
                }
            }
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--home") == 0) && i + 1 < argc) {
            strncpy(home_dir, argv[++i], sizeof(home_dir) - 1);
        } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--shell") == 0) && i + 1 < argc) {
            strncpy(shell, argv[++i], sizeof(shell) - 1);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--create-home") == 0) {
            create_home = 1;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--system") == 0) {
            is_system = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            username = argv[i];
        }
    }

    if (!username) {
        show_help(argv[0]);
        return 2;
    }

    if (!is_valid_name(username)) {
        fprintf(stderr, "useradd: invalid user name '%s'\n", username);
        return 3;
    }

    if (user_exists(username)) {
        fprintf(stderr, "useradd: user '%s' already exists\n", username);
        return 9;
    }

    if (uid == (uid_t)-1) {
        uid = find_next_uid(is_system);
    }

    if (gid == (gid_t)-1) {
        if (!group_exists(username)) {
            gid = find_next_gid(is_system);
            /* Add group with same name as user */
            FILE *fg = fopen(GROUP_FILE, "a");
            if (fg) {
                fprintf(fg, "%s:x:%u:\n", username, gid);
                fclose(fg);
            }
        } else {
            struct group *gr = getgrnam(username);
            gid = gr ? gr->gr_gid : 1000;
        }
    }

    if (home_dir[0] == '\0') {
        if (is_system) {
            snprintf(home_dir, sizeof(home_dir), "/");
        } else {
            snprintf(home_dir, sizeof(home_dir), "/home/%s", username);
        }
    }

    /* 1. Append to /etc/passwd */
    FILE *fp = fopen(PASSWD_FILE, "a");
    if (!fp) {
        perror("useradd: cannot open " PASSWD_FILE);
        return 1;
    }
    fprintf(fp, "%s:x:%u:%u:%s:%s:%s\n", username, uid, gid, username, home_dir, shell);
    fclose(fp);

    /* 2. Append locked entry to /etc/shadow */
    FILE *fs = fopen(SHADOW_FILE, "a");
    if (fs) {
        long days = (long)(time(NULL) / 86400);
        fprintf(fs, "%s:!:%ld:0:99999:7:::\n", username, days);
        fclose(fs);
    }

    /* 3. Create Home Directory if requested */
    if (create_home && home_dir[0] != '\0' && strcmp(home_dir, "/") != 0) {
        mkdir("/home", 0755);
        if (mkdir(home_dir, 0750) == 0 || errno == EEXIST) {
            if (chown(home_dir, uid, gid) < 0) {}
            chmod(home_dir, 0750);
        }
    }

    printf("[useradd] User '%s' (UID %u, GID %u) created.\n", username, uid, gid);
    return 0;
}
