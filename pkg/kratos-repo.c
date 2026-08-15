/* kratos-repo.c -- KratosOS Package Repository Client
 *
 * Implements remote repository synchronization, local index cache,
 * package search, and upgrade logic for the KPM ecosystem.
 *
 * Depends on:
 *   kratos-json.c   -- JSON parsing for index.json
 *   kratos-sha256.c -- SHA-256 integrity verification
 *   kratos-fetch    -- HTTPS download binary (invoked as subprocess)
 */

#define _GNU_SOURCE

#include "kratos-repo.h"
#include "kratos-json.h"
#include "kratos-sha256.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Internal Helpers                                                    */
/* ------------------------------------------------------------------ */

static void repo_mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* Case-insensitive substring search */
static int repo_strcasestr_match(const char *haystack, const char *needle)
{
    if (!needle || needle[0] == '\0') return 1;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i+j]) !=
                tolower((unsigned char)needle[j])) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

/* Compare two version strings. Returns <0, 0, >0 like strcmp.
 * Understands numeric segments: "2.42" > "2.9" > "2.1" */
static int repo_version_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            long va = strtol(a, (char **)&a, 10);
            long vb = strtol(b, (char **)&b, 10);
            if (va != vb) return (va > vb) ? 1 : -1;
        } else {
            if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
            a++; b++;
        }
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Repository Config Parser                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[REPO_NAME_MAX];
    char url[REPO_URL_MAX];
    int  enabled;
    int  priority;
} repo_conf_t;

/* Parse a single *.conf file. Format:
 *   [repo-name]
 *   url=https://...
 *   enabled=yes
 *   priority=100
 */
static int parse_repo_conf(const char *filepath, repo_conf_t *conf)
{
    memset(conf, 0, sizeof(*conf));
    conf->enabled  = 1;
    conf->priority = 100;

    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    char line[640];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        if (line[0] == '[') {
            /* Section header: [repo-name] */
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                snprintf(conf->name, sizeof(conf->name), "%s", line + 1);
            }
        } else if (strncmp(line, "url=", 4) == 0) {
            snprintf(conf->url, sizeof(conf->url), "%s", line + 4);
        } else if (strncmp(line, "enabled=", 8) == 0) {
            conf->enabled = (strcmp(line + 8, "yes") == 0 ||
                             strcmp(line + 8, "1")   == 0) ? 1 : 0;
        } else if (strncmp(line, "priority=", 9) == 0) {
            conf->priority = atoi(line + 9);
        }
    }
    fclose(f);
    return (conf->name[0] && conf->url[0]) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Index.json Parser                                                   */
/* ------------------------------------------------------------------ */

/* Parse index.json into an array of repo_pkg_t.
 * Expected format (version 1):
 * {
 *   "version": 1,
 *   "packages": [
 *     { "name":"bash", "version":"5.3", "release":1, "arch":"x86_64",
 *       "description":"...", "sha256":"...", "url":"packages/...",
 *       "depends":"glibc>=2.42", "size":123456, "installed_size":456789 }
 *   ]
 * }
 */
