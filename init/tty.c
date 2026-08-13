/* tty.c — KratosOS Init TTY Supervision & Login Spawning Module */

#include "tty.h"

tty_tab_t ttys[MAX_TTYS] = {
    { "/dev/tty1",    0, 1 },
    { "/dev/tty2",    0, 1 },
    { "/dev/ttyS0",   0, 1 }
};

static void set_sane_termios(int fd)
{
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        t.c_iflag |= (ICRNL | IXON);
        t.c_oflag |= (OPOST | ONLCR);
        t.c_lflag |= (ECHO | ECHOE | ECHOK | ICANON | ISIG);
        tcsetattr(fd, TCSANOW, &t);
    }
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

        setenv("TERM",  "linux", 1);
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
    for (int i = 0; i < MAX_TTYS; i++) {
        if (ttys[i].enabled && ttys[i].pid == 0) {
            if (access(ttys[i].dev, F_OK) == 0) {
                ttys[i].pid = spawn_tty_shell(ttys[i].dev);
            }
        }
    }
}
