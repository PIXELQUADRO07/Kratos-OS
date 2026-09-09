/* live.c — KratosOS Initramfs Live Boot Supervisor
 *
 * Responsabilità rigorosamente limitate al boot Live:
 *   1. mount_virtual_fs()  -> /proc, /sys, /dev, /run
 *   2. find_live_device()   -> individua il supporto di memorizzazione Live
 *   3. mount_live_media()   -> monta il supporto Live su /mnt/media
 *   4. mount_squashfs()     -> configura loop device e monta rootfs.squashfs su /mnt/rofs
 *   5. setup_overlay()      -> prepara OverlayFS con tmpfs (upper/work) su /mnt/newroot
 *   6. switch_root()        -> sposta i VFS ed esegue /sbin/init del sistema reale
 *
 * Niente Xorg, niente XFCE, niente DBus, niente login o TTY.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <glob.h>

#define MEDIA_MNT       "/mnt/media"
#define SQUASHFS_REL    "live/rootfs.squashfs"
#define SQUASHFS_PATH   "/mnt/media/live/rootfs.squashfs"
#define ROFS_MNT        "/mnt/rofs"
#define COW_MNT         "/mnt/cow"
#define NEWROOT_MNT     "/mnt/newroot"

static void emergency_shell(const char *msg)
{
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "[live-init] CRITICAL ERROR: %s\n", msg);
    fprintf(stderr, "[live-init] Spawning emergency recovery shell...\n");
    fprintf(stderr, "========================================\n\n");

    char *const sh_args[] = { "/bin/sh", NULL };
    char *const bash_args[] = { "/bin/bash", NULL };

    execv("/bin/sh", sh_args);
    execv("/bin/bash", bash_args);

    fprintf(stderr, "[live-init] Shell execution failed. Halting system.\n");
    for (;;) {
        sleep(10);
    }
}

static int mount_virtual_fs(void)
{
    fprintf(stderr, "[live-init] Mounting early virtual filesystems...\n");

    mkdir("/proc", 0755);
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0 && errno != EBUSY) {
        perror("[live-init] mount /proc failed");
    }

    mkdir("/sys", 0755);
    if (mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0 && errno != EBUSY) {
        perror("[live-init] mount /sys failed");
    }

    mkdir("/dev", 0755);
    if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755") < 0 && errno != EBUSY) {
        perror("[live-init] mount /dev failed");
    }

    int cfd = open("/dev/console", O_RDWR);
    if (cfd >= 0) {
        dup2(cfd, 0);
        dup2(cfd, 1);
        dup2(cfd, 2);
        if (cfd > 2) close(cfd);
    }

    mkdir("/run", 0755);
    if (mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755") < 0 && errno != EBUSY) {
        perror("[live-init] mount /run failed");
    }

    /* Wait for /dev/loop-control node if devtmpfs needs settling */
    for (int i = 0; i < 50; i++) {
        if (access("/dev/loop-control", F_OK) == 0)
            break;
        usleep(10000);
    }

    return 0;
}

static int find_live_device(char *dev_out, size_t max_len)
{
    const char *types[] = { "iso9660", "vfat", "ext4", NULL };
    const char *patterns[] = {
        "/dev/sr*",
        "/dev/sd*",
        "/dev/vd*",
        "/dev/nvme*n*",
        "/dev/mmcblk*",
        "/dev/loop*",
        NULL
    };

    mkdir("/mnt", 0755);
    mkdir(MEDIA_MNT, 0755);

    fprintf(stderr, "[live-init] Searching for KratosOS live media...\n");

    for (int attempt = 1; attempt <= 20; attempt++) {
        for (int p = 0; patterns[p]; ++p) {
            glob_t gl;
            if (glob(patterns[p], GLOB_NOSORT, NULL, &gl) != 0)
                continue;

            for (size_t i = 0; i < gl.gl_pathc; ++i) {
                const char *dev = gl.gl_pathv[i];
                if (access(dev, F_OK) != 0)
                    continue;

                for (int t = 0; types[t]; ++t) {
                    if (mount(dev, MEDIA_MNT, types[t], MS_RDONLY, NULL) == 0) {
                        if (access(SQUASHFS_PATH, F_OK) == 0) {
                            fprintf(stderr, "[live-init] Found live media: %s (%s)\n", dev, types[t]);
                            strncpy(dev_out, dev, max_len - 1);
                            dev_out[max_len - 1] = '\0';
                            globfree(&gl);
                            return 0;
                        }
                        umount(MEDIA_MNT);
                    }
                }
            }
            globfree(&gl);
        }

        if (attempt % 3 == 0) {
            fprintf(stderr, "[live-init] Still waiting for live media... (%d/20)\n", attempt);
        }
        sleep(1);
    }

    return -1;
}