static int parse_index_json(const char *filepath,
                             const char *repo_name, const char *repo_url,
                             int repo_priority,
                             repo_pkg_t **pkgs_out, size_t *count_out)
{
    /* Read entire file */
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    rewind(f);
    if (fsz <= 0 || fsz > 64 * 1024 * 1024) { fclose(f); return -1; }

    char *buf = malloc((size_t)fsz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)fsz, f);
    buf[rd] = '\0';
    fclose(f);

    /* Parse JSON */
    kratos_json_value_t *root = kratos_json_parse(buf, rd);
    free(buf);
    if (!root || root->type != KRATOS_JSON_OBJECT) {
        kratos_json_free(root);
        return -1;
    }

    /* Find "packages" array */
    kratos_json_value_t *pkgs_arr = kratos_json_object_get(root, "packages");
    if (!pkgs_arr || pkgs_arr->type != KRATOS_JSON_ARRAY) {
        kratos_json_free(root);
        return -1;
    }

    size_t count = pkgs_arr->u.array.count;
    if (count == 0) {
        *pkgs_out  = NULL;
        *count_out = 0;
        kratos_json_free(root);
        return 0;
    }

    repo_pkg_t *pkgs = calloc(count, sizeof(repo_pkg_t));
    if (!pkgs) { kratos_json_free(root); return -1; }

    size_t valid = 0;
    for (size_t i = 0; i < count; i++) {
        kratos_json_value_t *entry = pkgs_arr->u.array.items[i];
        if (!entry || entry->type != KRATOS_JSON_OBJECT) continue;

        repo_pkg_t *p = &pkgs[valid];

        /* Required fields */
        kratos_json_value_t *v;
        v = kratos_json_object_get(entry, "name");
        if (!v || v->type != KRATOS_JSON_STRING) continue;
        snprintf(p->name, sizeof(p->name), "%s", v->u.string);

        v = kratos_json_object_get(entry, "version");
        if (!v || v->type != KRATOS_JSON_STRING) continue;
        snprintf(p->version, sizeof(p->version), "%s", v->u.string);

        /* Optional fields */
        v = kratos_json_object_get(entry, "arch");
        if (v && v->type == KRATOS_JSON_STRING)
            snprintf(p->arch, sizeof(p->arch), "%s", v->u.string);
        else
            snprintf(p->arch, sizeof(p->arch), "x86_64");

        v = kratos_json_object_get(entry, "description");
        if (v && v->type == KRATOS_JSON_STRING)
            snprintf(p->description, sizeof(p->description), "%s", v->u.string);

        v = kratos_json_object_get(entry, "sha256");
        if (v && v->type == KRATOS_JSON_STRING)
            snprintf(p->sha256, sizeof(p->sha256), "%s", v->u.string);

        v = kratos_json_object_get(entry, "url");
        if (v && v->type == KRATOS_JSON_STRING)
            snprintf(p->url, sizeof(p->url), "%s", v->u.string);

        v = kratos_json_object_get(entry, "depends");
        if (v && v->type == KRATOS_JSON_STRING)
            snprintf(p->depends, sizeof(p->depends), "%s", v->u.string);

        v = kratos_json_object_get(entry, "release");
        if (v && v->type == KRATOS_JSON_NUMBER)
            p->release = (int)v->u.number;
        else
            p->release = 1;

        v = kratos_json_object_get(entry, "size");
        if (v && v->type == KRATOS_JSON_NUMBER)
            p->size = (long)v->u.number;

        v = kratos_json_object_get(entry, "installed_size");
        if (v && v->type == KRATOS_JSON_NUMBER)
            p->installed_size = (long)v->u.number;

        /* Repo metadata */
        snprintf(p->repo_name, sizeof(p->repo_name), "%s", repo_name);
        snprintf(p->repo_url,  sizeof(p->repo_url),  "%s", repo_url);
        p->repo_priority = repo_priority;

        valid++;
    }

    kratos_json_free(root);
    *pkgs_out  = pkgs;
    *count_out = valid;
    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTPS Fetch via kratos-fetch subprocess                             */
/* ------------------------------------------------------------------ */

/* Invoke kratos-fetch <url> <output_path>
 * Returns 0 on success, -1 on error. */
