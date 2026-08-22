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
    /* Silenzioso in caso di successo: try_mount() stampa comunque un
     * WARNING se un mount fallisce, quindi qui non serve annunciare
     * ogni singolo passo per avere un boot pulito. */
    try_mount("proc",     "/proc",     "proc",     MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    try_mount("sysfs",    "/sys",      "sysfs",    MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    try_mount("devtmpfs", "/dev",      "devtmpfs", MS_NOSUID,                        "mode=0755");

    mkdir("/dev/pts", 0755);
    try_mount("devpts",   "/dev/pts",  "devpts",   MS_NOSUID | MS_NOEXEC,            "mode=0620,gid=5");

    mkdir("/dev/shm", 1777);
    try_mount("tmpfs",    "/dev/shm",  "tmpfs",    MS_NOSUID | MS_NODEV,             "mode=1777");

    mkdir("/run", 0755);
    try_mount("tmpfs",    "/run",      "tmpfs",    MS_NOSUID | MS_NODEV,             "mode=0755,size=64m");
    mkdir("/run/lock", 1777);
    mkdir("/run/shm", 1777);

    mkdir("/tmp", 1777);
    try_mount("tmpfs",    "/tmp",      "tmpfs",    MS_NOSUID | MS_NODEV,             "mode=1777,size=128m");
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

static unsigned long parse_mount_opts(const char *opts, char *data, size_t data_len)
{
    unsigned long flags = 0;
    if (data && data_len > 0) data[0] = '\0';
    if (!opts) return 0;

    char *copy = strdup(opts);
    char *tok = strtok(copy, ",");
    int first_data = 1;

    while (tok) {
        if (strcmp(tok, "defaults") == 0) {
            /* ignore */
        } else if (strcmp(tok, "ro") == 0)        flags |= MS_RDONLY;
        else if (strcmp(tok, "rw") == 0)        flags &= ~MS_RDONLY;
        else if (strcmp(tok, "noexec") == 0)  flags |= MS_NOEXEC;
        else if (strcmp(tok, "exec") == 0)    flags &= ~MS_NOEXEC;
        else if (strcmp(tok, "nosuid") == 0)  flags |= MS_NOSUID;
        else if (strcmp(tok, "suid") == 0)    flags &= ~MS_NOSUID;
        else if (strcmp(tok, "nodev") == 0)   flags |= MS_NODEV;
        else if (strcmp(tok, "dev") == 0)     flags &= ~MS_NODEV;
        else if (strcmp(tok, "noatime") == 0) flags |= MS_NOATIME;
        else if (strcmp(tok, "relatime") == 0) flags |= MS_RELATIME;
        else if (strcmp(tok, "bind") == 0)    flags |= MS_BIND;
        else if (strcmp(tok, "remount") == 0) flags |= MS_REMOUNT;
        else {
            /* Data option (e.g. size=64m) */
            if (data && data_len > 0) {
                if (!first_data) strncat(data, ",", data_len - strlen(data) - 1);
                strncat(data, tok, data_len - strlen(data) - 1);
                first_data = 0;
            }
        }
        tok = strtok(NULL, ",");
    }

    free(copy);
    return flags;
}

void mount_fstab(void)
{
    FILE *f = setmntent("/etc/fstab", "r");
    if (!f) {
        return;
    }

    struct mntent mnt;
    char buf[1024];
    int mounted = 0;

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

        char data[512];
        unsigned long flags = parse_mount_opts(mnt.mnt_opts, data, sizeof(data));
        const char *mount_data = (data[0] == '\0') ? NULL : data;

        mkdir(mnt.mnt_dir, 0755);
        if (mount(target_dev, mnt.mnt_dir, mnt.mnt_type, flags, mount_data) == 0) {
            mounted++;
        }
    }

    if (mounted > 0) {
        fprintf(stderr, "[init] Mounted %d filesystem(s) from /etc/fstab.\n", mounted);
    }

    endmntent(f);
}