static int mount_live_media(const char *dev)
{
    /* If already mounted at MEDIA_MNT and SQUASHFS_PATH exists, success */
    if (access(SQUASHFS_PATH, F_OK) == 0) {
        return 0;
    }

    const char *types[] = { "iso9660", "vfat", "ext4", NULL };
    for (int t = 0; types[t]; ++t) {
        if (mount(dev, MEDIA_MNT, types[t], MS_RDONLY, NULL) == 0) {
            if (access(SQUASHFS_PATH, F_OK) == 0) {
                return 0;
            }
            umount(MEDIA_MNT);
        }
    }
    return -1;
}

static int mount_squashfs(void)
{
    fprintf(stderr, "[live-init] Setting up SquashFS root filesystem...\n");

    int ctrl_fd = open("/dev/loop-control", O_RDWR);
    if (ctrl_fd < 0) {
        perror("[live-init] open /dev/loop-control failed");
        return -1;
    }

    int dev_nr = ioctl(ctrl_fd, LOOP_CTL_GET_FREE);
    close(ctrl_fd);
    if (dev_nr < 0) {
        perror("[live-init] LOOP_CTL_GET_FREE failed");
        return -1;
    }

    char loop_dev[64];
    snprintf(loop_dev, sizeof(loop_dev), "/dev/loop%d", dev_nr);

    struct stat st;
    if (stat(loop_dev, &st) < 0) {
        if (mknod(loop_dev, S_IFBLK | 0660, makedev(7, dev_nr)) < 0) {
            perror("[live-init] mknod loop device failed");
            return -1;
        }
    }

    int file_fd = open(SQUASHFS_PATH, O_RDONLY);
    if (file_fd < 0) {
        perror("[live-init] open squashfs file failed");
        return -1;
    }

    int loop_fd = open(loop_dev, O_RDONLY);
    if (loop_fd < 0)
        loop_fd = open(loop_dev, O_RDWR);
    if (loop_fd < 0) {
        perror("[live-init] open loop device failed");
        close(file_fd);
        return -1;
    }

    if (ioctl(loop_fd, LOOP_SET_FD, file_fd) < 0) {
        perror("[live-init] LOOP_SET_FD failed");
        close(loop_fd);
        close(file_fd);
        return -1;
    }

    struct loop_info64 info;
    memset(&info, 0, sizeof(info));
    info.lo_flags = LO_FLAGS_READ_ONLY;
    if (ioctl(loop_fd, LOOP_SET_STATUS64, &info) < 0) {
        perror("[live-init] LOOP_SET_STATUS64 failed");
    }

    close(loop_fd);
    close(file_fd);

    mkdir(ROFS_MNT, 0755);
    if (mount(loop_dev, ROFS_MNT, "squashfs", MS_RDONLY, NULL) < 0) {
        perror("[live-init] mount squashfs failed");
        return -1;
    }

    fprintf(stderr, "[live-init] SquashFS mounted on %s via %s\n", ROFS_MNT, loop_dev);
    return 0;
}

