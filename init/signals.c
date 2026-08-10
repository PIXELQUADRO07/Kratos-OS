/* signals.c — KratosOS Init Signal Handling & Child Reaping Module */

#include "signals.h"

volatile sig_atomic_t caught_sig = 0;

static void sig_handler(int sig)
{
    caught_sig = sig;
}

void setup_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT,  &sa, NULL); /* Reboot */
    sigaction(SIGUSR1, &sa, NULL); /* Poweroff */
    sigaction(SIGUSR2, &sa, NULL); /* Halt */
    sigaction(SIGPWR,  &sa, NULL); /* Poweroff */

    sa.sa_handler = sig_handler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

void reap_zombies(void)
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
