/* kratos-pkg.c — KratosOS Package Manager Backend Engine
 *
 * Responsabilità:
 *   1. Gestisce il database locale dei pacchetti (/var/lib/kratos/db/)
 *   2. Installa pacchetti .kpkg (unpack metadata, manifest, checksum, hooks, payload)
 *   3. Rimuove pacchetti (esecuzione pre/post-remove hooks, cancellazione file da manifest)
 *   4. Elenca i pacchetti installati (list) e ne mostra i dettagli (info)
 *
 * Compilazione:
 *   x86_64-kratos-linux-gnu-gcc --sysroot=$KRATOS_SYSROOT -O2 -Wall -std=gnu11 -o /usr/libexec/kratos-pkg kratos-pkg.c
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DB_ROOT       "/var/lib/kratos"
#define DB_PKGS       "/var/lib/kratos/db/packages"
#define DB_FILES      "/var/lib/kratos/db/files"
#define DB_CACHE      "/var/lib/kratos/cache"
#define DB_KEYS       "/var/lib/kratos/keys"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void ensure_dirs(const char *root_prefix)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s%s", root_prefix, DB_ROOT);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s%s", root_prefix, "/var/lib/kratos/db");
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s%s", root_prefix, DB_PKGS);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s%s", root_prefix, DB_FILES);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s%s", root_prefix, DB_CACHE);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s%s", root_prefix, DB_KEYS);
    mkdir(path, 0755);
}

static int run_cmd(const char *cmd)
{
    int res = system(cmd);
    if (WIFEXITED(res)) {
        return WEXITSTATUS(res);
    }
    return -1;
}

static void run_hook(const char *stage_dir, const char *hook_name, const char *target_root)
{
    char hook_path[PATH_MAX];
    snprintf(hook_path, sizeof(hook_path), "%s/hooks/%s", stage_dir, hook_name);

    if (access(hook_path, X_OK) == 0) {
        printf("[kratos-pkg] Running hook: %s...\n", hook_name);
        char cmd[PATH_MAX * 2];
        snprintf(cmd, sizeof(cmd), "ROOT=\"%s\" \"%s\"", target_root, hook_path);
        run_cmd(cmd);
    }
}

/* ------------------------------------------------------------------ */
/* Package Metadata Parser                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[128];
    char version[64];
    char arch[32];
    char description[256];
    char license[64];
    char dependencies[256];
} pkg_meta_t;

static int read_metadata(const char *filepath, pkg_meta_t *meta)
{
    memset(meta, 0, sizeof(pkg_meta_t));

    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';

        if (strncmp(line, "name=", 5) == 0) {
            snprintf(meta->name, sizeof(meta->name), "%.127s", line + 5);
        } else if (strncmp(line, "version=", 8) == 0) {
            snprintf(meta->version, sizeof(meta->version), "%.63s", line + 8);
        } else if (strncmp(line, "arch=", 5) == 0) {
            snprintf(meta->arch, sizeof(meta->arch), "%.31s", line + 5);
        } else if (strncmp(line, "description=", 12) == 0) {
            snprintf(meta->description, sizeof(meta->description), "%.255s", line + 12);
        } else if (strncmp(line, "license=", 8) == 0) {
            snprintf(meta->license, sizeof(meta->license), "%.63s", line + 8);
        } else if (strncmp(line, "dependencies=", 13) == 0) {
            snprintf(meta->dependencies, sizeof(meta->dependencies), "%.255s", line + 13);
        }
    }

    fclose(f);
    return (meta->name[0] != '\0') ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Package Installation                                                */
static int is_safe_path(const char *rel_path, const char *target_root)
{
    if (!rel_path || rel_path[0] == '\0') return 0;

    /* Check for direct path traversal components */
    if (strstr(rel_path, "../") != NULL || strstr(rel_path, "/..") != NULL || strcmp(rel_path, "..") == 0) {
        return 0;
    }

    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s",
             target_root[0] ? target_root : "/",
             (rel_path[0] == '/') ? rel_path + 1 : rel_path);

    char resolved_root[PATH_MAX];
    if (target_root[0] != '\0') {
        if (realpath(target_root, resolved_root) == NULL) {
            strncpy(resolved_root, target_root, sizeof(resolved_root) - 1);
            resolved_root[sizeof(resolved_root) - 1] = '\0';
        }
    } else {
        strcpy(resolved_root, "/");
    }

    size_t root_len = strlen(resolved_root);
    if (root_len > 1 && resolved_root[root_len - 1] == '/') {
        resolved_root[--root_len] = '\0';
    }

    if (strncmp(full_path, resolved_root, root_len) != 0) {
        return 0;
    }

    return 1;
}

