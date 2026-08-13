/* dmesg.c — KratosOS Native Kernel Message Buffer Utility (/bin/dmesg)
 *
 * Usage:
 *   dmesg             Show all kernel messages
 *   dmesg -c          Show and clear the kernel ring buffer (root only)
 *   dmesg -n <level>  Set console log level (root only)
 *   dmesg -h          Show help
 *
 * Reads from /dev/kmsg or falls back to klogctl(SYSLOG_ACTION_READ_ALL).
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <unistd.h>

#define SYSLOG_ACTION_READ_ALL   3
#define SYSLOG_ACTION_READ_CLEAR 4
#define SYSLOG_ACTION_CONSOLE_LEVEL 8

#define KLOG_BUFSZ (1024 * 512) /* 512 KiB */

static void show_help(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -c            Show and clear the kernel ring buffer\n");
    printf("  -n <level>    Set console log level (0-7)\n");
    printf("  -h            Show this help\n");
}

int main(int argc, char *argv[])
{
    int clear = 0;
    int set_level = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            clear = 1;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            set_level = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        }
    }

    /* Set console log level */
    if (set_level >= 0) {
        if (klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, set_level) < 0) {
            perror("dmesg: klogctl(CONSOLE_LEVEL)");
            return 1;
        }
        return 0;
    }

    /* Read kernel log */
    int action = clear ? SYSLOG_ACTION_READ_CLEAR : SYSLOG_ACTION_READ_ALL;

    char *buf = malloc(KLOG_BUFSZ);
    if (!buf) { perror("dmesg: malloc"); return 1; }

    int len = klogctl(action, buf, KLOG_BUFSZ);
    if (len < 0) {
        /* Fallback: try reading /dev/kmsg directly */
        free(buf);
        int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            perror("dmesg: cannot read kernel log");
            return 1;
        }
        char line[4096];
        ssize_t n;
        while ((n = read(fd, line, sizeof(line) - 1)) > 0) {
            line[n] = '\0';
            /* Format: priority,sequence,timestamp,...;message\n */
            char *semi = strchr(line, ';');
            if (semi) {
                printf("%s", semi + 1);
            } else {
                printf("%s", line);
            }
        }
        close(fd);
        return 0;
    }

    /* Print the buffer */
    if (len > 0) {
        fwrite(buf, 1, (size_t)len, stdout);
        /* Ensure trailing newline */
        if (buf[len - 1] != '\n') putchar('\n');
    }

    free(buf);
    return 0;
}
