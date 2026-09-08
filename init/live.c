#define _GNU_SOURCE
#include "init.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <linux/loop.h>
#include <glob.h>

#define LIVE_SWITCHED_MARK "/run/kratos-live-switched"
#define LIVE_SQUASHFS      "/mnt/iso/live/rootfs.squashfs"
#define LIVE_NEWROOT       "/mnt/root"

static int has_boot_param(const char *param)
{
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f)
        return 0;
    char line[1024];
    int found = 0;
    if (fgets(line, sizeof(line), f)) {
        if (strstr(line, param))
            found = 1;
    }
    fclose(f);
    return found;
}

static int do_switch_root(const char *newroot)
{
    char path[256];

    snprintf(path, sizeof(path), "%s/proc", newroot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys", newroot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/dev", newroot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/run", newroot);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s/proc", newroot);
    if (mount("/proc", path, NULL, MS_MOVE, NULL) < 0)
        fprintf(stderr, "[live] warning: MS_MOVE /proc: %s\n", strerror(errno));
    snprintf(path, sizeof(path), "%s/sys", newroot);
    if (mount("/sys", path, NULL, MS_MOVE, NULL) < 0)
        fprintf(stderr, "[live] warning: MS_MOVE /sys: %s\n", strerror(errno));
    snprintf(path, sizeof(path), "%s/dev", newroot);
    if (mount("/dev", path, NULL, MS_MOVE, NULL) < 0)
        fprintf(stderr, "[live] warning: MS_MOVE /dev: %s\n", strerror(errno));
    snprintf(path, sizeof(path), "%s/run", newroot);
    if (mount("/run", path, NULL, MS_MOVE, NULL) < 0)
        fprintf(stderr, "[live] warning: MS_MOVE /run: %s\n", strerror(errno));

    if (chdir(newroot) < 0)
        return -1;
    if (mount(".", "/", NULL, MS_MOVE, NULL) < 0)
        return -1;
    if (chroot(".") < 0)
        return -1;
    if (chdir("/") < 0)
        return -1;

    return 0;
}

static int setup_loop_device(const char *file, char *loop_dev_out)
{
    int ctrl_fd = open("/dev/loop-control", O_RDWR);
    if (ctrl_fd < 0) {
        perror("[live] open /dev/loop-control failed");
        return -1;
    }

    int dev_nr = ioctl(ctrl_fd, LOOP_CTL_GET_FREE);
    close(ctrl_fd);
    if (dev_nr < 0) {
        perror("[live] LOOP_CTL_GET_FREE failed");
        return -1;
    }

    snprintf(loop_dev_out, 64, "/dev/loop%d", dev_nr);

    struct stat st;
    if (stat(loop_dev_out, &st) < 0) {
        if (mknod(loop_dev_out, S_IFBLK | 0660, makedev(7, dev_nr)) < 0) {
            perror("[live] mknod loop device failed");
            return -1;
        }
    }

    int file_fd = open(file, O_RDONLY);
    if (file_fd < 0) {
        perror("[live] open squashfs file failed");
        return -1;
    }

    /* Backing file is read-only: O_RDWR + LOOP_SET_FD often fails with EROFS. */
    int loop_fd = open(loop_dev_out, O_RDONLY);
    if (loop_fd < 0)
        loop_fd = open(loop_dev_out, O_RDWR);
    if (loop_fd < 0) {
        perror("[live] open loop device failed");
        close(file_fd);
        return -1;
    }

    if (ioctl(loop_fd, LOOP_SET_FD, file_fd) < 0) {
        perror("[live] LOOP_SET_FD failed");
        close(loop_fd);
        close(file_fd);
        return -1;
    }

    struct loop_info64 info;
    memset(&info, 0, sizeof(info));
    info.lo_flags = LO_FLAGS_READ_ONLY;
    if (ioctl(loop_fd, LOOP_SET_STATUS64, &info) < 0)
        perror("[live] LOOP_SET_STATUS64 (read-only) failed");

    close(loop_fd);
    close(file_fd);
    return 0;
}

