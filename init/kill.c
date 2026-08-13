/* kill.c — KratosOS Native Signal Sending Utility (/bin/kill)
 *
 * Usage:
 *   kill [-s <signal>] <pid> [pid ...]
 *   kill -l              List available signals
 *   kill -<signal> <pid> Shorthand (e.g. kill -9 1234)
 */

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
    int         num;
    const char *name;
} sig_table[] = {
    {  1, "HUP"  }, {  2, "INT"  }, {  3, "QUIT" }, {  6, "ABRT" },
    {  9, "KILL" }, { 13, "PIPE" }, { 14, "ALRM" }, { 15, "TERM" },
    { 17, "CHLD" }, { 18, "CONT" }, { 19, "STOP" }, { 20, "TSTP" },
    { 21, "TTIN" }, { 22, "TTOU" }, { 10, "USR1" }, { 12, "USR2" },
    {  0, NULL   }
};

static int parse_signal(const char *str)
{
    /* Numeric */
    if (isdigit((unsigned char)str[0])) {
        return atoi(str);
    }
    /* Name with or without SIG prefix */
    const char *name = str;
    if (strncmp(name, "SIG", 3) == 0) name += 3;
    for (int i = 0; sig_table[i].name; i++) {
        if (strcasecmp(name, sig_table[i].name) == 0)
            return sig_table[i].num;
    }
    return -1;
}

static void list_signals(void)
{
    for (int i = 0; sig_table[i].name; i++) {
        printf("%2d) SIG%-6s", sig_table[i].num, sig_table[i].name);
        if ((i + 1) % 4 == 0) putchar('\n');
    }
    putchar('\n');
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: kill [-s signal | -signal] <pid> ...\n");
        fprintf(stderr, "       kill -l\n");
        return 1;
    }

    int sig = SIGTERM; /* default */
    int first_pid_arg = 1;

    if (strcmp(argv[1], "-l") == 0) {
        list_signals();
        return 0;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        printf("Usage: kill [-s signal | -signal] <pid> ...\n");
        printf("       kill -l    List available signals\n");
        return 0;
    }

    if (strcmp(argv[1], "-s") == 0 && argc > 2) {
        sig = parse_signal(argv[2]);
        if (sig < 0) {
            fprintf(stderr, "kill: unknown signal '%s'\n", argv[2]);
            return 1;
        }
        first_pid_arg = 3;
    } else if (argv[1][0] == '-' && (isdigit((unsigned char)argv[1][1]) || isalpha((unsigned char)argv[1][1]))) {
        sig = parse_signal(argv[1] + 1);
        if (sig < 0) {
            fprintf(stderr, "kill: unknown signal '%s'\n", argv[1] + 1);
            return 1;
        }
        first_pid_arg = 2;
    }

    if (first_pid_arg >= argc) {
        fprintf(stderr, "kill: no process ID specified\n");
        return 1;
    }

    int errors = 0;
    for (int i = first_pid_arg; i < argc; i++) {
        char *endp;
        long pid = strtol(argv[i], &endp, 10);
        if (*endp != '\0') {
            fprintf(stderr, "kill: invalid pid '%s'\n", argv[i]);
            errors++;
            continue;
        }
        if (kill((pid_t)pid, sig) < 0) {
            fprintf(stderr, "kill: (%ld): %s\n", pid, strerror(errno));
            errors++;
        }
    }

    return errors ? 1 : 0;
}
