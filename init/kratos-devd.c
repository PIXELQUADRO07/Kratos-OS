/* kratos-devd.c — KratosOS Device Management Daemon
 *
 * Responsabilità:
 *   1. Apri un socket Netlink (NETLINK_KOBJECT_UEVENT) per ascoltare gli uevent del kernel.
 *   2. Supporta il Coldplug (scansione /sys alla partenza ed emulazione uevent 'add').
 *   3. Applica permessi ed ownership corrette sui nodi /dev/ (/dev/null, /dev/tty*, dischi).
 *   4. Gestisce i symlink dinamici in /dev/disk/by-uuid/ e /dev/disk/by-label/.
 *   5. Esegue il modprobe automatico dei moduli hardware basandosi su MODALIAS.
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /sbin/kratos-devd kratos-devd.c
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/sysmacros.h>
#include <linux/netlink.h>

#define UEVENT_BUFFER_SIZE 8192

typedef struct {
    char action[32];
    char devpath[256];
    char subsystem[64];
    char devname[128];
    char modalias[256];
    int  major;
    int  minor;
} uevent_t;

static volatile sig_atomic_t running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ------------------------------------------------------------------ */
/* Utility Functions                                                   */
/* ------------------------------------------------------------------ */

static void parse_uevent(const char *buf, size_t len, uevent_t *ev)
{
    memset(ev, 0, sizeof(uevent_t));

    size_t pos = 0;
    while (pos < len) {
        const char *line = buf + pos;
        size_t line_len = strlen(line);

        if (line_len == 0) break;

        if (strncmp(line, "ACTION=", 7) == 0) {
            strncpy(ev->action, line + 7, sizeof(ev->action) - 1);
        } else if (strncmp(line, "DEVPATH=", 8) == 0) {
            strncpy(ev->devpath, line + 8, sizeof(ev->devpath) - 1);
        } else if (strncmp(line, "SUBSYSTEM=", 10) == 0) {
            strncpy(ev->subsystem, line + 10, sizeof(ev->subsystem) - 1);
        } else if (strncmp(line, "DEVNAME=", 8) == 0) {
            strncpy(ev->devname, line + 8, sizeof(ev->devname) - 1);
        } else if (strncmp(line, "MODALIAS=", 9) == 0) {
            strncpy(ev->modalias, line + 9, sizeof(ev->modalias) - 1);
        } else if (strncmp(line, "MAJOR=", 6) == 0) {
            ev->major = atoi(line + 6);
        } else if (strncmp(line, "MINOR=", 6) == 0) {
            ev->minor = atoi(line + 6);
        }

        pos += line_len + 1;
    }
}

/* ------------------------------------------------------------------ */
/* Device Rules & Permissions                                          */
/* ------------------------------------------------------------------ */

static void apply_device_rules(const uevent_t *ev)
{
    if (ev->devname[0] == '\0') return;

    char node_path[256];
    snprintf(node_path, sizeof(node_path), "/dev/%s", ev->devname);

    /* Default: 0600 root:root */
    mode_t mode = 0600;
    uid_t uid = 0;
    gid_t gid = 0;

    /* World-readable & writable devices */
    if (strcmp(ev->devname, "null") == 0 ||
        strcmp(ev->devname, "zero") == 0 ||
        strcmp(ev->devname, "full") == 0 ||
        strcmp(ev->devname, "random") == 0 ||
        strcmp(ev->devname, "urandom") == 0 ||
        strcmp(ev->devname, "tty") == 0 ||
        strcmp(ev->devname, "ptmx") == 0)
    {
        mode = 0666;
    }
    /* TTYs */
    else if (strncmp(ev->devname, "tty", 3) == 0 ||
             strncmp(ev->devname, "pts/", 4) == 0)
    {
        mode = 0620;
        gid = 5; /* tty group */
    }
    /* Block devices (disks, partitions) */
    else if (strcmp(ev->subsystem, "block") == 0 ||
             strncmp(ev->devname, "sd", 2) == 0 ||
             strncmp(ev->devname, "vd", 2) == 0 ||
             strncmp(ev->devname, "loop", 4) == 0 ||
             strncmp(ev->devname, "nvme", 4) == 0)
    {
        mode = 0660;
        gid = 6; /* disk group */
    }

    /* Create parent directories in /dev if needed (e.g. /dev/input/event0) */
    char parent_dir[256];
    snprintf(parent_dir, sizeof(parent_dir), "%s", node_path);
    char *slash = strrchr(parent_dir, '/');
    if (slash && slash != parent_dir) {
        *slash = '\0';
        mkdir(parent_dir, 0755);
    }

    /* If devtmpfs didn't create the node yet, create it via mknod */
    if (access(node_path, F_OK) != 0 && ev->major > 0) {
        mode_t dev_type = (strcmp(ev->subsystem, "block") == 0) ? S_IFBLK : S_IFCHR;
        mknod(node_path, mode | dev_type, makedev(ev->major, ev->minor));
    }

    chmod(node_path, mode);
    chown(node_path, uid, gid);
}

/* ------------------------------------------------------------------ */
/* Disk Symlinks (/dev/disk/by-uuid/, /dev/disk/by-label/)            */
/* ------------------------------------------------------------------ */

