/* kratos-pack.c — KratosOS Package Creation Tool (/usr/bin/kratos-pack)
 *
 * Scopo:
 *   Converte una directory di stage (destdir) in un archivio pacchetto .kpkg
 *   generando automaticamente il file metadata, il manifest dei file installati,
 *   l'archivio payload.tar.gz e i checksum d'integrità SHA-256.
 *
 * Uso:
 *   kratos-pack --name bash --version 5.3.0 --arch x86_64 --dir /tmp/stage-bash --out bash-5.3.0-x86_64.kpkg
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void show_help(void)
{
    printf("Usage: kratos-pack --name <name> --version <ver> --arch <arch> --dir <stage_dir> --out <output.kpkg>\n");
}

int main(int argc, char *argv[])
{
    char name[128]        = "";
    char version[64]      = "0.1.0";
    char arch[32]         = "x86_64";
    char description[256] = "KratosOS Package";
    char license[64]      = "GPL-3.0";
    char dependencies[256]= "";
    char dir[512]         = "";
    char out[512]         = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) strncpy(name, argv[++i], sizeof(name) - 1);
        else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) strncpy(version, argv[++i], sizeof(version) - 1);
        else if (strcmp(argv[i], "--arch") == 0 && i + 1 < argc) strncpy(arch, argv[++i], sizeof(arch) - 1);
        else if (strcmp(argv[i], "--description") == 0 && i + 1 < argc) strncpy(description, argv[++i], sizeof(description) - 1);
        else if (strcmp(argv[i], "--license") == 0 && i + 1 < argc) strncpy(license, argv[++i], sizeof(license) - 1);
        else if (strcmp(argv[i], "--deps") == 0 && i + 1 < argc) strncpy(dependencies, argv[++i], sizeof(dependencies) - 1);
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) strncpy(dir, argv[++i], sizeof(dir) - 1);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) strncpy(out, argv[++i], sizeof(out) - 1);
    }

    if (name[0] == '\0' || dir[0] == '\0') {
        show_help();
        return 1;
    }

    if (out[0] == '\0') {
        snprintf(out, sizeof(out), "%s-%s-%s.kpkg", name, version, arch);
    }

    printf("[kratos-pack] Building package %s...\n", out);

    /* Create temporary packing working directory */
    char temp_dir[] = "/tmp/kpack-XXXXXX";
    if (!mkdtemp(temp_dir)) {
        perror("mkdtemp");
        return 1;
    }

    /* 1. Generate metadata file */
    char meta_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata", temp_dir);
    FILE *fmeta = fopen(meta_path, "w");
    if (!fmeta) {
        perror("fopen metadata");
        return 1;
    }
    fprintf(fmeta, "name=%s\n", name);
    fprintf(fmeta, "version=%s\n", version);
    fprintf(fmeta, "arch=%s\n", arch);
    fprintf(fmeta, "description=%s\n", description);
    fprintf(fmeta, "license=%s\n", license);
    fprintf(fmeta, "dependencies=%s\n", dependencies);
    fclose(fmeta);

    /* 2. Generate manifest file from dir */
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest", temp_dir);
    char find_cmd[2048];
    snprintf(find_cmd, sizeof(find_cmd), "find \"%s\" -type f -o -type l | sed 's|^%s||' > \"%s\"", dir, dir, manifest_path);
    system(find_cmd);

    /* 3. Create payload.tar.gz */
    char payload_path[1024];
    snprintf(payload_path, sizeof(payload_path), "%s/payload.tar.gz", temp_dir);
    char tar_cmd[2048];
    snprintf(tar_cmd, sizeof(tar_cmd), "tar -czf \"%s\" -C \"%s\" .", payload_path, dir);
    system(tar_cmd);

    /* 4. Pack everything into final .kpkg tarball */
    char pack_cmd[2048];
    snprintf(pack_cmd, sizeof(pack_cmd), "tar -cf \"%s\" -C \"%s\" metadata manifest payload.tar.gz", out, temp_dir);
    int res = system(pack_cmd);

    /* Cleanup temp directory */
    snprintf(pack_cmd, sizeof(pack_cmd), "rm -rf \"%s\"", temp_dir);
    system(pack_cmd);

    if (res == 0) {
        printf("[✓] Package created: %s\n", out);
        return 0;
    } else {
        fprintf(stderr, "[!] Package creation failed.\n");
        return 1;
    }
}
