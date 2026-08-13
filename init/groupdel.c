/* groupdel.c — KratosOS Native Group Deletion Utility (/usr/sbin/groupdel)
 *
 * Usage:
 *   groupdel <groupname>
 */

#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GROUP_FILE "/etc/group"

static int remove_group_entry(const char *groupname)
{
    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp-%d", GROUP_FILE, getpid());

    FILE *in = fopen(GROUP_FILE, "r");
    if (!in) return -1;

    FILE *out = fopen(tmpfile, "w");
    if (!out) {
        fclose(in);
        return -1;
    }

    size_t glen = strlen(groupname);
    char line[1024];
    int found = 0;

    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, groupname, glen) == 0 && line[glen] == ':') {
            found = 1;
            continue;
        }
        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    if (found) {
        rename(tmpfile, GROUP_FILE);
    } else {
        unlink(tmpfile);
    }
    return found ? 0 : -1;
}

int main(int argc, char *argv[])
{
    if (getuid() != 0) {
        fprintf(stderr, "groupdel: only root may remove a group from the system.\n");
        return 1;
    }

    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s <groupname>\n", argv[0]);
        return 1;
    }

    const char *groupname = argv[1];

    if (getgrnam(groupname) == NULL) {
        fprintf(stderr, "groupdel: group '%s' does not exist\n", groupname);
        return 6;
    }

    if (remove_group_entry(groupname) == 0) {
        printf("[groupdel] Group '%s' removed.\n", groupname);
        return 0;
    } else {
        fprintf(stderr, "groupdel: failed to remove group '%s'\n", groupname);
        return 1;
    }
}