static int install_kpkg(const char *kpkg_path, const char *target_root)
{
    printf("[kratos-pkg] Installing package: %s\n", kpkg_path);

    ensure_dirs(target_root);

    /* 1. Create temporary staging area */
    char stage_dir[] = "/tmp/kpkg-stage-XXXXXX";
    if (!mkdtemp(stage_dir)) {
        die("mkdtemp failed");
    }

    /* 2. Unpack .kpkg archive into stage_dir */
    char unpack_cmd[PATH_MAX * 2];
    snprintf(unpack_cmd, sizeof(unpack_cmd), "tar -xf \"%s\" -C \"%s\"", kpkg_path, stage_dir);
    if (run_cmd(unpack_cmd) != 0) {
        fprintf(stderr, "[kratos-pkg] Error: Failed to unpack %s\n", kpkg_path);
        char rm_cmd[PATH_MAX + 16];
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", stage_dir);
        run_cmd(rm_cmd);
        return -1;
    }

    /* 3. Read metadata */
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata", stage_dir);
    pkg_meta_t meta;
    if (read_metadata(meta_path, &meta) < 0) {
        fprintf(stderr, "[kratos-pkg] Error: Invalid package metadata in %s\n", kpkg_path);
        char rm_cmd[PATH_MAX + 16];
        snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", stage_dir);
        run_cmd(rm_cmd);
        return -1;
    }

    printf("  Package:     %s (%s)\n", meta.name, meta.version);
    printf("  Arch:        %s\n", meta.arch);
    printf("  Description: %s\n", meta.description);

    /* 4. Validate manifest against Zip-Slip path traversal */
    char manifest_path[PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest", stage_dir);
    FILE *mf = fopen(manifest_path, "r");
    if (mf) {
        char line[PATH_MAX];
        while (fgets(line, sizeof(line), mf)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
            if (line[0] == '\0') continue;

            if (!is_safe_path(line, target_root)) {
                fprintf(stderr, "[kratos-pkg] Security Error: Path traversal detected in manifest entry '%s'\n", line);
                fclose(mf);
                char rm_cmd[PATH_MAX + 16];
                snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", stage_dir);
                run_cmd(rm_cmd);
                return -1;
            }
        }
        fclose(mf);
    }

    /* 5. Run pre-install hook */
    run_hook(stage_dir, "pre-install", target_root);

    /* 6. Extract payload into target root */
    char payload_path[PATH_MAX];
    snprintf(payload_path, sizeof(payload_path), "%s/payload.tar.gz", stage_dir);

    if (access(payload_path, F_OK) == 0) {
        char extract_cmd[PATH_MAX * 2];
        snprintf(extract_cmd, sizeof(extract_cmd), "tar -xzf \"%s\" -C \"%s/\"", payload_path, target_root);
        if (run_cmd(extract_cmd) != 0) {
            fprintf(stderr, "[kratos-pkg] Error: Failed to extract payload for %s\n", meta.name);
            char rm_cmd[PATH_MAX + 16];
            snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", stage_dir);
            run_cmd(rm_cmd);
            return -1;
        }
    }

    /* 6. Run post-install hook */
    run_hook(stage_dir, "post-install", target_root);

    /* 7. Save metadata to DB */
    char db_meta_path[PATH_MAX];
    snprintf(db_meta_path, sizeof(db_meta_path), "%s%s/%s", target_root, DB_PKGS, meta.name);
    char copy_meta_cmd[PATH_MAX * 2 + 32];
    snprintf(copy_meta_cmd, sizeof(copy_meta_cmd), "cp \"%s\" \"%s\"", meta_path, db_meta_path);
    run_cmd(copy_meta_cmd);

    /* 8. Save file manifest to DB */
    char db_manifest_path[PATH_MAX];
    snprintf(db_manifest_path, sizeof(db_manifest_path), "%s%s/%s", target_root, DB_FILES, meta.name);
    char copy_manifest_cmd[PATH_MAX * 2 + 32];
    snprintf(copy_manifest_cmd, sizeof(copy_manifest_cmd), "cp \"%s\" \"%s\"", manifest_path, db_manifest_path);
    run_cmd(copy_manifest_cmd);

    /* 9. Save hooks to DB for later removal */
    char hooks_src[PATH_MAX];
    snprintf(hooks_src, sizeof(hooks_src), "%s/hooks", stage_dir);
    char hooks_dst[PATH_MAX];
    snprintf(hooks_dst, sizeof(hooks_dst), "%s%s/%s.hooks", target_root, DB_PKGS, meta.name);
    
    struct stat hooks_st;
    if (stat(hooks_src, &hooks_st) == 0 && S_ISDIR(hooks_st.st_mode)) {
        char cp_hooks[PATH_MAX * 2 + 32];
        snprintf(cp_hooks, sizeof(cp_hooks), "cp -r \"%s\" \"%s\"", hooks_src, hooks_dst);
        run_cmd(cp_hooks);
    }

    /* Clean up stage directory */
    snprintf(unpack_cmd, sizeof(unpack_cmd), "rm -rf \"%s\"", stage_dir);
    run_cmd(unpack_cmd);

    printf("[✓] Package '%s-%s' installed successfully.\n", meta.name, meta.version);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Package Removal                                                     */
/* ------------------------------------------------------------------ */

static void run_hook_from_db(const char *hooks_dir, const char *hook_name, const char *target_root)
{
    char hook_path[PATH_MAX];
    snprintf(hook_path, sizeof(hook_path), "%s/%s", hooks_dir, hook_name);

    if (access(hook_path, X_OK) == 0) {
        printf("[kratos-pkg] Running hook: %s...\n", hook_name);
        char cmd[PATH_MAX * 2];
        snprintf(cmd, sizeof(cmd), "ROOT=\"%s\" \"%s\"", target_root, hook_path);
        run_cmd(cmd);
    }
}

static int remove_pkg(const char *pkg_name, const char *target_root)
{
    printf("[kratos-pkg] Removing package: %s\n", pkg_name);

    char db_meta_path[PATH_MAX];
    snprintf(db_meta_path, sizeof(db_meta_path), "%s%s/%s", target_root, DB_PKGS, pkg_name);

    char db_manifest_path[PATH_MAX];
    snprintf(db_manifest_path, sizeof(db_manifest_path), "%s%s/%s", target_root, DB_FILES, pkg_name);

    if (access(db_meta_path, F_OK) != 0) {
        fprintf(stderr, "[kratos-pkg] Error: Package '%s' is not installed.\n", pkg_name);
        return -1;
    }

    /* Run pre-remove hook if saved */
    char hooks_dir[PATH_MAX];
    snprintf(hooks_dir, sizeof(hooks_dir), "%s%s/%s.hooks", target_root, DB_PKGS, pkg_name);
    run_hook_from_db(hooks_dir, "pre-remove", target_root);

    /* Delete files listed in manifest */
    FILE *f = fopen(db_manifest_path, "r");
    if (f) {
        char line[PATH_MAX];
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';

            if (line[0] == '\0') continue;

            char target_file[PATH_MAX];
            snprintf(target_file, sizeof(target_file), "%s/%s", target_root, (line[0] == '/') ? line + 1 : line);

            struct stat st;
            if (lstat(target_file, &st) == 0 && S_ISREG(st.st_mode)) {
                unlink(target_file);
            }
        }
        fclose(f);
    }

    /* Remove DB entries */
    unlink(db_meta_path);
    unlink(db_manifest_path);

    /* Run post-remove hook */
    run_hook_from_db(hooks_dir, "post-remove", target_root);
    
    /* Clean up saved hooks */
    char rm_hooks[PATH_MAX + 16];
    snprintf(rm_hooks, sizeof(rm_hooks), "rm -rf \"%s\"", hooks_dir);
    run_cmd(rm_hooks);

    printf("[✓] Package '%s' removed successfully.\n", pkg_name);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Package Listing & Info                                             */
/* ------------------------------------------------------------------ */

static int list_pkgs(const char *target_root)
{
    char pkgs_dir[PATH_MAX];
    snprintf(pkgs_dir, sizeof(pkgs_dir), "%s%s", target_root, DB_PKGS);

    DIR *d = opendir(pkgs_dir);
    if (!d) {
        printf("No packages installed.\n");
        return 0;
    }

    printf("%-20s %-12s %-10s %s\n", "PACKAGE", "VERSION", "ARCH", "DESCRIPTION");
    printf("--------------------------------------------------------------------------------\n");

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char meta_path[PATH_MAX + 256];
        snprintf(meta_path, sizeof(meta_path), "%s/%s", pkgs_dir, entry->d_name);

        pkg_meta_t meta;
        if (read_metadata(meta_path, &meta) == 0) {
            printf("%-20s %-12s %-10s %s\n", meta.name, meta.version, meta.arch, meta.description);
            count++;
        }
    }

    closedir(d);
    printf("--------------------------------------------------------------------------------\n");
    printf("Total installed packages: %d\n", count);
    return 0;
}

static int info_pkg(const char *pkg_name, const char *target_root)
{
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s%s/%s", target_root, DB_PKGS, pkg_name);

    pkg_meta_t meta;
    if (read_metadata(meta_path, &meta) < 0) {
        fprintf(stderr, "Package '%s' is not installed.\n", pkg_name);
        return -1;
    }

    printf("Name:         %s\n", meta.name);
    printf("Version:      %s\n", meta.version);
    printf("Architecture: %s\n", meta.arch);
    printf("License:      %s\n", meta.license);
    printf("Description:  %s\n", meta.description);
    printf("Dependencies: %s\n", meta.dependencies[0] ? meta.dependencies : "None");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <install|remove|list|info> [args...]\n", argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *target_root = getenv("KRATOS_SYSROOT");
    if (!target_root) target_root = "";

    if (strcmp(cmd, "install") == 0 && argc >= 3) {
        return install_kpkg(argv[2], target_root);
    } else if (strcmp(cmd, "remove") == 0 && argc >= 3) {
        return remove_pkg(argv[2], target_root);
    } else if (strcmp(cmd, "list") == 0) {
        return list_pkgs(target_root);
    } else if (strcmp(cmd, "info") == 0 && argc >= 3) {
        return info_pkg(argv[2], target_root);
    } else {
        fprintf(stderr, "Invalid command or arguments.\n");
        return 1;
    }
}
