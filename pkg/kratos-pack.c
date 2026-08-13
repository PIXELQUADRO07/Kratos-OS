/* kratos-pack.c — KratosOS Package Creation Tool (/usr/bin/kratos-pack)
 *
 * Scopo:
 *   Converte una directory di stage (destdir) in un archivio pacchetto .kpkg (v2 format)
 *   generando automaticamente il file metadata, il manifest dei file installati,
 *   l'archivio payload.tar.gz e i checksum d'integrità SHA-256 senza invocare system().
 *
 * Uso:
 *   kratos-pack --name bash --version 5.3.0 --arch x86_64 --dir /tmp/stage-bash --out bash-5.3.0-x86_64.kpkg
 */

#define _GNU_SOURCE

#include "kratos-tar.h"
#include "kratos-sha256.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void show_help(void)
{
    printf("Usage: kratos-pack --name <name> --version <ver> --arch <arch> --dir <stage_dir> [options]\n\n"
           "Options:\n"
           "  --name <name>          Package name (required)\n"
           "  --version <version>    Package version (default: 0.1.0)\n"
           "  --release <release>    Package release number (default: 1)\n"
           "  --arch <arch>          Architecture (default: x86_64)\n"
           "  --description <desc>   Short package description\n"
           "  --license <license>    License identifier (default: GPL-3.0)\n"
           "  --deps <dep1,dep2>     Comma-separated dependency list (e.g. 'glibc>=2.39,ncurses')\n"
           "  --conflicts <list>     Conflicting package names\n"
           "  --provides <list>      Virtual package provisions\n"
           "  --replaces <list>      Replaced package names\n"
           "  --abi <version>        ABI compatibility version\n"
           "  --dir <stage_dir>      Root directory of package files to pack (required)\n"
           "  --out <output.kpkg>    Output file path (default: <name>-<ver>-<release>-<arch>.kpkg)\n");
}

static void build_manifest_recursive(FILE *mf, const char *base_dir, const char *rel_sub)
{
    char cur_path[PATH_MAX * 2];
    if (rel_sub && rel_sub[0] != '\0') {
        snprintf(cur_path, sizeof(cur_path), "%s/%s", base_dir, rel_sub);
    } else {
        snprintf(cur_path, sizeof(cur_path), "%s", base_dir);
    }

    DIR *d = opendir(cur_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char next_rel[PATH_MAX];
        if (rel_sub && rel_sub[0] != '\0') {
            snprintf(next_rel, sizeof(next_rel), "%s/%s", rel_sub, ent->d_name);
        } else {
            snprintf(next_rel, sizeof(next_rel), "%s", ent->d_name);
        }

        char full_fs[PATH_MAX * 2];
        snprintf(full_fs, sizeof(full_fs), "%s/%s", base_dir, next_rel);

        struct stat st;
        if (lstat(full_fs, &st) < 0) continue;

        if (S_ISDIR(st.st_mode)) {
            build_manifest_recursive(mf, base_dir, next_rel);
        } else {
            fprintf(mf, "/%s\n", next_rel);
        }
    }
    closedir(d);
}

