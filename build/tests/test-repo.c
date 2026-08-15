/* test-repo.c — Test package repository client */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../../pkg/kratos-repo.h"

static int checks_passed = 0;
static int checks_failed = 0;

static void check(const char *label, int ok)
{
    printf("%-55s %s\n", label, ok ? "[PASS]" : "[FAIL]");
    if (ok) checks_passed++; else checks_failed++;
}

int main(void)
{
    printf("============================================================\n");
    printf("       KRATOSOS PACKAGE REPOSITORY CLIENT TEST SUITE        \n");
    printf("============================================================\n");

    /* Create temporary directories for sysroot testing */
    char sysroot[] = "/tmp/kratos-repo-test-XXXXXX";
    if (!mkdtemp(sysroot)) {
        perror("mkdtemp");
        return 1;
    }

    char conf_dir[512];
    char cache_dir[512];
    snprintf(conf_dir, sizeof(conf_dir), "%s/etc/kratos/repos.d", sysroot);
    snprintf(cache_dir, sizeof(cache_dir), "%s/var/lib/kratos/repo-cache/testrepo", sysroot);

    /* Create paths */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s && mkdir -p %s", conf_dir, cache_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to create test directories\n");
    }

    /* 1. Create a mock repository conf file */
    char conf_file[512];
    snprintf(conf_file, sizeof(conf_file), "%s/testrepo.conf", conf_dir);
    FILE *fconf = fopen(conf_file, "w");
    if (fconf) {
        fprintf(fconf, "[testrepo]\n");
        fprintf(fconf, "url=http://localhost:8080/repo\n");
        fprintf(fconf, "enabled=yes\n");
        fprintf(fconf, "priority=99\n");
        fclose(fconf);
    }

    /* 2. Create a mock index.json file in cache */
    char index_file[512];
    snprintf(index_file, sizeof(index_file), "%s/index.json", cache_dir);
    FILE *fidx = fopen(index_file, "w");
    if (fidx) {
        fprintf(fidx, "{\n");
        fprintf(fidx, "  \"version\": 1,\n");
        fprintf(fidx, "  \"packages\": [\n");
        fprintf(fidx, "    {\n");
        fprintf(fidx, "      \"name\": \"nano\",\n");
        fprintf(fidx, "      \"version\": \"8.0\",\n");
        fprintf(fidx, "      \"release\": 2,\n");
        fprintf(fidx, "      \"arch\": \"x86_64\",\n");
        fprintf(fidx, "      \"description\": \"GNU nano text editor\",\n");
        fprintf(fidx, "      \"sha256\": \"1a2b3c4d\",\n");
        fprintf(fidx, "      \"url\": \"packages/nano/8.0-2/nano.kpkg\",\n");
        fprintf(fidx, "      \"depends\": \"ncurses>=6.0\"\n");
        fprintf(fidx, "    },\n");
        fprintf(fidx, "    {\n");
        fprintf(fidx, "      \"name\": \"curl\",\n");
        fprintf(fidx, "      \"version\": \"8.8.0\",\n");
        fprintf(fidx, "      \"release\": 1,\n");
        fprintf(fidx, "      \"description\": \"Command line tool for transferring data\",\n");
        fprintf(fidx, "      \"sha256\": \"5e6f7g8h\",\n");
        fprintf(fidx, "      \"url\": \"packages/curl/curl.kpkg\"\n");
        fprintf(fidx, "    }\n");
        fprintf(fidx, "  ]\n");
        fprintf(fidx, "}\n");
        fclose(fidx);
    }

    /* Test load all repos */
    repo_list_t list;
    int load_res = kratos_repo_load_all(&list, sysroot);
    check("kratos_repo_load_all returns 0", load_res == 0);
    check("Loaded 1 repository", list.repo_count == 1);

    if (list.repo_count == 1) {
        repo_t *repo = &list.repos[0];
        check("Repo name is 'testrepo'", strcmp(repo->name, "testrepo") == 0);
        check("Repo URL matches", strcmp(repo->url, "http://localhost:8080/repo") == 0);
        check("Repo enabled", repo->enabled == 1);
        check("Repo priority is 99", repo->priority == 99);
        check("Repo has 2 packages loaded", repo->pkg_count == 2);

        if (repo->pkg_count == 2) {
            check("First package is 'nano'", strcmp(repo->packages[0].name, "nano") == 0);
            check("First package version is '8.0'", strcmp(repo->packages[0].version, "8.0") == 0);
            check("First package release is 2", repo->packages[0].release == 2);
            check("First package depends matches", strcmp(repo->packages[0].depends, "ncurses>=6.0") == 0);
            check("Second package is 'curl'", strcmp(repo->packages[1].name, "curl") == 0);
            check("Second package version is '8.8.0'", strcmp(repo->packages[1].version, "8.8.0") == 0);
        }
    }

    /* Test find package */
    repo_pkg_t found;
    int find_res = kratos_repo_find("nano", &found, sysroot);
    check("kratos_repo_find('nano') succeeds", find_res == 0);
    if (find_res == 0) {
        check("Found package name is 'nano'", strcmp(found.name, "nano") == 0);
        check("Found package version is '8.0'", strcmp(found.version, "8.0") == 0);
        check("Found package repo matches", strcmp(found.repo_name, "testrepo") == 0);
    }

    int find_missing = kratos_repo_find("missing-pkg", &found, sysroot);
    check("kratos_repo_find('missing-pkg') fails", find_missing != 0);

    /* Clean up memory */
    kratos_repo_free(&list);

    /* Clean up files */
    snprintf(cmd, sizeof(cmd), "rm -rf %s", sysroot);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed to clean up test directory\n");
    }

    printf("\n%d checks passed, %d failed\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
