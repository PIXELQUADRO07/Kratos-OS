/* id.c — KratosOS Native User/Group Identity Utility (/usr/bin/id)
 *
 * Usage:
 *   id [options] [username]
 *
 * Options:
 *   -u    Print only the effective user ID
 *   -g    Print only the effective group ID
 *   -G    Print all group IDs
 *   -n    Print name instead of number (with -u, -g, -G)
 *   -r    Print real ID instead of effective (with -u, -g)
 *   -h    Show this help message
 *
 * With no options, prints: uid=N(name) gid=N(name) groups=N(name),...
 */

#define _GNU_SOURCE

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void show_help(const char *prog)
{
    printf("Usage: %s [options] [username]\n\n", prog);
    printf("Options:\n");
    printf("  -u    Print only the effective user ID\n");
    printf("  -g    Print only the effective group ID\n");
    printf("  -G    Print all group IDs\n");
    printf("  -n    Print name instead of number (with -u, -g, -G)\n");
    printf("  -r    Print real ID instead of effective (with -u, -g)\n");
    printf("  -h    Show help\n");
}

int main(int argc, char *argv[])
{
    int opt_u = 0, opt_g = 0, opt_G = 0, opt_n = 0, opt_r = 0;
    const char *target_user = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (const char *c = argv[i] + 1; *c; c++) {
                switch (*c) {
                    case 'u': opt_u = 1; break;
                    case 'g': opt_g = 1; break;
                    case 'G': opt_G = 1; break;
                    case 'n': opt_n = 1; break;
                    case 'r': opt_r = 1; break;
                    case 'h': show_help(argv[0]); return 0;
                    default:
                        fprintf(stderr, "id: invalid option -- '%c'\n", *c);
                        return 1;
                }
            }
        } else {
            target_user = argv[i];
        }
    }

    uid_t uid, euid;
    gid_t gid, egid;
    struct passwd *pw;

    if (target_user) {
        pw = getpwnam(target_user);
        if (!pw) {
            fprintf(stderr, "id: '%s': no such user\n", target_user);
            return 1;
        }
        uid = euid = pw->pw_uid;
        gid = egid = pw->pw_gid;
    } else {
        uid = getuid();
        euid = geteuid();
        gid = getgid();
        egid = getegid();
        pw = getpwuid(opt_r ? uid : euid);
    }

    uid_t the_uid = opt_r ? uid : euid;
    gid_t the_gid = opt_r ? gid : egid;

    /* -u: print only UID */
    if (opt_u) {
        if (opt_n) {
            struct passwd *p = getpwuid(the_uid);
            printf("%s\n", p ? p->pw_name : "???");
        } else {
            printf("%u\n", the_uid);
        }
        return 0;
    }

    /* -g: print only GID */
    if (opt_g) {
        if (opt_n) {
            struct group *gr = getgrgid(the_gid);
            printf("%s\n", gr ? gr->gr_name : "???");
        } else {
            printf("%u\n", the_gid);
        }
        return 0;
    }

    /* -G: print all group IDs */
    if (opt_G) {
        const char *uname = target_user;
        if (!uname) {
            struct passwd *p = getpwuid(uid);
            uname = p ? p->pw_name : NULL;
        }
        if (!uname) {
            printf("%u\n", the_gid);
            return 0;
        }

        int ngroups = 0;
        getgrouplist(uname, the_gid, NULL, &ngroups);
        if (ngroups <= 0) ngroups = 1;

        gid_t *gids = malloc((size_t)ngroups * sizeof(gid_t));
        if (!gids) { perror("id: malloc"); return 1; }

        if (getgrouplist(uname, the_gid, gids, &ngroups) < 0) {
            ngroups = 1;
            gids[0] = the_gid;
        }

        for (int i = 0; i < ngroups; i++) {
            if (i > 0) putchar(' ');
            if (opt_n) {
                struct group *gr = getgrgid(gids[i]);
                printf("%s", gr ? gr->gr_name : "???");
            } else {
                printf("%u", gids[i]);
            }
        }
        putchar('\n');
        free(gids);
        return 0;
    }

    /* Default full output: uid=N(name) gid=N(name) groups=N(name),... */
    struct passwd *pu = getpwuid(uid);
    struct group *gg = getgrgid(gid);

    printf("uid=%u", uid);
    if (pu) printf("(%s)", pu->pw_name);

    printf(" gid=%u", gid);
    if (gg) printf("(%s)", gg->gr_name);

    if (euid != uid) {
        struct passwd *epu = getpwuid(euid);
        printf(" euid=%u", euid);
        if (epu) printf("(%s)", epu->pw_name);
    }
    if (egid != gid) {
        struct group *egg = getgrgid(egid);
        printf(" egid=%u", egid);
        if (egg) printf("(%s)", egg->gr_name);
    }

    /* Supplementary groups */
    const char *uname = target_user;
    if (!uname && pu) uname = pu->pw_name;

    if (uname) {
        int ngroups = 0;
        getgrouplist(uname, egid, NULL, &ngroups);
        if (ngroups <= 0) ngroups = 1;

        gid_t *gids = malloc((size_t)ngroups * sizeof(gid_t));
        if (gids) {
            if (getgrouplist(uname, egid, gids, &ngroups) >= 0) {
                printf(" groups=");
                for (int i = 0; i < ngroups; i++) {
                    if (i > 0) putchar(',');
                    struct group *sg = getgrgid(gids[i]);
                    printf("%u", gids[i]);
                    if (sg) printf("(%s)", sg->gr_name);
                }
            }
            free(gids);
        }
    }

    putchar('\n');
    return 0;
}
