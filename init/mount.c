/* mount.c — KratosOS Init Filesystem Mounting Module
 *
 * Handles essential VFS mounts (/proc, /sys, /dev, /dev/pts, /dev/shm, /run, /tmp)
 * and automatic mounting of entries from /etc/fstab with LABEL= and UUID= resolution.
 */

#include "mount.h"
#include <mntent.h>

static void try_mount(const char *src, const char *tgt,
                      const char *type, unsigned long flags,
                      const char *data)
{
    if (mount(src, tgt, type, flags, data) < 0 && errno != EBUSY) {
        fprintf(stderr, "[init] WARNING: mount %s on %s failed: %s\n",
                src, tgt, strerror(errno));
    }
}

void mount_vfs(void)
{
    fprintf(stderr, "[init] Mounting virtual filesystems...\n");

    try_mount("proc",     "/proc",     "proc",     MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    try_mount("sysfs",    "/sys",      "sysfs",    MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    try_mount("devtmpfs", "/dev",      "devtmpfs", MS_NOSUID,                        "mode=0755,size=10m");

    mkdir("/dev/pts", 0755);
    try_mount("devpts",   "/dev/pts",  "devpts",   MS_NOSUID | MS_NOEXEC,            "mode=0620,gid=5");

    mkdir("/dev/shm", 1777);
    try_mount("tmpfs",    "/dev/shm",  "tmpfs",    MS_NOSUID | MS_NODEV,             "mode=1777");

    mkdir("/run", 0755);
    try_mount("tmpfs",    "/run",      "tmpfs",    MS_NOSUID | MS_NODEV,             "mode=0755,size=64m");

    mkdir("/tmp", 1777);
    try_mount("tmpfs",    "/tmp",      "tmpfs",    MS_NOSUID | MS_NODEV,             "mode=1777,size=128m");

    fprintf(stderr, "[init] Virtual filesystems mounted.\n");
}

static const char *resolve_dev_spec(const char *spec, char *buf, size_t buflen)
{
    if (strncmp(spec, "LABEL=", 6) == 0) {
        snprintf(buf, buflen, "/dev/disk/by-label/%s", spec + 6);
        return buf;
    }
    if (strncmp(spec, "UUID=", 5) == 0) {
        snprintf(buf, buflen, "/dev/disk/by-uuid/%s", spec + 5);
        return buf;
    }
    return spec;
}

void mount_fstab(void)
{
    FILE *f = setmntent("/etc/fstab", "r");
    if (!f) {
        return;
    }

    struct mntent mnt;
    char buf[1024];

    fprintf(stderr, "[init] Mounting filesystems from /etc/fstab...\n");

    while (getmntent_r(f, &mnt, buf, sizeof(buf))) {
        if (strcmp(mnt.mnt_type, "proc") == 0 ||
            strcmp(mnt.mnt_type, "sysfs") == 0 ||
            strcmp(mnt.mnt_type, "devtmpfs") == 0 ||
            strcmp(mnt.mnt_type, "devpts") == 0 ||
            strcmp(mnt.mnt_type, "tmpfs") == 0)
        {
            continue; /* Already mounted in mount_vfs() */
        }

        char resolved[512];
        const char *target_dev = resolve_dev_spec(mnt.mnt_fsname, resolved, sizeof(resolved));

        mkdir(mnt.mnt_dir, 0755);
        if (mount(target_dev, mnt.mnt_dir, mnt.mnt_type, 0, mnt.mnt_opts) == 0) {
            fprintf(stderr, "[init] Mounted %s on %s (%s)\n",
                    target_dev, mnt.mnt_dir, mnt.mnt_type);
        }
    }

    endmntent(f);
}
