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

    struct stat orig_st;
    int have_orig_st = (stat(filepath, &orig_st) == 0);

    FILE *in = fopen(filepath, "r");
    if (!in) return -1;

    /* Preserve the original file's mode explicitly instead of letting the
     * new file fall back to umask-derived permissions — this matters most
     * for /etc/shadow (0600): without this, the rename() below would
     * silently replace it with a world-readable file, exposing every
     * password hash on the system. */
    int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC,
                  have_orig_st ? (orig_st.st_mode & 07777) : 0644);
    if (fd < 0) {
        fclose(in);
        return -1;
    }
    FILE *out = fdopen(fd, "w");
    if (!out) {
        close(fd);
        fclose(in);
        unlink(tmpfile);
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

/* Remove `username` from every group's supplementary member list (the
 * comma-separated 4th field of /etc/group), not just the same-named
 * private group entry deleted above — otherwise a deleted user stays
 * listed as a member of e.g. 'sudo', which a future account reusing that
 * name (or UID) would silently inherit. */
static int remove_user_from_all_groups(const char *username)
{
    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp-%d", GROUP_FILE, getpid());

    struct stat orig_st;
    int have_orig_st = (stat(GROUP_FILE, &orig_st) == 0);

    FILE *in = fopen(GROUP_FILE, "r");
    if (!in) return -1;

    int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC,
                  have_orig_st ? (orig_st.st_mode & 07777) : 0644);
    if (fd < 0) {
        fclose(in);
        return -1;
    }
    FILE *out = fdopen(fd, "w");
    if (!out) {
        close(fd);
        fclose(in);
        unlink(tmpfile);
        return -1;
    }

    size_t ulen = strlen(username);
    char line[1024];
    int changed = 0;

    while (fgets(line, sizeof(line), in)) {
        size_t l = strlen(line);
        int had_nl = (l > 0 && line[l-1] == '\n');
        if (had_nl) line[--l] = '\0';

        /* Split into name:passwd:gid:members */
        char *fields[4];
        char *buf = strdup(line);
        char *tok = strtok(buf, ":");
        int nf = 0;
        while (tok && nf < 4) { fields[nf++] = tok; tok = strtok(NULL, ":"); }

        if (nf == 4) {
            /* Walk the comma-separated member list, dropping any entry
             * that exactly matches username. */
            char rebuilt[1024] = "";
            char *mbuf = strdup(fields[3]);
            char *mtok = strtok(mbuf, ",");
            int first = 1;
            int removed_here = 0;
            while (mtok) {
                if (strcmp(mtok, username) == 0) {
                    removed_here = 1;
                } else {
                    if (!first) strcat(rebuilt, ",");
                    strncat(rebuilt, mtok, sizeof(rebuilt) - strlen(rebuilt) - 1);
                    first = 0;
                }
                mtok = strtok(NULL, ",");
            }
            free(mbuf);

            if (removed_here) {
                changed = 1;
                fprintf(out, "%s:%s:%s:%s\n", fields[0], fields[1], fields[2], rebuilt);
            } else {
                fprintf(out, "%s\n", line);
            }
        } else {
            fprintf(out, "%s\n", line);
        }
        free(buf);
        (void)ulen;
    }

    fclose(in);
    fclose(out);

    if (changed) {
        rename(tmpfile, GROUP_FILE);
    } else {
        unlink(tmpfile);
    }
    return 0;
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
    remove_user_from_all_groups(username);

    if (remove_home && home[0] && strcmp(home, "/") != 0 && strcmp(home, "/root") != 0) {
        rm_rf(home);
    }

    printf("[userdel] User '%s' removed.\n", username);
    return 0;
}
