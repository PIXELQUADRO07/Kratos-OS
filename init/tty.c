/* tty.c — KratosOS Init TTY Supervision & Login Spawning Module */

#include "tty.h"
#include <fcntl.h>

tty_tab_t ttys[MAX_TTYS] = {
    { "/dev/tty1",    0, 1 },
    { "/dev/tty2",    0, 1 },
    { "/dev/ttyS0",   0, 1 }
};

/* Set to 1 when booted with kratos.graphical or kratos.live.
 * In that mode /dev/tty1 is reserved for the graphical session until
 * /run/kratos-graphical.pid is removed by start-live.sh on exit. */
static int graphical_boot = -1;   /* -1 = not yet probed */

#define GRAPHICAL_PID_FILE "/run/kratos-graphical.pid"

static int detect_graphical_boot(void)
{
    if (graphical_boot >= 0)
        return graphical_boot;

    FILE *f = fopen("/proc/cmdline", "r");
    if (!f) {
        graphical_boot = 0;
        return 0;
    }
    char line[1024];
    graphical_boot = 0;
    if (fgets(line, sizeof(line), f)) {
        if (strstr(line, "kratos.graphical") || strstr(line, "kratos.live"))
            graphical_boot = 1;
    }
    fclose(f);
    return graphical_boot;
}

/* Returns 1 while the graphical session is considered active:
 * PID file present AND (optionally) the PID still exists in /proc. */
static int graphical_session_active(void)
{
    int fd = open(GRAPHICAL_PID_FILE, O_RDONLY);
    if (fd < 0)
        return 0;  /* PID file gone — X has exited */

    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0)
        return 0;

    /* Verify the PID actually exists in /proc */
    pid_t pid = (pid_t)atoi(buf);
    if (pid <= 0)
        return 0;

    char procpath[64];
    snprintf(procpath, sizeof(procpath), "/proc/%d", (int)pid);
    return (access(procpath, F_OK) == 0) ? 1 : 0;
}

static void set_sane_termios(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        t.c_iflag |= (ICRNL | IXON);
        t.c_oflag |= (OPOST | ONLCR);
        t.c_lflag |= (ECHO | ECHOE | ECHOK | ICANON | ISIG);
        tcsetattr(fd, TCSANOW, &t);
    }

    /* Nessuno propaga mai una window size su questa tty. Su ttyS0
     * (seriale) il kernel non ha alcuna dimensione di default (a
     * differenza della VGA console), quindi TIOCGWINSZ torna 0x0.
     * bash/readline con winsize 0x0 sbagliano i calcoli di
     * wrap-around e riposizionamento del cursore, causando schermate
     * che vanno a capo da sole o sembrano "cancellare tutto". Diamo
     * un default sano (80x24) finché non gestiamo SIGWINCH. */
    struct winsize ws = { .ws_row = 24, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 };
    ioctl(fd, TIOCSWINSZ, &ws);
}

static int setup_tty(const char *tty_dev)
{
    if (setsid() < 0 && errno != EPERM) {
        /* Ignore EPERM if already session leader */
    }

    int fd = open(tty_dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    ioctl(fd, TIOCSCTTY, 1);

    set_sane_termios(fd);

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

        /* /dev/ttyS0 is a real serial line, not a Linux virtual console —
         * it doesn't understand the "linux" console's private escape
         * sequences. Using TERM=linux there confuses readline/ncurses
         * cursor-addressing math (stray line wraps, "clear" leaving the
         * shell seemingly invisible). vt100 is the lowest common
         * denominator every terminfo/termcap database ships, and is what
         * agetty defaults to on serial ports for the same reason. */
        int is_serial = (strncmp(tty_dev, "/dev/ttyS", 9) == 0);
        setenv("TERM",  is_serial ? "vt100" : "linux", 1);
        setenv("HOME",  "/root", 1);
        setenv("PATH",  "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("SHELL", "/bin/bash", 1);

        if (access("/bin/login", X_OK) == 0) {
            /* /bin/login prints /etc/issue itself — don't print it here too,
             * or the banner shows up twice on every boot/login. */
            execl("/bin/login", "login", (char *)NULL);
        }

        /* Fallback path: no /bin/login available, so nothing else will ever
         * print the banner — print it here before dropping into the shell. */
        print_issue();
        fflush(stdout);
        execl("/bin/bash", "bash", "--login", (char *)NULL);
        perror("[init] execl bash");
        _exit(127);
    }

    return pid;
}

void check_and_respawn_ttys(void)
{
    int gfx = detect_graphical_boot();

    for (int i = 0; i < MAX_TTYS; i++) {
        if (!ttys[i].dev || !ttys[i].enabled || ttys[i].pid != 0)
            continue;

        /* In graphical boot mode, suppress /dev/tty1 while the graphical
         * session is active.  This prevents a getty/login process from
         * racing against Xorg on VT1 (boot messages VT) before the VT
         * switch to VT7 completes, and from auto-logging into bash while
         * the user sees a seemingly-frozen console.
         *
         * /dev/tty2 and /dev/ttyS0 are always spawned; they provide an
         * emergency recovery console at any point during the graphical boot.
         *
         * As soon as start-live.sh removes /run/kratos-graphical.pid on
         * X11 exit/crash, this guard drops on the very next 1-second tick
         * and init immediately spawns a login shell on tty1. */
        if (gfx && strcmp(ttys[i].dev, "/dev/tty1") == 0) {
            if (graphical_session_active()) {
                /* Graphical session is running — keep tty1 suppressed. */
                continue;
            }
            /* PID file gone or PID dead: fall through and spawn getty. */
        }

        if (access(ttys[i].dev, F_OK) == 0) {
            ttys[i].pid = spawn_tty_shell(ttys[i].dev);
        }
    }
}

