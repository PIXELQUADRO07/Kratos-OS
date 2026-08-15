/* kratos-repo.h — KratosOS Package Repository Client
 *
 * Manages remote repository index synchronization, local cache,
 * package search, and upgrade resolution for the KPM ecosystem.
 *
 * Repository layout (server-side):
 *   https://repo.kratosos.org/
 *       index.json            <- signed package index
 *       index.json.sig        <- Ed25519 signature (Milestone 3)
 *       packages/
 *           <name>/<ver>-<rel>/
 *               <name>-<ver>-<rel>-x86_64.kpkg
 *
 * Local cache layout:
 *   /var/lib/kratos/repo-cache/
 *       <repo-name>/
 *           index.json        <- cached index
 *           last-update       <- unix timestamp of last sync
 *
 * Repository config (one file per repo):
 *   /etc/kratos/repos.d/ *.conf
 *       [<repo-name>]
 *       url=https://...
 *       enabled=yes
 *       priority=100
 */

#ifndef KRATOS_REPO_H
#define KRATOS_REPO_H

#include <stddef.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

#define REPO_NAME_MAX        64
#define REPO_URL_MAX         512
#define REPO_CACHE_ROOT      "/var/lib/kratos/repo-cache"
#define REPO_CONF_DIR        "/etc/kratos/repos.d"
#define REPO_INDEX_FILE      "index.json"
#define REPO_STAMP_FILE      "last-update"

#define REPO_PKG_NAME_MAX    128
#define REPO_PKG_VER_MAX     64
#define REPO_PKG_ARCH_MAX    32
#define REPO_PKG_DESC_MAX    256
#define REPO_PKG_SHA256_MAX  65
#define REPO_PKG_URL_MAX     512
#define REPO_PKG_DEPS_MAX    512

#define REPO_MAX_REPOS       32
#define REPO_MAX_PACKAGES    4096

/* ------------------------------------------------------------------ */
/* Data Structures                                                     */
/* ------------------------------------------------------------------ */

/* A single package entry from the remote index */
typedef struct {
    char name[REPO_PKG_NAME_MAX];
    char version[REPO_PKG_VER_MAX];
    char arch[REPO_PKG_ARCH_MAX];
    char description[REPO_PKG_DESC_MAX];
    char sha256[REPO_PKG_SHA256_MAX];
    char url[REPO_PKG_URL_MAX];           /* relative path within repo */
    char depends[REPO_PKG_DEPS_MAX];
    long size;                             /* compressed .kpkg size in bytes */
    long installed_size;
    int  release;
    /* Internal: which repo this entry came from */
    char repo_name[REPO_NAME_MAX];
    char repo_url[REPO_URL_MAX];
    int  repo_priority;
} repo_pkg_t;

/* Loaded in-memory repository index */
typedef struct {
    char        name[REPO_NAME_MAX];
    char        url[REPO_URL_MAX];
    int         enabled;
    int         priority;
    repo_pkg_t *packages;
    size_t      pkg_count;
    time_t      last_update;
} repo_t;

/* Collection of all loaded repositories */
typedef struct {
    repo_t repos[REPO_MAX_REPOS];
    size_t repo_count;
} repo_list_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/*
 * kratos_repo_update() -- Synchronize all enabled repositories.
 *
 * For each repo in REPO_CONF_DIR/ *.conf:
 *   1. Fetch <url>/index.json via HTTPS (kratos-fetch)
 *   2. Validate JSON structure
 *   3. Save to REPO_CACHE_ROOT/<name>/index.json
 *   4. Update last-update timestamp
 *
 * Returns 0 on success (at least one repo updated), -1 on total failure.
 */
int kratos_repo_update(const char *sysroot);

/*
 * kratos_repo_search() -- Search all cached repository indexes for a query.
 *
 * Matches against package name and description (case-insensitive substring).
 * Results are printed to stdout in tabular form.
 *
 * Returns number of results found (0 = no match).
 */
int kratos_repo_search(const char *query, const char *sysroot);

/*
 * kratos_repo_upgrade() -- Upgrade all installed packages that have newer
 * versions available in any enabled repository.
 *
 * For each installed package, compares installed version against repo index.
 * Downloads and installs packages with newer versions.
 *
 * Returns 0 if all upgrades succeeded (or nothing to upgrade), -1 on error.
 */
int kratos_repo_upgrade(const char *sysroot);

/*
 * kratos_repo_find() -- Find a package by exact name in the repository cache.
 *
 * Fills `out` with the best matching entry (highest priority repo, newest ver).
 * Returns 0 if found, -1 if not found.
 */
int kratos_repo_find(const char *name, repo_pkg_t *out, const char *sysroot);

/*
 * kratos_repo_load_all() -- Load all cached repository indexes into memory.
 *
 * Reads from REPO_CONF_DIR/ *.conf and REPO_CACHE_ROOT/<name>/index.json.
 * Returns 0 on success.
 */
int kratos_repo_load_all(repo_list_t *list, const char *sysroot);

/*
 * kratos_repo_free() -- Free memory allocated by kratos_repo_load_all().
 */
void kratos_repo_free(repo_list_t *list);

/*
 * kratos_repo_download_pkg() -- Download a .kpkg from a repository.
 *
 * Downloads to REPO_CACHE_ROOT/<name>/packages/<pkg>.kpkg and verifies SHA-256.
 * Returns path to downloaded file (static buffer), or NULL on error.
 */
const char *kratos_repo_download_pkg(const repo_pkg_t *pkg, const char *sysroot);

#endif /* KRATOS_REPO_H */
