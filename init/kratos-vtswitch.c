/* kratos-vtswitch.c — Minimal virtual terminal switcher for KratosOS
 *
 * Why this exists:
 *   Xorg normally switches the active VT to itself once it finishes
 *   initializing (unless started with -novtswitch). Doing that requires
 *   CAP_SYS_TTY_CONFIG (effectively root) or a running systemd-logind
 *   session to hand the permission out. KratosOS has neither: the live
 *   session starts Xorg as the unprivileged "kratos-live" user, and
 *   there is no logind. So Xorg's own self-switch silently never
 *   happens — X ends up running correctly but invisibly on its target
 *   VT, while the person watching the screen just sees the console
 *   frozen on whatever was last printed. Indistinguishable, from the
 *   outside, from the machine actually hanging.
 *
 *   This tool runs as root (called from start-live.sh, which is invoked
 *   from /etc/rc.d and therefore already root, BEFORE dropping to the
 *   kratos-live user) and performs the switch explicitly so X starts on
 *   a VT that is already active, instead of depending on a permission
 *   it doesn't have.
 *
 * Usage:
 *   kratos-vtswitch <vt-number>
 *
 * Requires root (or CAP_SYS_TTY_CONFIG). Exits non-zero on failure so
 * callers can detect it and fall back / report it, instead of silently
 * continuing as if the switch worked.
 *
 * Compilation:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -Wextra -std=gnu11 \
 *     -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie \
 *     -o /sbin/kratos-vtswitch kratos-vtswitch.c -Wl,-z,relro,-z,now
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <vt-number>\n", argv[0]);
        return 2;
    }

    char *end = NULL;
    long vt = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || vt < 1 || vt > 63) {
        fprintf(stderr, "[kratos-vtswitch] Invalid VT number: %s\n", argv[1]);
        return 2;
    }

    /* /dev/tty0 always refers to "the currently active VT" and accepts
     * VT_ACTIVATE for any target VT number — no need to open the target
     * device node directly, and it works even if the target VT has no
     * process attached to it yet. */
    int fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "[kratos-vtswitch] open(/dev/tty0): %s\n", strerror(errno));
        return 1;
    }

    if (ioctl(fd, VT_ACTIVATE, (int)vt) != 0) {
        fprintf(stderr, "[kratos-vtswitch] VT_ACTIVATE(%ld): %s\n", vt, strerror(errno));
        close(fd);
        return 1;
    }

    /* Blocks until the switch has actually completed, so callers can
     * rely on the VT truly being active once this returns, rather than
     * racing the switch. */
    if (ioctl(fd, VT_WAITACTIVE, (int)vt) != 0) {
        fprintf(stderr, "[kratos-vtswitch] VT_WAITACTIVE(%ld): %s\n", vt, strerror(errno));
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
