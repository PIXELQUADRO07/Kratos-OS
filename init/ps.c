/* ps.c — KratosOS Native Process Status Utility (/bin/ps)
 *
 * Usage:
 *   ps              Show processes for current terminal
 *   ps -e / ps -A   Show all processes
 *   ps -f           Full format listing
 *   ps -h           Show help
 *
 * Reads /proc/<pid>/stat and /proc/<pid>/status for process information.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

struct proc_info {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    char state;
    char comm[256];
    char tty[32];
    unsigned long utime;
    unsigned long stime;
};

static int read_proc_info(pid_t pid, struct proc_info *info)
{
    char path[128];
    FILE *f;

    memset(info, 0, sizeof(*info));
    info->pid = pid;

    /* Read /proc/<pid>/stat */
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    f = fopen(path, "r");
    if (!f) return -1;

    char statline[1024];
    if (!fgets(statline, sizeof(statline), f)) { fclose(f); return -1; }
    fclose(f);

    /* Parse: pid (comm) state ppid ... */
    char *open_paren = strchr(statline, '(');
    char *close_paren = strrchr(statline, ')');
    if (!open_paren || !close_paren) return -1;

    size_t comm_len = (size_t)(close_paren - open_paren - 1);
    if (comm_len >= sizeof(info->comm)) comm_len = sizeof(info->comm) - 1;
    memcpy(info->comm, open_paren + 1, comm_len);
    info->comm[comm_len] = '\0';

    /* Fields after close_paren: state ppid pgrp session tty_nr tpgid ... utime stime */
    int tty_nr = 0;
    if (sscanf(close_paren + 2, "%c %d %*d %*d %d %*d %*u %*u %*u %*u %*u %lu %lu",
               &info->state, &info->ppid, &tty_nr,
               &info->utime, &info->stime) < 3) {
        /* Partial parse is OK, we got at least state and ppid */
    }

    /* Decode tty_nr: major 4 = tty, major 136+ = pts */
    int major = (tty_nr >> 8) & 0xff;
    int minor = tty_nr & 0xff;
    if (tty_nr == 0) {
        strcpy(info->tty, "?");
    } else if (major == 4) {
        snprintf(info->tty, sizeof(info->tty), "tty%d", minor);
    } else if (major >= 136) {
        snprintf(info->tty, sizeof(info->tty), "pts/%d", minor);
    } else {
        snprintf(info->tty, sizeof(info->tty), "%d/%d", major, minor);
    }

    /* Read UID from /proc/<pid>/status */
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Uid:", 4) == 0) {
                sscanf(line + 4, "%u", &info->uid);
                break;
            }
        }
        fclose(f);
    }

    return 0;
}

static void format_time(unsigned long ticks, char *buf, size_t bufsz)
{
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    unsigned long total_sec = ticks / (unsigned long)hz;
    unsigned long hours = total_sec / 3600;
    unsigned long mins = (total_sec % 3600) / 60;
    unsigned long secs = total_sec % 60;
    snprintf(buf, bufsz, "%02lu:%02lu:%02lu", hours, mins, secs);
}

int main(int argc, char *argv[])
{
    int show_all = 0;
    int full_format = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "-A") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            full_format = 1;
        } else if (strcmp(argv[i], "-ef") == 0 || strcmp(argv[i], "-Af") == 0) {
            show_all = 1;
            full_format = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-e|-A] [-f] [-h]\n", argv[0]);
            printf("  -e, -A  Show all processes\n");
            printf("  -f      Full format listing\n");
            printf("  -h      Show this help\n");
            return 0;
        }
    }

    DIR *proc = opendir("/proc");
    if (!proc) {
        perror("ps: cannot open /proc");
        return 1;
    }

    /* Header */
    if (full_format) {
        printf("%-10s %5s %5s %c %-8s %-16s\n",
               "USER", "PID", "PPID", 'S', "TIME", "CMD");
    } else {
        printf("%5s %-8s %8s %-16s\n", "PID", "TTY", "TIME", "CMD");
    }

    uid_t my_uid = getuid();
    struct dirent *de;

    while ((de = readdir(proc)) != NULL) {
        /* Only numeric dirs = PIDs */
        if (!isdigit((unsigned char)de->d_name[0])) continue;

        pid_t pid = (pid_t)atoi(de->d_name);
        struct proc_info info;
        if (read_proc_info(pid, &info) < 0) continue;

        /* Filter: show only our processes unless -e/-A */
        if (!show_all && info.uid != my_uid) continue;

        char timebuf[32];
        format_time(info.utime + info.stime, timebuf, sizeof(timebuf));

        if (full_format) {
            struct passwd *pw = getpwuid(info.uid);
            printf("%-10s %5d %5d %c %-8s %-16s\n",
                   pw ? pw->pw_name : "???",
                   info.pid, info.ppid, info.state,
                   timebuf, info.comm);
        } else {
            printf("%5d %-8s %8s %-16s\n",
                   info.pid, info.tty, timebuf, info.comm);
        }
    }

    closedir(proc);
    return 0;
}