static int try_mount_iso(void)
{
    const char *types[] = { "iso9660", "vfat", "ext4", NULL };
    const char *patterns[] = {
        "/dev/sr*",
        "/dev/sd*",
        "/dev/vd*",
        "/dev/nvme*n*",
        "/dev/mmcblk*",
        NULL
    };
    glob_t gl;
    for (int p = 0; patterns[p]; ++p) {
        if (glob(patterns[p], GLOB_NOSORT, NULL, &gl) != 0) continue;
        for (size_t i = 0; i < gl.gl_pathc; ++i) {
            const char *dev = gl.gl_pathv[i];
            if (access(dev, F_OK) != 0) continue;
            for (int t = 0; types[t]; ++t) {
                if (mount(dev, "/mnt/iso", types[t], MS_RDONLY, NULL) == 0) {
                    if (access(LIVE_SQUASHFS, F_OK) == 0) {
                        fprintf(stderr, "[live] ISO mounted from %s (%s)\n", dev, types[t]);
                        globfree(&gl);
                        return 0;
                    }
                    umount("/mnt/iso");
                }
            }
        }
        globfree(&gl);
    }
    return -1;
}

void setup_live_session(void)
{
    if (access(LIVE_SWITCHED_MARK, F_OK) == 0)
        return;

    mkdir("/proc", 0755);
    mount("proc", "/proc", "proc", 0, NULL);
    mkdir("/sys", 0755);
    mount("sysfs", "/sys", "sysfs", 0, NULL);

    if (!has_boot_param("kratos.live")) {
        umount("/sys");
        umount("/proc");
        return;
    }

    fprintf(stderr, "[live] Entering Live Session setup...\n");

    mkdir("/dev", 0755);
    mount("devtmpfs", "/dev", "devtmpfs", 0, "mode=0755,size=32m");
    mkdir("/run", 0755);
    mount("tmpfs", "/run", "tmpfs", 0, "mode=0755,size=64m");

    for (int i = 0; i < 50; i++) {
        if (access("/dev/loop-control", F_OK) == 0)
            break;
        usleep(10000);
    }

    mkdir("/mnt", 0755);
    mkdir("/mnt/iso", 0755);

    int found = 0;
    for (int attempt = 0; attempt < 15; attempt++) {
        if (try_mount_iso() == 0) {
            found = 1;
            break;
        }
        fprintf(stderr, "[live] waiting for live media... (%d/15)\n", attempt + 1);
        sleep(1);
    }

    if (!found) {
        fprintf(stderr, "[live] ERROR: Could not find KratosOS Live ISO.\n");
        return;
    }

    char loop_dev[64];
    if (setup_loop_device(LIVE_SQUASHFS, loop_dev) < 0) {
        fprintf(stderr, "[live] ERROR: Failed to setup loop device for SquashFS.\n");
        return;
    }
    fprintf(stderr, "[live] SquashFS mapped to %s\n", loop_dev);

    mkdir("/mnt/squash", 0755);
    if (mount(loop_dev, "/mnt/squash", "squashfs", MS_RDONLY, NULL) < 0) {
        perror("[live] squashfs mount failed");
        return;
    }

    mkdir("/mnt/overlay", 0755);
    if (mount("tmpfs", "/mnt/overlay", "tmpfs", 0, "size=50%") < 0) {
        perror("[live] overlay tmpfs mount failed");
        return;
    }
    mkdir("/mnt/overlay/upper", 0755);
    mkdir("/mnt/overlay/work", 0755);
    mkdir(LIVE_NEWROOT, 0755);

    char opts[512];
    snprintf(opts, sizeof(opts),
             "lowerdir=/mnt/squash,upperdir=/mnt/overlay/upper,workdir=/mnt/overlay/work");
    if (mount("overlay", LIVE_NEWROOT, "overlay", 0, opts) < 0) {
        perror("[live] overlay mount failed");
        return;
    }

    fprintf(stderr, "[live] Switching to SquashFS root...\n");
    if (do_switch_root(LIVE_NEWROOT) < 0) {
        perror("[live] switch_root failed");
        return;
    }

    mkdir("/run", 0755);
    int mark = open(LIVE_SWITCHED_MARK, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (mark >= 0)
        close(mark);

    char *args[] = { "/sbin/init", NULL };
    execv("/sbin/init", args);

    /* PID 1 must not exit: continue as init on the new root. */
    perror("[live] execv /sbin/init failed");
}
