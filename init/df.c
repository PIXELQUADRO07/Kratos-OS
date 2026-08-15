/* df.c — KratosOS Native Disk Free Utility (/usr/bin/df)
 *
 * Usage:
 *   df              Show all mounted filesystems
 *   df <path>       Show filesystem containing <path>
 *   df -h           Human-readable sizes
 *   df --help       Show help
 *
 * Reads /proc/mounts and uses statvfs() for space information.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#define MTAB_FILE "/proc/mounts"

static void format_human(unsigned long long bytes, char *buf, size_t bufsz)
{
    if (bytes >= (1ULL << 40)) {
        snprintf(buf, bufsz, "%.1fT", (double)bytes / (1ULL << 40));
    } else if (bytes >= (1ULL << 30)) {
        snprintf(buf, bufsz, "%.1fG", (double)bytes / (1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(buf, bufsz, "%.1fM", (double)bytes / (1ULL << 20));
    } else if (bytes >= (1ULL << 10)) {
        snprintf(buf, bufsz, "%.1fK", (double)bytes / (1ULL << 10));
    } else {
        snprintf(buf, bufsz, "%lluB", bytes);
    }
}

static void print_fs(const char *dev, const char *mp, const char *type, int human)
{
    struct statvfs st;
    if (statvfs(mp, &st) < 0) return;

    unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
    unsigned long long avail = (unsigned long long)st.f_bavail * st.f_frsize;
    unsigned long long used  = total - (unsigned long long)st.f_bfree * st.f_frsize;
    unsigned long pct = (total > 0) ? (unsigned long)(used * 100 / total) : 0;

    if (human) {
        char t[16], u[16], a[16];
        format_human(total, t, sizeof(t));
        format_human(used, u, sizeof(u));
        format_human(avail, a, sizeof(a));
        printf("%-20s %-6s %8s %8s %8s %3lu%% %s\n",
               dev, type, t, u, a, pct, mp);
    } else {
        /* Sizes in 1K blocks */
        printf("%-20s %-6s %12llu %12llu %12llu %3lu%% %s\n",
               dev, type, total / 1024, used / 1024, avail / 1024, pct, mp);
    }
}

int main(int argc, char *argv[])
{
    int human = 0;
    const char *target = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            human = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: df [-h] [path]\n");
            printf("  -h    Human-readable sizes\n");
            return 0;
        } else {
            target = argv[i];
        }
    }

    if (target) {
        /* Show info for a single path */
        struct statvfs st;
        if (statvfs(target, &st) < 0) {
            fprintf(stderr, "df: '%s': %s\n", target, strerror(errno));
            return 1;
        }
        if (human) {
            printf("%-20s %-6s %8s %8s %8s %4s %s\n",
                   "Filesystem", "Type", "Size", "Used", "Avail", "Use%", "Mounted on");
        } else {
            printf("%-20s %-6s %12s %12s %12s %4s %s\n",
                   "Filesystem", "Type", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
        }
        /* Find the mount entry for this path */
        FILE *f = fopen(MTAB_FILE, "r");
        if (f) {
            char line[1024], best_dev[256] = "???", best_mp[256] = "/", best_type[64] = "???";
            size_t best_len = 0;
            while (fgets(line, sizeof(line), f)) {
                char dev[256], mp[256], type[64];
                if (sscanf(line, "%255s %255s %63s", dev, mp, type) >= 3) {
                    size_t mlen = strlen(mp);
                    /* Require a full path-component match: either the
                     * target equals mp exactly, or the next character in
                     * target right after the mp prefix is '/'. Without
                     * this, target="/homework/x" would wrongly match
                     * mountpoint "/home" on a plain byte-prefix test. */
                    int boundary_ok = (target[mlen] == '\0' || target[mlen] == '/' ||
                                       (mlen > 0 && mp[mlen-1] == '/'));
                    if (mlen > 0 && strncmp(target, mp, mlen) == 0 && boundary_ok && mlen > best_len) {
                        best_len = mlen;
                        strncpy(best_dev, dev, sizeof(best_dev) - 1);
                        strncpy(best_mp, mp, sizeof(best_mp) - 1);
                        strncpy(best_type, type, sizeof(best_type) - 1);
                    }
                }
            }
            fclose(f);
            print_fs(best_dev, best_mp, best_type, human);
        }
        return 0;
    }

    /* Show all mounted filesystems */
    if (human) {
        printf("%-20s %-6s %8s %8s %8s %4s %s\n",
               "Filesystem", "Type", "Size", "Used", "Avail", "Use%", "Mounted on");
    } else {
        printf("%-20s %-6s %12s %12s %12s %4s %s\n",
               "Filesystem", "Type", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
    }

    FILE *f = fopen(MTAB_FILE, "r");
    if (!f) {
        perror("df: /proc/mounts");
        return 1;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char dev[256], mp[256], type[64];
        if (sscanf(line, "%255s %255s %63s", dev, mp, type) < 3) continue;

        /* Skip pseudo-filesystems with 0 total size */
        struct statvfs st;
        if (statvfs(mp, &st) < 0) continue;
        if (st.f_blocks == 0) continue;

        print_fs(dev, mp, type, human);
    }
    fclose(f);
    return 0;
}
