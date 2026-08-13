/* kratos-mount.c — KratosOS Native Mount/Umount Utility
 *
 * Usage:
 *   mount                         List currently mounted filesystems
 *   mount <device> <mountpoint>   Mount a filesystem
 *   mount -t <type> <device> <mountpoint>
 *   mount -o <options> <device> <mountpoint>
 *   mount -a                      Mount all entries from /etc/fstab
 *   umount <mountpoint>           Unmount a filesystem
 *
 * When invoked as "umount", acts as the unmount command.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define FSTAB_FILE "/etc/fstab"
#define MTAB_FILE  "/proc/mounts"

static void show_help_mount(const char *prog)
{
    printf("Usage: %s [options] [device] [mountpoint]\n\n", prog);
    printf("Options:\n");
    printf("  -t <type>     Filesystem type (ext4, vfat, proc, sysfs, ...)\n");
    printf("  -o <options>  Mount options (ro, rw, noexec, nosuid, ...)\n");
    printf("  -a            Mount all from /etc/fstab\n");
    printf("  -h            Show this help\n");
    printf("\n  With no arguments, lists currently mounted filesystems.\n");
}

static void show_help_umount(const char *prog)
{
    printf("Usage: %s <mountpoint>\n", prog);
    printf("  -h    Show this help\n");
}

static unsigned long parse_mount_flags(const char *opts)
{
    unsigned long flags = 0;
    if (!opts) return flags;

    char *copy = strdup(opts);
    char *tok = strtok(copy, ",");
    while (tok) {
        if (strcmp(tok, "ro") == 0)        flags |= MS_RDONLY;
        else if (strcmp(tok, "noexec") == 0)  flags |= MS_NOEXEC;
        else if (strcmp(tok, "nosuid") == 0)  flags |= MS_NOSUID;
        else if (strcmp(tok, "nodev") == 0)   flags |= MS_NODEV;
        else if (strcmp(tok, "noatime") == 0) flags |= MS_NOATIME;
        else if (strcmp(tok, "remount") == 0) flags |= MS_REMOUNT;
        else if (strcmp(tok, "bind") == 0)    flags |= MS_BIND;
        tok = strtok(NULL, ",");
    }
    free(copy);
    return flags;
}

static int list_mounts(void)
{
    FILE *f = fopen(MTAB_FILE, "r");
    if (!f) {
        perror("mount: cannot open " MTAB_FILE);
        return 1;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char dev[256], mp[256], type[64], opts[512];
        if (sscanf(line, "%255s %255s %63s %511s", dev, mp, type, opts) >= 3) {
            printf("%s on %s type %s (%s)\n", dev, mp, type, opts);
        }
    }
    fclose(f);
    return 0;
}

static int do_mount(const char *dev, const char *mp, const char *type, unsigned long flags)
{
    const char *fstype = type ? type : "auto";

    /* Try common filesystem types if "auto" */
    if (strcmp(fstype, "auto") == 0) {
        const char *try_types[] = {"ext4", "ext3", "ext2", "vfat", "xfs", "btrfs", NULL};
        for (int i = 0; try_types[i]; i++) {
            if (mount(dev, mp, try_types[i], flags, NULL) == 0) {
                return 0;
            }
        }
        fprintf(stderr, "mount: %s: cannot detect filesystem type\n", dev);
        return 1;
    }

    if (mount(dev, mp, fstype, flags, NULL) < 0) {
        fprintf(stderr, "mount: mounting %s on %s: %s\n", dev, mp, strerror(errno));
        return 1;
    }
    return 0;
}

static int mount_fstab(void)
{
    FILE *f = fopen(FSTAB_FILE, "r");
    if (!f) {
        perror("mount: cannot open " FSTAB_FILE);
        return 1;
    }
    char line[1024];
    int errors = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Skip comments and blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char dev[256], mp[256], type[64], opts[512];
        int dump, pass;
        if (sscanf(p, "%255s %255s %63s %511s %d %d", dev, mp, type, opts, &dump, &pass) < 3)
            continue;

        /* Skip swap and noauto entries */
        if (strcmp(type, "swap") == 0) continue;
        if (strstr(opts, "noauto")) continue;

        /* Skip pseudo-filesystems already mounted by init */
        if (strcmp(mp, "/") == 0 || strcmp(mp, "/proc") == 0 ||
            strcmp(mp, "/sys") == 0 || strcmp(mp, "/dev") == 0 ||
            strcmp(mp, "/run") == 0) continue;

        unsigned long flags = parse_mount_flags(opts);
        mkdir(mp, 0755);
        if (do_mount(dev, mp, type, flags) != 0) {
            fprintf(stderr, "mount: failed to mount %s on %s\n", dev, mp);
            errors++;
        }
    }
    fclose(f);
    return errors ? 1 : 0;
}

static int do_umount(const char *mp)
{
    if (umount(mp) < 0) {
        if (errno == EBUSY) {
            /* Try lazy umount */
            if (umount2(mp, MNT_DETACH) < 0) {
                fprintf(stderr, "umount: %s: %s\n", mp, strerror(errno));
                return 1;
            }
        } else {
            fprintf(stderr, "umount: %s: %s\n", mp, strerror(errno));
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    /* Determine if invoked as "umount" */
    const char *progname = strrchr(argv[0], '/');
    progname = progname ? progname + 1 : argv[0];
    int is_umount = (strcmp(progname, "umount") == 0);

    if (is_umount) {
        if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            show_help_umount(argv[0]);
            return (argc < 2) ? 1 : 0;
        }
        if (getuid() != 0) {
            fprintf(stderr, "umount: must be root\n");
            return 1;
        }
        return do_umount(argv[1]);
    }

    /* mount mode */
    if (argc == 1) {
        return list_mounts();
    }

    const char *type = NULL;
    const char *opts_str = NULL;
    const char *device = NULL;
    const char *mountpoint = NULL;
    int mount_all = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help_mount(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            type = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts_str = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0) {
            mount_all = 1;
        } else if (!device) {
            device = argv[i];
        } else if (!mountpoint) {
            mountpoint = argv[i];
        }
    }

    if (getuid() != 0) {
        fprintf(stderr, "mount: must be root\n");
        return 1;
    }

    if (mount_all) {
        return mount_fstab();
    }

    if (!device || !mountpoint) {
        fprintf(stderr, "mount: usage: mount [-t type] [-o opts] <device> <mountpoint>\n");
        return 1;
    }

    unsigned long flags = parse_mount_flags(opts_str);
    return do_mount(device, mountpoint, type, flags);
}
