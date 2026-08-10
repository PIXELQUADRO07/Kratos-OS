/* init.h — KratosOS PID 1 Init System Header
 *
 * Shared declarations and data structures for modular init system.
 */

#ifndef KRATOS_INIT_H
#define KRATOS_INIT_H

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
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

extern tty_tab_t ttys[MAX_TTYS];
extern volatile sig_atomic_t caught_sig;

/* Function prototypes */
void mount_vfs(void);
void mount_fstab(void);
void set_hostname(void);
void run_sysinit(void);
void run_services(void);
void setup_signal_handlers(void);
void reap_zombies(void);
void check_and_respawn_ttys(void);
void shutdown_system(int cmd);

#endif /* KRATOS_INIT_H */
