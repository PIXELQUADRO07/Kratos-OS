/* groupadd.c — KratosOS Native Group Creation Utility (/usr/sbin/groupadd)
 *
 * Usage:
 *   groupadd [-g gid] [-r] <groupname>
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GROUP_FILE "/etc/group"

static void show_help(const char *prog)
{
    printf("Usage: %s [-g gid] [-r] <groupname>\n", prog);
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

int main(int argc, char *argv[])
{
    if (getuid() != 0) {
        fprintf(stderr, "groupadd: only root may add a group to the system.\n");
        return 1;
    }

    const char *groupname = NULL;
    gid_t gid = (gid_t)-1;
    int is_system = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            gid = (gid_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--system") == 0) {
            is_system = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            groupname = argv[i];
        }
    }

    if (!groupname) {
        show_help(argv[0]);
        return 2;
    }

    if (!is_valid_name(groupname)) {
        fprintf(stderr, "groupadd: invalid group name '%s'\n", groupname);
        return 3;
    }

    if (getgrnam(groupname) != NULL) {
        fprintf(stderr, "groupadd: group '%s' already exists\n", groupname);
        return 9;
    }

    if (gid == (gid_t)-1) {
        gid = find_next_gid(is_system);
    }

    FILE *f = fopen(GROUP_FILE, "a");
    if (!f) {
        perror("groupadd: cannot open " GROUP_FILE);
        return 1;
    }
    fprintf(f, "%s:x:%u:\n", groupname, gid);
    fclose(f);

    printf("[groupadd] Group '%s' (GID %u) created.\n", groupname, gid);
    return 0;
}
