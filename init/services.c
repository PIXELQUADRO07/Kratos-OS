/* services.c — KratosOS Init System Services & Hostname Module
 *
 * Handles setting system hostname from /etc/hostname, executing /etc/rc.sysinit,
 * and starting background scripts in /etc/rc.d/.
 */

#include "services.h"
#include <dirent.h>

static void trim_newline(char *str)
{
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ')) {
        str[--len] = '\0';
    }
}

void set_hostname(void)
{
    FILE *f = fopen("/etc/hostname", "r");
    if (f) {
        char host[128];
        if (fgets(host, sizeof(host), f)) {
            trim_newline(host);
            if (sethostname(host, strlen(host)) == 0) {
                fprintf(stderr, "[init] Hostname set to '%s'\n", host);
            } else {
                perror("[init] sethostname failed");
            }
        }
        fclose(f);
    } else {
        sethostname("kratos", 6);
    }
}

void run_sysinit(void)
{
    if (access("/etc/rc.sysinit", X_OK) == 0) {
        fprintf(stderr, "[init] Running /etc/rc.sysinit...\n");
        pid_t pid = fork();
        if (pid == 0) {
            execl("/etc/rc.sysinit", "/etc/rc.sysinit", (char *)NULL);
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

void run_services(void)
{
    DIR *d = opendir("/etc/rc.d");
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[384];
        snprintf(path, sizeof(path), "/etc/rc.d/%s", entry->d_name);

        if (access(path, X_OK) == 0) {
            fprintf(stderr, "[init] Starting service: %s\n", entry->d_name);
            pid_t pid = fork();
            if (pid == 0) {
                execl(path, path, (char *)NULL);
                _exit(127);
            }
        }
    }
    closedir(d);
}
