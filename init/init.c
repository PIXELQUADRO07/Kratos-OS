/* init.c — KratosOS PID 1 System Init Entry Point
 *
 * Responsabilità:
 *   1. Monta i filesystem virtuali VFS (/proc, /sys, /dev, /dev/pts, /dev/shm, /run, /tmp)
 *   2. Imposta l'hostname di sistema da /etc/hostname
 *   3. Monta le voci in /etc/fstab
 *   4. Esegue lo script di avvio predefinito /etc/rc.sysinit
 *   5. Avvia gli eventuali servizi in /etc/rc.d/
 *   6. Gestisce la mietitura dei processi figli zombie (SIGCHLD handler)
 *   7. Gestisce i segnali di spegnimento e riavvio (SIGINT = reboot, SIGUSR1 = poweroff, SIGUSR2 = halt)
 *   8. Gestisce le console virtuali (TTY1, TTY2, ttyS0) e riavvia automaticamente le shell uscite.
 */

#include "init.h"
#include "mount.h"
#include "services.h"
#include "signals.h"
#include "tty.h"

void shutdown_system(int cmd)
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

static const char *kratos_build_marker = "KRATOS_DEBUG_BUILD_20260817_01";

int main(void)
{
    /* Set basic environment for init and all its children (rc.sysinit, services, shells) */
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("TERM", "linux", 0); // Default to linux, can be overridden by TTY spawn

    fprintf(stderr, "\n[init] KratosOS starting... (%s)\n", kratos_build_marker);

    if (getpid() != 1) {
        fprintf(stderr, "[init] WARNING: Not running as PID 1 (PID=%d)\n", getpid());
    }

    /* Configura gestori di segnali */
    setup_signal_handlers();

    /* Monta i VFS essenziali (/proc, /sys, /dev) */
    mount_vfs();

    /* Avvia kratos-devd ed attende il completamento del coldplug */
    start_devd();

    /* Imposta hostname */
    set_hostname();

    /* Monta i filesystem da /etc/fstab (ora /dev/disk/by-uuid e by-label sono popolati) */
    mount_fstab();

    /* Esegui script di avvio sistema */
    run_sysinit();

    /* Avvia i servizi */
    run_services();

    fprintf(stderr, "[init] Startup complete.\n\n");

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

        /* Maschera SIGCHLD durante la mietitura e il respawn delle TTY per evitare race condition */
        sigset_t chld_mask, old_mask;
        sigemptyset(&chld_mask);
        sigaddset(&chld_mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &chld_mask, &old_mask);

        /* Mietitura zombie */
        reap_zombies();

        /* Riavvio TTY uscite */
        check_and_respawn_ttys();

        sigprocmask(SIG_SETMASK, &old_mask, NULL);

        /* Attesa evento / segnale senza polling continuo CPU */
        sleep(1);
    }

    return 0;
}