static int setup_overlay(void)
{
    fprintf(stderr, "[live-init] Initializing OverlayFS...\n");

    mkdir(COW_MNT, 0755);
    if (mount("tmpfs", COW_MNT, "tmpfs", 0, "size=75%") < 0) {
        perror("[live-init] mount cow tmpfs failed");
        return -1;
    }

    char upper_dir[256];
    char work_dir[256];
    snprintf(upper_dir, sizeof(upper_dir), "%s/upper", COW_MNT);
    snprintf(work_dir,  sizeof(work_dir),  "%s/work",  COW_MNT);

    mkdir(upper_dir, 0755);
    mkdir(work_dir,  0755);
    mkdir(NEWROOT_MNT, 0755);

    char opts[1024];
    snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s",
             ROFS_MNT, upper_dir, work_dir);

    if (mount("overlay", NEWROOT_MNT, "overlay", 0, opts) < 0) {
        perror("[live-init] mount overlay failed");
        return -1;
    }

    fprintf(stderr, "[live-init] OverlayFS mounted on %s\n", NEWROOT_MNT);
    return 0;
}

static int switch_root(const char *newroot)
{
    fprintf(stderr, "[live-init] Preparing switch_root to %s...\n", newroot);

    char path[256];

    /* Assicurati che i mount point virtuali esistano in newroot */
    snprintf(path, sizeof(path), "%s/proc", newroot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/sys", newroot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/dev", newroot);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/run", newroot);
    mkdir(path, 0755);

    /* Sposta i VFS attivi nella newroot con MS_MOVE */
    snprintf(path, sizeof(path), "%s/proc", newroot);
    if (mount("/proc", path, NULL, MS_MOVE, NULL) < 0)
        perror("[live-init] MS_MOVE /proc failed");

    snprintf(path, sizeof(path), "%s/sys", newroot);
    if (mount("/sys", path, NULL, MS_MOVE, NULL) < 0)
        perror("[live-init] MS_MOVE /sys failed");

    snprintf(path, sizeof(path), "%s/dev", newroot);
    if (mount("/dev", path, NULL, MS_MOVE, NULL) < 0)
        perror("[live-init] MS_MOVE /dev failed");

    snprintf(path, sizeof(path), "%s/run", newroot);
    if (mount("/run", path, NULL, MS_MOVE, NULL) < 0)
        perror("[live-init] MS_MOVE /run failed");

    /* Switch root standard Linux su ramfs/tmpfs */
    if (chdir(newroot) < 0) {
        perror("[live-init] chdir newroot failed");
        return -1;
    }

    if (mount(".", "/", NULL, MS_MOVE, NULL) < 0) {
        perror("[live-init] mount MS_MOVE to / failed");
        return -1;
    }

    if (chroot(".") < 0) {
        perror("[live-init] chroot failed");
        return -1;
    }

    if (chdir("/") < 0) {
        perror("[live-init] chdir / failed");
        return -1;
    }

    fprintf(stderr, "[live-init] switch_root successful. Launching /sbin/init...\n\n");

    char *const init_argv[] = { "/sbin/init", NULL };
    execv("/sbin/init", init_argv);

    perror("[live-init] execv /sbin/init failed");
    return -1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "   KRATOSOS LIVE INITRAMFS BOOTSTRAP\n");
    fprintf(stderr, "   Build: %s %s\n", __DATE__, __TIME__);
    fprintf(stderr, "========================================\n");

    mount_virtual_fs();

    char live_device[256] = {0};
    if (find_live_device(live_device, sizeof(live_device)) < 0) {
        emergency_shell("Live media device not found!");
    }

    if (mount_live_media(live_device) < 0) {
        emergency_shell("Failed to mount live media!");
    }

    if (mount_squashfs() < 0) {
        emergency_shell("Failed to mount SquashFS image!");
    }

    if (setup_overlay() < 0) {
        emergency_shell("Failed to initialize OverlayFS!");
    }

    if (switch_root(NEWROOT_MNT) < 0) {
        emergency_shell("Failed to switch_root to live system!");
    }

    emergency_shell("Unreachable code reached in initramfs main!");
    return 1;
}
