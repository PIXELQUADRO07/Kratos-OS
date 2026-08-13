/* groups.c — KratosOS Native Group Membership Utility (/usr/bin/groups)
 *
 * Usage:
 *   groups [username]
 *
 * Shows the groups a user belongs to. If no username is given,
 * shows groups for the current user.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int show_groups_for_user(const char *username)
{
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "groups: '%s': no such user\n", username);
        return 1;
    }

    /* Get number of supplementary groups */
    int ngroups = 0;
    getgrouplist(username, pw->pw_gid, NULL, &ngroups);
    if (ngroups <= 0) ngroups = 1;

    gid_t *gids = malloc((size_t)ngroups * sizeof(gid_t));
    if (!gids) { perror("groups: malloc"); return 1; }

    if (getgrouplist(username, pw->pw_gid, gids, &ngroups) < 0) {
        /* Fallback: just show primary group */
        ngroups = 1;
        gids[0] = pw->pw_gid;
    }

    printf("%s :", username);
    for (int i = 0; i < ngroups; i++) {
        struct group *gr = getgrgid(gids[i]);
        if (gr) {
            printf(" %s", gr->gr_name);
        } else {
            printf(" %u", gids[i]);
        }
    }
    printf("\n");

    free(gids);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        /* Show groups for each specified user */
        int ret = 0;
        for (int i = 1; i < argc; i++) {
            if (show_groups_for_user(argv[i]) != 0)
                ret = 1;
        }
        return ret;
    }

    /* No argument: show groups for current user */
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        fprintf(stderr, "groups: cannot find name for user ID %u\n", uid);
        return 1;
    }
    return show_groups_for_user(pw->pw_name);
}