static int fetch_url(const char *url, const char *out_path, const char *sysroot)
{
    char fetch_bin[PATH_MAX];
    if (sysroot && sysroot[0])
        snprintf(fetch_bin, sizeof(fetch_bin), "%s/usr/bin/kratos-fetch", sysroot);
    else
        snprintf(fetch_bin, sizeof(fetch_bin), "/usr/bin/kratos-fetch");

    /* Fallback: look in PATH */
    if (access(fetch_bin, X_OK) != 0)
        snprintf(fetch_bin, sizeof(fetch_bin), "kratos-fetch");

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        /* Child: exec kratos-fetch */
        execl(fetch_bin, "kratos-fetch", url, out_path, (char *)NULL);
        execlp("kratos-fetch", "kratos-fetch", url, out_path, (char *)NULL);
        perror("[kratos-repo] exec kratos-fetch failed");
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Public API — kratos_repo_load_all                                   */
/* ------------------------------------------------------------------ */

int kratos_repo_load_all(repo_list_t *list, const char *sysroot)
{
    memset(list, 0, sizeof(*list));
    if (!sysroot) sysroot = "";

    char conf_dir[PATH_MAX];
    snprintf(conf_dir, sizeof(conf_dir), "%s%s", sysroot, REPO_CONF_DIR);

    DIR *d = opendir(conf_dir);
    if (!d) {
        /* No repos configured yet — not an error */
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && list->repo_count < REPO_MAX_REPOS) {
        if (ent->d_name[0] == '.') continue;
        size_t nl = strlen(ent->d_name);
        if (nl < 6 || strcmp(ent->d_name + nl - 5, ".conf") != 0) continue;

        char conf_path[PATH_MAX];
        snprintf(conf_path, sizeof(conf_path), "%s/%s", conf_dir, ent->d_name);

        repo_conf_t conf;
        if (parse_repo_conf(conf_path, &conf) < 0) continue;
        if (!conf.enabled) continue;

        repo_t *repo = &list->repos[list->repo_count];
        snprintf(repo->name, sizeof(repo->name), "%s", conf.name);
        snprintf(repo->url,  sizeof(repo->url),  "%s", conf.url);
        repo->enabled  = conf.enabled;
        repo->priority = conf.priority;

        /* Load cached index if available */
        char index_path[PATH_MAX];
        snprintf(index_path, sizeof(index_path), "%s%s/%s/%s",
                 sysroot, REPO_CACHE_ROOT, conf.name, REPO_INDEX_FILE);

        if (access(index_path, R_OK) == 0) {
            parse_index_json(index_path, repo->name, repo->url, repo->priority,
                             &repo->packages, &repo->pkg_count);

            /* Read last-update timestamp */
            char stamp_path[PATH_MAX];
            snprintf(stamp_path, sizeof(stamp_path), "%s%s/%s/%s",
                     sysroot, REPO_CACHE_ROOT, conf.name, REPO_STAMP_FILE);
            FILE *sf = fopen(stamp_path, "r");
            if (sf) {
                long ts = 0;
                fscanf(sf, "%ld", &ts);
                repo->last_update = (time_t)ts;
                fclose(sf);
            }
        }

        list->repo_count++;
    }
    closedir(d);
    return 0;
}

void kratos_repo_free(repo_list_t *list)
{
    for (size_t i = 0; i < list->repo_count; i++) {
        free(list->repos[i].packages);
        list->repos[i].packages  = NULL;
        list->repos[i].pkg_count = 0;
    }
    list->repo_count = 0;
}

/* ------------------------------------------------------------------ */
/* Public API — kratos_repo_update                                     */
/* ------------------------------------------------------------------ */

int kratos_repo_update(const char *sysroot)
{
    if (!sysroot) sysroot = "";

    char conf_dir[PATH_MAX];
    snprintf(conf_dir, sizeof(conf_dir), "%s%s", sysroot, REPO_CONF_DIR);

    DIR *d = opendir(conf_dir);
    if (!d) {
        fprintf(stderr, "[kratos-repo] No repository configuration found in %s\n", conf_dir);
        fprintf(stderr, "  Add a repository config to %s%s/\n", sysroot, REPO_CONF_DIR);
        fprintf(stderr, "  Example: %s%s/00-official.conf\n", sysroot, REPO_CONF_DIR);
        return -1;
    }

    int updated = 0;
    int errors  = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        size_t nl = strlen(ent->d_name);
        if (nl < 6 || strcmp(ent->d_name + nl - 5, ".conf") != 0) continue;

        char conf_path[PATH_MAX];
        snprintf(conf_path, sizeof(conf_path), "%s/%s", conf_dir, ent->d_name);

        repo_conf_t conf;
        if (parse_repo_conf(conf_path, &conf) < 0) {
            fprintf(stderr, "[kratos-repo] Warning: Failed to parse %s — skipping\n", conf_path);
            continue;
        }
        if (!conf.enabled) {
            printf("  [~] Skipping disabled repository: %s\n", conf.name);
            continue;
        }

        printf("[kratos-repo] Updating repository: %s\n", conf.name);
        printf("  URL: %s\n", conf.url);

        /* Create cache directory for this repo */
        char cache_dir[PATH_MAX];
        snprintf(cache_dir, sizeof(cache_dir), "%s%s/%s",
                 sysroot, REPO_CACHE_ROOT, conf.name);
        repo_mkdir_p(cache_dir);

        /* Fetch index.json */
        char index_url[PATH_MAX];
        char index_path_tmp[PATH_MAX];
        char index_path[PATH_MAX];

        /* Strip trailing slash from URL */
        size_t url_len = strlen(conf.url);
        char clean_url[REPO_URL_MAX];
        snprintf(clean_url, sizeof(clean_url), "%s", conf.url);
        while (url_len > 0 && clean_url[url_len-1] == '/') clean_url[--url_len] = '\0';

        snprintf(index_url,      sizeof(index_url),      "%s/%s", clean_url, REPO_INDEX_FILE);
        snprintf(index_path_tmp, sizeof(index_path_tmp), "%s/%s.tmp", cache_dir, REPO_INDEX_FILE);
        snprintf(index_path,     sizeof(index_path),     "%s/%s", cache_dir, REPO_INDEX_FILE);

        printf("  Fetching: %s\n", index_url);
        if (fetch_url(index_url, index_path_tmp, sysroot) != 0) {
            fprintf(stderr, "  [!] Failed to fetch index for repository '%s'\n", conf.name);
            errors++;
            continue;
        }

        /* Validate that the downloaded file is valid JSON with a packages array */
        repo_pkg_t *test_pkgs = NULL;
        size_t      test_count = 0;
        if (parse_index_json(index_path_tmp, conf.name, conf.url, conf.priority,
                             &test_pkgs, &test_count) < 0) {
            fprintf(stderr, "  [!] Downloaded index.json is invalid for repository '%s'\n", conf.name);
            unlink(index_path_tmp);
            errors++;
            continue;
        }
        free(test_pkgs);

        /* Atomic replace: rename tmp -> final */
        if (rename(index_path_tmp, index_path) != 0) {
            fprintf(stderr, "  [!] Failed to install index for repository '%s': %s\n",
                    conf.name, strerror(errno));
            unlink(index_path_tmp);
            errors++;
            continue;
        }

        /* Update last-update timestamp */
        char stamp_path[PATH_MAX];
        snprintf(stamp_path, sizeof(stamp_path), "%s/%s", cache_dir, REPO_STAMP_FILE);
        FILE *sf = fopen(stamp_path, "w");
        if (sf) { fprintf(sf, "%ld\n", (long)time(NULL)); fclose(sf); }

        printf("  [OK] Repository '%s' updated: %zu packages available.\n",
               conf.name, test_count);
        updated++;
    }
    closedir(d);

    printf("\n");
    if (updated > 0)
        printf("[OK] %d repository(ies) updated successfully.\n", updated);
    if (errors > 0)
        printf("[!]  %d repository(ies) failed to update.\n", errors);
    if (updated == 0 && errors == 0)
        printf("[i]  No active repositories found. Add configs to %s%s/\n",
               sysroot, REPO_CONF_DIR);

    return (updated > 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Public API — kratos_repo_find                                       */
/* ------------------------------------------------------------------ */

int kratos_repo_find(const char *name, repo_pkg_t *out, const char *sysroot)
{
    repo_list_t list;
    if (kratos_repo_load_all(&list, sysroot) < 0) return -1;

    repo_pkg_t *best = NULL;
    int best_priority = -1;

    for (size_t r = 0; r < list.repo_count; r++) {
        repo_t *repo = &list.repos[r];
        for (size_t p = 0; p < repo->pkg_count; p++) {
            repo_pkg_t *pkg = &repo->packages[p];
            if (strcmp(pkg->name, name) != 0) continue;

            if (!best || repo->priority > best_priority ||
                (repo->priority == best_priority &&
                 repo_version_cmp(pkg->version, best->version) > 0)) {
                best = pkg;
                best_priority = repo->priority;
            }
        }
    }

    if (best) {
        memcpy(out, best, sizeof(repo_pkg_t));
        kratos_repo_free(&list);
        return 0;
    }

    kratos_repo_free(&list);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Public API — kratos_repo_search                                     */
/* ------------------------------------------------------------------ */

int kratos_repo_search(const char *query, const char *sysroot)
{
    repo_list_t list;
    if (kratos_repo_load_all(&list, sysroot) < 0) {
        fprintf(stderr, "[kratos-repo] No repository cache found. Run 'kratos update' first.\n");
        return 0;
    }

    if (list.repo_count == 0) {
        printf("[kratos-repo] No repositories configured. Add configs to %s%s/\n",
               sysroot ? sysroot : "", REPO_CONF_DIR);
        return 0;
    }

    /* Check if any repo has a loaded index */
    int any_index = 0;
    for (size_t r = 0; r < list.repo_count; r++)
        if (list.repos[r].pkg_count > 0) { any_index = 1; break; }

    if (!any_index) {
        printf("[i] Repository index not synchronized yet. Run 'kratos update' first.\n");
        kratos_repo_free(&list);
        return 0;
    }

    printf("%-24s %-12s %-10s %s\n", "PACKAGE", "VERSION", "REPO", "DESCRIPTION");
    printf("%-24s %-12s %-10s %s\n",
           "------------------------", "------------",
           "----------", "-------------------------------------------");

    int found = 0;
    for (size_t r = 0; r < list.repo_count; r++) {
        repo_t *repo = &list.repos[r];
        for (size_t p = 0; p < repo->pkg_count; p++) {
            repo_pkg_t *pkg = &repo->packages[p];
            if (!repo_strcasestr_match(pkg->name, query) &&
                !repo_strcasestr_match(pkg->description, query)) continue;

            /* Truncate description to fit 44 chars */
            char desc[45];
            snprintf(desc, sizeof(desc), "%s", pkg->description);

            printf("%-24s %-12s %-10s %s\n",
                   pkg->name, pkg->version, repo->name, desc);
            found++;
        }
    }

    if (found == 0) {
        printf("No packages found matching '%s'.\n", query ? query : "");
    } else {
        printf("\n%d package(s) found.\n", found);
    }

    kratos_repo_free(&list);
    return found;
}

/* ------------------------------------------------------------------ */
/* Public API — kratos_repo_download_pkg                               */
/* ------------------------------------------------------------------ */

const char *kratos_repo_download_pkg(const repo_pkg_t *pkg, const char *sysroot)
{
    if (!pkg || !pkg->url[0]) return NULL;
    if (!sysroot) sysroot = "";

    static char out_path[PATH_MAX];

    char pkg_cache_dir[PATH_MAX];
    snprintf(pkg_cache_dir, sizeof(pkg_cache_dir), "%s%s/%s/packages",
             sysroot, REPO_CACHE_ROOT, pkg->repo_name);
    repo_mkdir_p(pkg_cache_dir);

    /* Build filename from URL (last path component) */
    const char *fname = strrchr(pkg->url, '/');
    fname = fname ? fname + 1 : pkg->url;
    snprintf(out_path, sizeof(out_path), "%s/%s", pkg_cache_dir, fname);

    /* Already cached? Verify checksum first. */
    if (access(out_path, F_OK) == 0 && pkg->sha256[0]) {
        char computed[KRATOS_SHA256_HEX_SIZE];
        if (kratos_sha256_file(out_path, computed) == 0 &&
            strcasecmp(computed, pkg->sha256) == 0) {
            /* Cache hit: valid */
            return out_path;
        }
        /* Cached file is corrupt — re-download */
        unlink(out_path);
    }

    /* Build full download URL */
    char full_url[PATH_MAX];
    char clean_repo_url[REPO_URL_MAX];
    snprintf(clean_repo_url, sizeof(clean_repo_url), "%s", pkg->repo_url);
    size_t url_len = strlen(clean_repo_url);
    while (url_len > 0 && clean_repo_url[url_len-1] == '/') clean_repo_url[--url_len] = '\0';
    snprintf(full_url, sizeof(full_url), "%s/%s", clean_repo_url, pkg->url);

    printf("[kratos-repo] Downloading: %s (%s)\n", pkg->name, pkg->version);
    printf("  URL: %s\n", full_url);

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", out_path);

    if (fetch_url(full_url, tmp_path, sysroot) != 0) {
        fprintf(stderr, "[kratos-repo] Download failed for %s\n", pkg->name);
        unlink(tmp_path);
        return NULL;
    }

    /* Verify SHA-256 if available */
    if (pkg->sha256[0]) {
        char computed[KRATOS_SHA256_HEX_SIZE];
        if (kratos_sha256_file(tmp_path, computed) != 0 ||
            strcasecmp(computed, pkg->sha256) != 0) {
            fprintf(stderr, "[kratos-repo] SHA-256 mismatch for %s!\n", pkg->name);
            fprintf(stderr, "  Expected: %s\n  Got:      %s\n",
                    pkg->sha256, computed);
            unlink(tmp_path);
            return NULL;
        }
        printf("  [OK] SHA-256 verified.\n");
    }

    if (rename(tmp_path, out_path) != 0) {
        perror("[kratos-repo] rename");
        unlink(tmp_path);
        return NULL;
    }

    return out_path;
}

/* ------------------------------------------------------------------ */
/* Public API -- kratos_repo_upgrade                                   */
/* ------------------------------------------------------------------ */

int kratos_repo_upgrade(const char *sysroot)
{
    if (!sysroot) sysroot = "";

    /* Load all repo indexes */
    repo_list_t list;
    if (kratos_repo_load_all(&list, sysroot) < 0) {
        fprintf(stderr, "[kratos-repo] Failed to load repository indexes.\n");
        return -1;
    }

    int any_index = 0;
    for (size_t r = 0; r < list.repo_count; r++)
        if (list.repos[r].pkg_count > 0) { any_index = 1; break; }

    if (!any_index) {
        printf("[i] Repository index not synchronized. Run 'kratos update' first.\n");
        kratos_repo_free(&list);
        return 0;
    }

    /* Enumerate installed packages */
    char db_pkgs[PATH_MAX];
    snprintf(db_pkgs, sizeof(db_pkgs), "%s/var/lib/kratos/db/packages", sysroot);

    DIR *d = opendir(db_pkgs);
    if (!d) {
        printf("[i] No packages installed.\n");
        kratos_repo_free(&list);
        return 0;
    }

    int upgrades_available = 0;
    int upgrades_done      = 0;
    int upgrades_failed    = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || strstr(ent->d_name, ".hooks")) continue;

        char meta_path[PATH_MAX];
        snprintf(meta_path, sizeof(meta_path), "%s/%s", db_pkgs, ent->d_name);

        /* Read installed version from metadata */
        FILE *mf = fopen(meta_path, "r");
        if (!mf) continue;

        char installed_ver[64] = {0};
        char installed_name[128] = {0};
        char line[512];
        while (fgets(line, sizeof(line), mf)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
            if (strncmp(line, "name=", 5) == 0)
                snprintf(installed_name, sizeof(installed_name), "%s", line + 5);
            else if (strncmp(line, "version=", 8) == 0)
                snprintf(installed_ver, sizeof(installed_ver), "%s", line + 8);
        }
        fclose(mf);

        if (!installed_name[0] || !installed_ver[0]) continue;

        /* Find best available version in repos */
        repo_pkg_t best;
        memset(&best, 0, sizeof(best));
        int found = 0;

        for (size_t r = 0; r < list.repo_count; r++) {
            repo_t *repo = &list.repos[r];
            for (size_t p = 0; p < repo->pkg_count; p++) {
                repo_pkg_t *pkg = &repo->packages[p];
                if (strcmp(pkg->name, installed_name) != 0) continue;
                if (!found || repo->priority > best.repo_priority ||
                    (repo->priority == best.repo_priority &&
                     repo_version_cmp(pkg->version, best.version) > 0)) {
                    memcpy(&best, pkg, sizeof(repo_pkg_t));
                    best.repo_priority = repo->priority;
                    found = 1;
                }
            }
        }

        if (!found) continue;
        if (repo_version_cmp(best.version, installed_ver) <= 0) continue;

        /* Upgrade available */
        upgrades_available++;
        printf("[kratos-repo] Upgrade available: %s %s -> %s\n",
               installed_name, installed_ver, best.version);

        /* Download package */
        const char *kpkg_path = kratos_repo_download_pkg(&best, sysroot);
        if (!kpkg_path) {
            fprintf(stderr, "  [!] Download failed for %s — skipping.\n", installed_name);
            upgrades_failed++;
            continue;
        }

        /* Install via kratos-pkg (force upgrade) */
        char pkg_bin[PATH_MAX];
        if (sysroot[0])
            snprintf(pkg_bin, sizeof(pkg_bin), "%s/usr/libexec/kratos-pkg", sysroot);
        else
            snprintf(pkg_bin, sizeof(pkg_bin), "/usr/libexec/kratos-pkg");

        pid_t pid = fork();
        if (pid == 0) {
            execl(pkg_bin, "kratos-pkg", "install", "--force", kpkg_path, (char *)NULL);
            execlp("kratos-pkg", "kratos-pkg", "install", "--force", kpkg_path, (char *)NULL);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("  [OK] %s upgraded to %s\n", installed_name, best.version);
            upgrades_done++;
        } else {
            fprintf(stderr, "  [!] Failed to upgrade %s\n", installed_name);
            upgrades_failed++;
        }
    }
    closedir(d);
    kratos_repo_free(&list);

    printf("\n");
    if (upgrades_available == 0) {
        printf("[OK] All packages are up to date.\n");
    } else {
        printf("[i]  %d upgrade(s) available, %d done, %d failed.\n",
               upgrades_available, upgrades_done, upgrades_failed);
    }

    return (upgrades_failed > 0) ? -1 : 0;
}
