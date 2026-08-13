/* userdel.c — KratosOS Native User Account Deletion Utility (/usr/sbin/userdel)
 *
 * Usage:
 *   userdel [-r] <username>
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PASSWD_FILE "/etc/passwd"
#define SHADOW_FILE "/etc/shadow"
#define GROUP_FILE  "/etc/group"

static int rm_rf(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) return (errno == ENOENT) ? 0 : -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char sub[1024];
            snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
            rm_rf(sub);
        }
        closedir(d);
        return rmdir(path);
    } else {
        return unlink(path);
    }
}

static int remove_line_matching_user(const char *filepath, const char *username)
{
    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp-%d", filepath, getpid());

    FILE *in = fopen(filepath, "r");
    if (!in) return -1;

    FILE *out = fopen(tmpfile, "w");
    if (!out) {
        fclose(in);
        return -1;
    }

    size_t ulen = strlen(username);
    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, username, ulen) == 0 && line[ulen] == ':') {
            found = 1;
            continue; /* Skip deleting user */
        }
        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (found) {
        rename(tmpfile, filepath);
    } else {
        unlink(tmpfile);
    }
    return found ? 0 : -1;
}

int main(int argc, char *argv[])
{
    if (getuid() != 0) {
        fprintf(stderr, "userdel: only root may remove a user from the system.\n");
        return 1;
    }

    const char *username = NULL;
    int remove_home = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0) {
            remove_home = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-r] <username>\n", argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            username = argv[i];
        }
    }

    if (!username) {
        fprintf(stderr, "Usage: %s [-r] <username>\n", argv[0]);
        return 2;
    }

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "userdel: user '%s' does not exist\n", username);
        return 6;
    }

    char home[512] = {0};
    strncpy(home, pw->pw_dir, sizeof(home) - 1);

    remove_line_matching_user(PASSWD_FILE, username);
    remove_line_matching_user(SHADOW_FILE, username);
    remove_line_matching_user(GROUP_FILE, username);

    if (remove_home && home[0] && strcmp(home, "/") != 0 && strcmp(home, "/root") != 0) {
        rm_rf(home);
    }

    printf("[userdel] User '%s' removed.\n", username);
    return 0;
}