static void update_disk_symlinks(const uevent_t *ev)
{
    if (strcmp(ev->subsystem, "block") != 0 || ev->devname[0] == '\0') return;

    char node_path[256];
    snprintf(node_path, sizeof(node_path), "/dev/%s", ev->devname);

    if (strcmp(ev->action, "remove") == 0) {
        return;
    }

    /* Probe for UUID using blkid */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "blkid -s UUID -o value %s 2>/dev/null", node_path);

    FILE *f = popen(cmd, "r");
    if (!f) return;

    char uuid[128] = {0};
    if (fgets(uuid, sizeof(uuid), f)) {
        size_t l = strlen(uuid);
        while (l > 0 && (uuid[l-1] == '\n' || uuid[l-1] == '\r')) {
            uuid[--l] = '\0';
        }
    }
    pclose(f);

    if (uuid[0] != '\0') {
        mkdir("/dev/disk", 0755);
        mkdir("/dev/disk/by-uuid", 0755);

        char link_path[512];
        snprintf(link_path, sizeof(link_path), "/dev/disk/by-uuid/%s", uuid);

        unlink(link_path);
        symlink(node_path, link_path);
    }

    /* Probe for LABEL using blkid */
    snprintf(cmd, sizeof(cmd), "blkid -s LABEL -o value %s 2>/dev/null", node_path);
    f = popen(cmd, "r");
    if (f) {
        char label[128] = {0};
        if (fgets(label, sizeof(label), f)) {
            size_t l = strlen(label);
            while (l > 0 && (label[l-1] == '\n' || label[l-1] == '\r')) {
                label[--l] = '\0';
            }
            if (label[0] != '\0') {
                mkdir("/dev/disk", 0755);
                mkdir("/dev/disk/by-label", 0755);

                char link_path[512];
                snprintf(link_path, sizeof(link_path), "/dev/disk/by-label/%s", label);

                unlink(link_path);
                symlink(node_path, link_path);
            }
        }
        pclose(f);
    }
}

/* ------------------------------------------------------------------ */
/* Auto-Modprobe for Hardware                                          */
/* ------------------------------------------------------------------ */

static void handle_modprobe(const uevent_t *ev)
{
    if (ev->modalias[0] == '\0') return;
    if (strcmp(ev->action, "add") != 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        execl("/sbin/modprobe", "modprobe", "-q", ev->modalias, (char *)NULL);
        execl("/bin/modprobe",  "modprobe", "-q", ev->modalias, (char *)NULL);
        _exit(0);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Coldplug: Scan /sys and trigger uevents                            */
/* ------------------------------------------------------------------ */

static void coldplug_dir(const char *dirpath)
{
    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char subpath[1024];
        snprintf(subpath, sizeof(subpath), "%s/%s", dirpath, entry->d_name);

        char uevent_file[1048];
        snprintf(uevent_file, sizeof(uevent_file), "%s/uevent", subpath);

        if (access(uevent_file, W_OK) == 0) {
            int fd = open(uevent_file, O_WRONLY);
            if (fd >= 0) {
                write(fd, "add\n", 4);
                close(fd);
            }
        }

        /* Use lstat to avoid following sysfs symlinks (which can cause infinite recursion) */
        struct stat st;
        if (lstat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            coldplug_dir(subpath);
        }
    }

    closedir(d);
}

static void trigger_coldplug(void)
{
    printf("[kratos-devd] Running coldplug scan on /sys...\n");
    coldplug_dir("/sys/devices");
    printf("[kratos-devd] Coldplug scan complete.\n");
}

/* ------------------------------------------------------------------ */
/* Main Daemon Loop                                                   */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int daemonize = 0;
    int coldplug  = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--daemon") == 0 || strcmp(argv[i], "-d") == 0) {
            daemonize = 1;
        } else if (strcmp(argv[i], "--no-coldplug") == 0) {
            coldplug = 0;
        }
    }

    printf("========================================\n");
    printf("   KratosOS Device Management Daemon\n");
    printf("========================================\n");

    if (daemonize) {
        if (fork() > 0) _exit(0);
        setsid();
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* Open Netlink Socket */
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (sock < 0) {
        perror("[kratos-devd] Netlink socket failed");
        return 1;
    }

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = 1; /* Kernel Multicast Group 1 */

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("[kratos-devd] Netlink bind failed");
        close(sock);
        return 1;
    }

    if (coldplug) {
        trigger_coldplug();
    }

    printf("[kratos-devd] Listening for kernel uevents...\n");

    char buf[UEVENT_BUFFER_SIZE];
    while (running) {
        ssize_t len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len < 0) {
            if (errno == EINTR) continue;
            perror("[kratos-devd] recv failed");
            break;
        }

        buf[len] = '\0';

        uevent_t ev;
        parse_uevent(buf, len, &ev);

        if (ev.action[0] != '\0') {
            apply_device_rules(&ev);
            update_disk_symlinks(&ev);
            handle_modprobe(&ev);
        }
    }

    printf("[kratos-devd] Shutting down.\n");
    close(sock);
    return 0;
}
