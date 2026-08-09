/* init.c — KratosOS PID 1 System Init
 *
 * Responsabilità:
 *   1. Monta i filesystem virtuali VFS (/proc, /sys, /dev, /dev/pts, /dev/shm, /run, /tmp)
 *   2. Imposta l'hostname di sistema da /etc/hostname
 *   3. Monta le voci in /etc/fstab
 *   4. Esegue lo script di avvio predefinito /etc/rc.sysinit
 *   5. Avvia gli eventuali servizi in /etc/rc.d/
 *   6. Gestisce la mietitura dei processi figli zombie (SIGCHLD handler)
 *   7. Gestisce i segnali di spegnimento e riavvio (SIGINT = reboot, SIGUSR1 = poweroff, SIGUSR2 = halt)
 *   8. Gestisce le console virtuali (TTY1, TTY2, TTYS0) e riavvia automaticamente le shell uscite.
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /sbin/init init.c
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_TTYS 3

typedef struct {
    const char *dev;
    pid_t pid;
    int enabled;
} tty_tab_t;

static tty_tab_t ttys[MAX_TTYS] = {
    { "/dev/console", 0, 1 },
    { "/dev/tty1",    0, 1 },
    { "/dev/tty2",    0, 1 }
};

static volatile sig_atomic_t caught_sig = 0;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void try_mount(const char *src, const char *tgt,
                      const char *type, unsigned long flags,
                      const char *data)
{
    if (mount(src, tgt, type, flags, data) < 0 && errno != EBUSY) {
        fprintf(stderr, "[init] WARNING: mount %s on %s failed: %s\n",
                src, tgt, strerror(errno));
    }
}

static void trim_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ')) {
        str[--len] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Signal Handlers                                                     */
/* ------------------------------------------------------------------ */

static void sig_handler(int sig)
{
    caught_sig = sig;
}

static void reap_zombies(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_TTYS; i++) {
            if (ttys[i].pid == pid) {
                ttys[i].pid = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* VFS & System Initialization                                         */
/* ------------------------------------------------------------------ */

static void mount_vfs(void)
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

static void set_hostname(void)
{
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        char host[128];
        if (fgets(host, sizeof(host), f)) {
            trim_newline(host);
            if (sethostname(host, strlen(host)) == 0) {
                fprintf(stderr, "[init] Hostname set to '%s'\n", host);
            } else {
                perror("[init] sethostname failed");
            }
        }
        fclose(f);
    } else {
        sethostname("kratos", 6);
    }
}

static void mount_fstab(void)
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
            continue; /* Già montati in mount_vfs() */
        }

        mkdir(mnt.mnt_dir, 0755);
        if (mount(mnt.mnt_fsname, mnt.mnt_dir, mnt.mnt_type, 0, mnt.mnt_opts) == 0) {
            fprintf(stderr, "[init] Mounted %s on %s (%s)\n",
                    mnt.mnt_fsname, mnt.mnt_dir, mnt.mnt_type);
        }
    }

    endmntent(f);
}

