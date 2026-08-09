/* kratos-cli.c — User-facing KratosOS Package Manager CLI Frontend (/usr/bin/kratos)
 *
 * Frontend UX:
 *   kratos install <package.kpkg>
 *   kratos remove <package>
 *   kratos list
 *   kratos info <package>
 *   kratos search <query>
 *   kratos update / upgrade
 *
 * Delegato internamente a /usr/libexec/kratos-pkg
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void show_help(void)
{
    printf("========================================\n");
    printf("    KratosOS Package Manager CLI\n");
    printf("========================================\n");
    printf("Usage: kratos <command> [arguments]\n\n");
    printf("Commands:\n");
    printf("  install <file.kpkg>   Install a .kpkg package archive\n");
    printf("  remove  <name>        Remove an installed package\n");
    printf("  list                  List all installed packages\n");
    printf("  info    <name>        Display detailed package information\n");
    printf("  search  <query>       Search available packages\n");
    printf("  update                Update package repository database\n");
    printf("  upgrade               Upgrade installed packages\n");
    printf("  help                  Show this help screen\n\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        show_help();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0 || strcmp(cmd, "remove") == 0 ||
        strcmp(cmd, "list") == 0 || strcmp(cmd, "info") == 0)
    {
        /* Resolve backend path considering KRATOS_SYSROOT if set */
        const char *sysroot = getenv("KRATOS_SYSROOT");
        char exec_path[512];
        if (sysroot && sysroot[0] != '\0') {
            snprintf(exec_path, sizeof(exec_path), "%s/usr/libexec/kratos-pkg", sysroot);
        } else {
            snprintf(exec_path, sizeof(exec_path), "/usr/libexec/kratos-pkg");
        }

        char **new_argv = calloc(argc + 1, sizeof(char *));
        new_argv[0] = exec_path;
        for (int i = 1; i < argc; i++) {
            new_argv[i] = argv[i];
        }

        execv(exec_path, new_argv);
        execvp("kratos-pkg", new_argv);

        perror("[kratos] execv /usr/libexec/kratos-pkg failed");
        free(new_argv);
        return 1;
    } else if (strcmp(cmd, "update") == 0 || strcmp(cmd, "upgrade") == 0 || strcmp(cmd, "search") == 0) {
        printf("[kratos] Repository command '%s' is in development.\n", cmd);
        return 0;
    } else {
        fprintf(stderr, "[kratos] Unknown command '%s'. Run 'kratos help' for usage.\n", cmd);
        return 1;
    }
}