int main(int argc, char *argv[])
{
    char name[128]        = "";
    char version[64]      = "0.1.0";
    char release[32]      = "1";
    char arch[32]         = "x86_64";
    char description[256] = "KratosOS Package";
    char license[64]      = "GPL-3.0";
    char dependencies[512]= "";
    char conflicts[256]   = "";
    char provides[256]    = "";
    char replaces[256]    = "";
    char abi[32]          = "";
    char dir[PATH_MAX]    = "";
    char out[PATH_MAX]    = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) strncpy(name, argv[++i], sizeof(name) - 1);
        else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) strncpy(version, argv[++i], sizeof(version) - 1);
        else if (strcmp(argv[i], "--release") == 0 && i + 1 < argc) strncpy(release, argv[++i], sizeof(release) - 1);
        else if (strcmp(argv[i], "--arch") == 0 && i + 1 < argc) strncpy(arch, argv[++i], sizeof(arch) - 1);
        else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc) strncpy(description, argv[++i], sizeof(description) - 1);
        else if (strcmp(argv[i], "--license") == 0 && i + 1 < argc) strncpy(license, argv[++i], sizeof(license) - 1);
        else if (strcmp(argv[i], "--deps") == 0 && i + 1 < argc) strncpy(dependencies, argv[++i], sizeof(dependencies) - 1);
        else if (strcmp(argv[i], "--conflicts") == 0 && i + 1 < argc) strncpy(conflicts, argv[++i], sizeof(conflicts) - 1);
        else if (strcmp(argv[i], "--provides") == 0 && i + 1 < argc) strncpy(provides, argv[++i], sizeof(provides) - 1);
        else if (strcmp(argv[i], "--replaces") == 0 && i + 1 < argc) strncpy(replaces, argv[++i], sizeof(replaces) - 1);
        else if (strcmp(argv[i], "--abi") == 0 && i + 1 < argc) strncpy(abi, argv[++i], sizeof(abi) - 1);
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) strncpy(dir, argv[++i], sizeof(dir) - 1);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) strncpy(out, argv[++i], sizeof(out) - 1);
    }

    if (name[0] == '\0' || dir[0] == '\0') {
        show_help();
        return 1;
    }

    if (out[0] == '\0') {
        snprintf(out, sizeof(out), "%s-%s-%s-%s.kpkg", name, version, release, arch);
    }

    printf("[kratos-pack] Building package %s...\n", out);

    /* Create temporary packing directory */
    char temp_dir[] = "/tmp/kpack-XXXXXX";
    if (!mkdtemp(temp_dir)) {
        perror("mkdtemp");
        return 1;
    }

    /* 1. Generate payload.tar.gz using safe pipe/fork/execve */
    char payload_path[PATH_MAX];
    snprintf(payload_path, sizeof(payload_path), "%s/payload.tar.gz", temp_dir);
    if (kratos_tar_create_gz(payload_path, dir) != 0) {
        fprintf(stderr, "[kratos-pack] Error: Failed to create payload.tar.gz from %s\n", dir);
        kratos_rm_rf(temp_dir);
        return 1;
    }

    /* Compute SHA-256 of payload.tar.gz */
    char payload_hash[KRATOS_SHA256_HEX_SIZE] = {0};
    kratos_sha256_file(payload_path, payload_hash);

    /* 2. Generate metadata file (Format v2) */
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata", temp_dir);
    FILE *fmeta = fopen(meta_path, "w");
    if (!fmeta) {
        perror("fopen metadata");
        kratos_rm_rf(temp_dir);
        return 1;
    }
    fprintf(fmeta, "name=%s\n", name);
    fprintf(fmeta, "version=%s\n", version);
    fprintf(fmeta, "release=%s\n", release);
    fprintf(fmeta, "arch=%s\n", arch);
    fprintf(fmeta, "description=%s\n", description);
    fprintf(fmeta, "license=%s\n", license);
    fprintf(fmeta, "dependencies=%s\n", dependencies);
    if (conflicts[0]) fprintf(fmeta, "conflicts=%s\n", conflicts);
    if (provides[0])  fprintf(fmeta, "provides=%s\n", provides);
    if (replaces[0])  fprintf(fmeta, "replaces=%s\n", replaces);
    if (abi[0])       fprintf(fmeta, "abi_version=%s\n", abi);
    fprintf(fmeta, "format_version=2\n");
    fprintf(fmeta, "sha256_payload=%s\n", payload_hash);
    fclose(fmeta);

    /* 3. Generate manifest file from dir */
    char manifest_path[PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest", temp_dir);
    FILE *fmanifest = fopen(manifest_path, "w");
    if (!fmanifest) {
        perror("fopen manifest");
        kratos_rm_rf(temp_dir);
        return 1;
    }
    build_manifest_recursive(fmanifest, dir, "");
    fclose(fmanifest);

    /* 4. Generate checksums file */
    char meta_hash[KRATOS_SHA256_HEX_SIZE] = {0};
    char manifest_hash[KRATOS_SHA256_HEX_SIZE] = {0};
    kratos_sha256_file(meta_path, meta_hash);
    kratos_sha256_file(manifest_path, manifest_hash);

    char checksums_path[PATH_MAX];
    snprintf(checksums_path, sizeof(checksums_path), "%s/checksums", temp_dir);
    FILE *fchk = fopen(checksums_path, "w");
    if (fchk) {
        fprintf(fchk, "%s metadata\n", meta_hash);
        fprintf(fchk, "%s manifest\n", manifest_hash);
        fprintf(fchk, "%s payload.tar.gz\n", payload_hash);
        fclose(fchk);
    }

    /* 5. Check for optional hooks directory in stage */
    char hooks_dir[PATH_MAX];
    snprintf(hooks_dir, sizeof(hooks_dir), "%s/hooks", dir);
    struct stat st;
    int has_hooks = (stat(hooks_dir, &st) == 0 && S_ISDIR(st.st_mode));
    if (has_hooks) {
        char temp_hooks[PATH_MAX];
        snprintf(temp_hooks, sizeof(temp_hooks), "%s/hooks", temp_dir);
        kratos_copy_tree(hooks_dir, temp_hooks);
    }

    /* 6. Pack everything into final .kpkg tar container */
    const char *files_with_hooks[] = { "metadata", "manifest", "checksums", "payload.tar.gz", "hooks" };
    const char *files_no_hooks[]   = { "metadata", "manifest", "checksums", "payload.tar.gz" };

    int pack_res = 0;
    if (has_hooks) {
        pack_res = kratos_tar_create(out, temp_dir, files_with_hooks, 5);
    } else {
        pack_res = kratos_tar_create(out, temp_dir, files_no_hooks, 4);
    }

    /* Cleanup temp directory */
    kratos_rm_rf(temp_dir);

    if (pack_res == 0) {
        printf("[✓] Package created: %s (SHA-256 payload: %.16s...)\n", out, payload_hash);
        return 0;
    } else {
        fprintf(stderr, "[!] Package creation failed.\n");
        return 1;
    }
}
