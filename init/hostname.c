/* hostname.c — KratosOS Native Hostname Utility (/usr/bin/hostname)
 *
 * Usage:
 *   hostname             Show the current hostname
 *   hostname <name>      Set the hostname (root only)
 *   hostname -f          Show the FQDN (if /etc/hostname contains one)
 *   hostname -h          Show help
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define HOSTNAME_FILE "/etc/hostname"

static void show_help(const char *prog)
{
    printf("Usage: %s [hostname | -f | -h]\n\n", prog);
    printf("  (no args)     Show the current hostname\n");
    printf("  <name>        Set the system hostname (requires root)\n");
    printf("  -f, --fqdn    Show the fully qualified domain name\n");
    printf("  -h, --help    Show this help message\n");
}

int main(int argc, char *argv[])
{
    char buf[256];

    if (argc == 1) {
        /* Show hostname */
        if (gethostname(buf, sizeof(buf)) < 0) {
            perror("hostname: gethostname");
            return 1;
        }
        puts(buf);
        return 0;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        show_help(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "-f") == 0 || strcmp(argv[1], "--fqdn") == 0) {
        /* Try /etc/hostname first, then fallback to gethostname */
        FILE *f = fopen(HOSTNAME_FILE, "r");
        if (f) {
            if (fgets(buf, sizeof(buf), f)) {
                /* Strip trailing newline */
                char *nl = strchr(buf, '\n');
                if (nl) *nl = '\0';
                puts(buf);
                fclose(f);
                return 0;
            }
            fclose(f);
        }
        if (gethostname(buf, sizeof(buf)) < 0) {
            perror("hostname");
            return 1;
        }
        puts(buf);
        return 0;
    }

    /* Set hostname */
    if (getuid() != 0) {
        fprintf(stderr, "hostname: you must be root to change the hostname\n");
        return 1;
    }

    const char *newname = argv[1];
    if (strlen(newname) == 0 || strlen(newname) > 253) {
        fprintf(stderr, "hostname: invalid hostname length\n");
        return 1;
    }

    if (sethostname(newname, strlen(newname)) < 0) {
        perror("hostname: sethostname");
        return 1;
    }

    /* Also persist to /etc/hostname */
    FILE *f = fopen(HOSTNAME_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", newname);
        fclose(f);
    }

    return 0;
}
