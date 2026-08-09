/* shutdown.c — KratosOS shutdown/reboot/poweroff/halt utility
 *
 * Usage:
 *   reboot    [-f]
 *   poweroff  [-f]
 *   halt      [-f]
 *   shutdown  [-h|-r|-P] [now]
 *
 * Sends signals to PID 1:
 *   SIGINT  → Reboot
 *   SIGUSR1 → Poweroff
 *   SIGUSR2 → Halt
 *
 * With -f (force), executes reboot() syscall directly.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-f]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -f, --force   Force immediate shutdown/reboot without contacting init\n");
}

int main(int argc, char *argv[])
{
    int force = 0;
    int sig = SIGINT;
    int reboot_cmd = RB_AUTOBOOT;
    char *prog;

    prog = basename(argv[0]);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "-r") == 0) {
            sig = SIGINT;
            reboot_cmd = RB_AUTOBOOT;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-P") == 0) {
            sig = SIGUSR1;
            reboot_cmd = RB_POWER_OFF;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(prog);
            return 0;
        }
    }

    if (strcmp(prog, "poweroff") == 0) {
        sig = SIGUSR1;
        reboot_cmd = RB_POWER_OFF;
    } else if (strcmp(prog, "halt") == 0) {
        sig = SIGUSR2;
        reboot_cmd = RB_HALT_SYSTEM;
    } else if (strcmp(prog, "reboot") == 0) {
        sig = SIGINT;
        reboot_cmd = RB_AUTOBOOT;
    }

    if (force) {
        sync();
        printf("[shutdown] Forcing %s via reboot() syscall...\n", prog);
        if (reboot(reboot_cmd) < 0) {
            perror("[shutdown] reboot failed");
            return 1;
        }
        return 0;
    }

    /* Signal PID 1 */
    printf("[shutdown] Requesting %s from init (PID 1)...\n", prog);
    if (kill(1, sig) < 0) {
        perror("[shutdown] kill(1) failed");
        fprintf(stderr, "[shutdown] Falling back to direct reboot() syscall...\n");
        sync();
        reboot(reboot_cmd);
        return 1;
    }

    return 0;
}