static void run_sysinit(void)
{
    if (access("/etc/rc.sysinit", X_OK) == 0) {
        fprintf(stderr, "[init] Running /etc/rc.sysinit...\n");
        pid_t pid = fork();
        if (pid == 0) {
            execl("/etc/rc.sysinit", "/etc/rc.sysinit", (char *)NULL);
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

static void run_services(void)
{
    DIR *d = opendir("/etc/rc.d");
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[384];
        snprintf(path, sizeof(path), "/etc/rc.d/%s", entry->d_name);

        if (access(path, X_OK) == 0) {
            fprintf(stderr, "[init] Starting service: %s\n", entry->d_name);
            pid_t pid = fork();
            if (pid == 0) {
                execl(path, path, (char *)NULL);
                _exit(127);
            }
        }
    }
    closedir(d);
}

/* ------------------------------------------------------------------ */
/* Shutdown / Reboot Logic                                             */
/* ------------------------------------------------------------------ */

static void shutdown_system(int cmd)
{
    const char *action_str = (cmd == RB_POWER_OFF) ? "Powering off" :
                             (cmd == (int)RB_HALT_SYSTEM) ? "Halting" : "Rebooting";

    fprintf(stderr, "\n[init] %s KratosOS...\n", action_str);

    /* 1. Invia SIGTERM a tutti i processi */
    fprintf(stderr, "[init] Sending SIGTERM to all processes...\n");
    kill(-1, SIGTERM);
    sleep(2);

    /* 2. Invia SIGKILL a tutti i processi rimanenti */
    fprintf(stderr, "[init] Sending SIGKILL to all processes...\n");
    kill(-1, SIGKILL);
    sleep(1);

    /* 3. Sincronizza i dischi */
    fprintf(stderr, "[init] Syncing filesystems...\n");
    sync();

    /* 4. Smonta tutti i filesystem */
    fprintf(stderr, "[init] Unmounting filesystems...\n");
    umount2("/dev/pts", MNT_DETACH);
    umount2("/tmp",     MNT_DETACH);
    umount2("/run",     MNT_DETACH);
    umount2("/sys",     MNT_DETACH);
    umount2("/proc",    MNT_DETACH);

    /* 5. Esegui syscall di reboot */
    reboot(cmd);

    for (;;) pause();
}

/* ------------------------------------------------------------------ */
/* TTY Setup & Shell Spawning                                          */
/* ------------------------------------------------------------------ */

static int setup_tty(const char *tty_dev)
{
    if (setsid() < 0 && errno != EPERM) {
        /* Ignora EPERM se siamo già leader di sessione */
    }

    int fd = open(tty_dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    ioctl(fd, TIOCSCTTY, 1);

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if (fd > STDERR_FILENO) {
        close(fd);
    }

    return 0;
}

static void print_issue(void)
{
    FILE *f = fopen("/etc/issue", "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof(buf), f)) {
            fputs(buf, stdout);
        }
        fclose(f);
    }
}

static pid_t spawn_tty_shell(const char *tty_dev)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("[init] fork TTY shell");
        return -1;
    }

    if (pid == 0) {
        if (setup_tty(tty_dev) < 0) {
            _exit(1);
        }

        print_issue();
        fflush(stdout);

        setenv("TERM",  "linux", 1);
        setenv("HOME",  "/root", 1);
        setenv("PATH",  "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("SHELL", "/bin/bash", 1);

        execl("/bin/bash", "bash", "--login", (char *)NULL);
        perror("[init] execl bash");
        _exit(127);
    }

    return pid;
}

/* ------------------------------------------------------------------ */
/* Main (PID 1)                                                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "       KratosOS Init System (PID 1)\n");
    fprintf(stderr, "========================================\n");

    if (getpid() != 1) {
        fprintf(stderr, "[init] WARNING: Not running as PID 1 (PID=%d)\n", getpid());
    }

    /* Configura gestori di segnali */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT,  &sa, NULL); /* Reboot */
    sigaction(SIGUSR1, &sa, NULL); /* Poweroff */
    sigaction(SIGUSR2, &sa, NULL); /* Halt */
    sigaction(SIGPWR,  &sa, NULL); /* Poweroff */

    /* Monta i VFS essenziali */
    mount_vfs();

    /* Imposta hostname */
    set_hostname();

    /* Monta i filesystem da /etc/fstab */
    mount_fstab();

    /* Esegui script di avvio sistema */
    run_sysinit();

    /* Avvia i servizi */
    run_services();

    fprintf(stderr, "[init] System startup complete. Spawning shells...\n\n");

    /* Loop principale di supervisione */
    for (;;) {
        /* Controllo segnali ricevuti */
        if (caught_sig != 0) {
            int sig = caught_sig;
            caught_sig = 0;

            if (sig == SIGINT) {
                shutdown_system(RB_AUTOBOOT);
            } else if (sig == SIGUSR1 || sig == SIGPWR) {
                shutdown_system(RB_POWER_OFF);
            } else if (sig == SIGUSR2) {
                shutdown_system(RB_HALT_SYSTEM);
            }
        }

        /* Mietitura zombie e riavvio TTY */
        reap_zombies();

        for (int i = 0; i < MAX_TTYS; i++) {
            if (ttys[i].enabled && ttys[i].pid == 0) {
                /* Avvia la shell sul TTY se la device esiste */
                if (access(ttys[i].dev, F_OK) == 0) {
                    ttys[i].pid = spawn_tty_shell(ttys[i].dev);
                }
            }
        }

        /* Attesa evento / segnale senza polling continuo CPU */
        sleep(1);
    }

    return 0;
}
