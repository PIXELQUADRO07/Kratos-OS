/* free.c — KratosOS Native Memory Information Utility (/usr/bin/free)
 *
 * Usage:
 *   free             Show memory in kilobytes
 *   free -m          Show memory in megabytes
 *   free -g          Show memory in gigabytes
 *   free -h          Human-readable output (auto-scale)
 *   free --help      Show help
 *
 * Reads from /proc/meminfo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long get_meminfo_field(FILE *f, const char *field)
{
    char line[256];
    rewind(f);
    size_t flen = strlen(field);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, field, flen) == 0 && line[flen] == ':') {
            return strtoul(line + flen + 1, NULL, 10); /* value is in kB */
        }
    }
    return 0;
}

static void format_human(unsigned long kb, char *buf, size_t bufsz)
{
    if (kb >= 1048576) {
        snprintf(buf, bufsz, "%.1fGi", (double)kb / 1048576.0);
    } else if (kb >= 1024) {
        snprintf(buf, bufsz, "%.1fMi", (double)kb / 1024.0);
    } else {
        snprintf(buf, bufsz, "%luKi", kb);
    }
}

int main(int argc, char *argv[])
{
    int mode = 0; /* 0=kB, 1=MB, 2=GB, 3=human */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) mode = 1;
        else if (strcmp(argv[i], "-g") == 0) mode = 2;
        else if (strcmp(argv[i], "-h") == 0) mode = 3;
        else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: free [-m | -g | -h]\n");
            printf("  -m    Megabytes\n");
            printf("  -g    Gigabytes\n");
            printf("  -h    Human-readable\n");
            return 0;
        }
    }

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        perror("free: /proc/meminfo");
        return 1;
    }

    unsigned long mem_total   = get_meminfo_field(f, "MemTotal");
    unsigned long mem_free    = get_meminfo_field(f, "MemFree");
    unsigned long mem_avail   = get_meminfo_field(f, "MemAvailable");
    unsigned long buffers     = get_meminfo_field(f, "Buffers");
    unsigned long cached      = get_meminfo_field(f, "Cached");
    unsigned long slab_recl   = get_meminfo_field(f, "SReclaimable");
    unsigned long swap_total  = get_meminfo_field(f, "SwapTotal");
    unsigned long swap_free   = get_meminfo_field(f, "SwapFree");
    fclose(f);

    unsigned long mem_used    = mem_total - mem_free - buffers - cached - slab_recl;
    unsigned long buff_cache  = buffers + cached + slab_recl;
    unsigned long swap_used   = swap_total - swap_free;

    if (mode == 3) {
        /* Human-readable */
        char t[16], u[16], fr[16], sh[16], bc[16], av[16];
        char st[16], su[16], sf[16];

        format_human(mem_total, t, sizeof(t));
        format_human(mem_used, u, sizeof(u));
        format_human(mem_free, fr, sizeof(fr));
        format_human(0, sh, sizeof(sh)); /* shared not easily available */
        format_human(buff_cache, bc, sizeof(bc));
        format_human(mem_avail, av, sizeof(av));
        format_human(swap_total, st, sizeof(st));
        format_human(swap_used, su, sizeof(su));
        format_human(swap_free, sf, sizeof(sf));

        printf("              total        used        free      shared  buff/cache   available\n");
        printf("Mem:    %10s  %10s  %10s  %10s  %10s  %10s\n", t, u, fr, sh, bc, av);
        printf("Swap:   %10s  %10s  %10s\n", st, su, sf);
    } else {
        unsigned long div = 1;
        const char *unit = "Ki";
        if (mode == 1) { div = 1024; unit = "Mi"; }
        if (mode == 2) { div = 1048576; unit = "Gi"; }

        printf("              total        used        free      shared  buff/cache   available\n");
        printf("Mem:    %10lu%s %10lu%s %10lu%s %10lu%s %10lu%s %10lu%s\n",
               mem_total/div, unit, mem_used/div, unit, mem_free/div, unit,
               0UL, unit, buff_cache/div, unit, mem_avail/div, unit);
        printf("Swap:   %10lu%s %10lu%s %10lu%s\n",
               swap_total/div, unit, swap_used/div, unit, swap_free/div, unit);
    }

    return 0;
}
