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

/* Helper to check if a kernel parameter exists */
static int has_boot_param(const char *param) {
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    if (fgets(line, sizeof(line), f)) {
        if (strstr(line, param)) found = 1;
    }
    fclose(f);
    return found;
}

/* Minimal switch_root implementation */
static int do_switch_root(const char *newroot) {
    /* 1. Move mounts to new root */
    mkdir("/mnt/root/proc", 0755);
    mkdir("/mnt/root/sys", 0755);
    mkdir("/mnt/root/dev", 0755);
    mkdir("/mnt/root/run", 0755);

    mount("/proc", "/mnt/root/proc", NULL, MS_MOVE, NULL);
    mount("/sys", "/mnt/root/sys", NULL, MS_MOVE, NULL);
    mount("/dev", "/mnt/root/dev", NULL, MS_MOVE, NULL);
    mount("/run", "/mnt/root/run", NULL, MS_MOVE, NULL);

    /* 2. Change root */
    if (chdir(newroot) < 0) return -1;
    if (mount(".", "/", NULL, MS_MOVE, NULL) < 0) return -1;
    if (chroot(".") < 0) return -1;
    if (chdir("/") < 0) return -1;

    return 0;
}

static int setup_loop_device(const char *file, char *loop_dev_out) {
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

    sprintf(loop_dev_out, "/dev/loop%d", dev_nr);

    /* If the device node doesn't exist, create it (major 7) */
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

    int loop_fd = open(loop_dev_out, O_RDWR);
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

    close(loop_fd);
    close(file_fd);
    return 0;
}

void setup_live_session(void) {
    /* 0. Check if we are already in the switched root */
    if (access("/init", F_OK) != 0) return;

    /* 1. Preliminary VFS mounts needed for detection */
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

    /* Give devtmpfs a moment to populate nodes */
    for (int i = 0; i < 50; i++) {
        if (access("/dev/loop-control", F_OK) == 0) break;
        usleep(10000);
    }

    /* 2. Find and mount the ISO */
    mkdir("/mnt", 0755);
    mkdir("/mnt/iso", 0755);

    const char *iso_dev = NULL;
    const char *devs[] = {"/dev/sr0", "/dev/sr1", "/dev/sda", "/dev/sdb", "/dev/sdc", NULL};
    const char *types[] = {"iso9660", "vfat", "ext4", NULL};

    for (int i = 0; devs[i]; i++) {
        for (int j = 0; types[j]; j++) {
            if (mount(devs[i], "/mnt/iso", types[j], MS_RDONLY, NULL) == 0) {
                if (access("/mnt/iso/live/rootfs.squashfs", F_OK) == 0) {
                    iso_dev = devs[i];
                    goto found;
                }
                umount("/mnt/iso");
            }
        }
    }

found:
    if (!iso_dev) {
        fprintf(stderr, "[live] ERROR: Could not find KratosOS Live ISO.\n");
        return;
    }
    fprintf(stderr, "[live] ISO mounted from %s\n", iso_dev);

    /* 3. Setup Loop Device for SquashFS */
    char loop_dev[64];
    if (setup_loop_device("/mnt/iso/live/rootfs.squashfs", loop_dev) < 0) {
        fprintf(stderr, "[live] ERROR: Failed to setup loop device for SquashFS.\n");
        return;
    }
    fprintf(stderr, "[live] SquashFS mapped to %s\n", loop_dev);

    /* 4. Mount SquashFS */
    mkdir("/mnt/squash", 0755);
    if (mount(loop_dev, "/mnt/squash", "squashfs", MS_RDONLY, NULL) < 0) {
        perror("[live] squashfs mount failed");
        return;
    }

    /* 5. Setup OverlayFS */
    mkdir("/mnt/overlay", 0755);
    mount("tmpfs", "/mnt/overlay", "tmpfs", 0, "size=50%");
    mkdir("/mnt/overlay/upper", 0755);
    mkdir("/mnt/overlay/work", 0755);
    mkdir("/mnt/root", 0755);

    char opts[512];
    snprintf(opts, sizeof(opts), "lowerdir=/mnt/squash,upperdir=/mnt/overlay/upper,workdir=/mnt/overlay/work");
    if (mount("overlay", "/mnt/root", "overlay", 0, opts) < 0) {
        perror("[live] overlay mount failed");
        return;
    }

    /* 6. Switch Root */
    fprintf(stderr, "[live] Switching to SquashFS root...\n");
    if (do_switch_root("/mnt/root") < 0) {
        perror("[live] switch_root failed");
        return;
    }

    /* 7. Exec the real init from the new root */
    char *args[] = {"/sbin/init", NULL};
    execv("/sbin/init", args);

    /* If execv returns, it's an error */
    perror("[live] execv failed");
    exit(1);
}
