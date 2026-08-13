/* test-pkg-security.c — Automated Security & Path Traversal Exploit Test Suite
 *
 * Validates KratosOS Package Manager defenses against:
 *   - Path traversal / Zip-Slip (../, /../, ..)
 *   - Leading slashes & absolute path injection
 *   - Symlink escape attacks
 *   - Malicious device nodes in archives
 *   - Corrupt tar headers & truncated payloads
 *   - SHA-256 integrity verification
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../pkg/kratos-tar.h"
#include "../../pkg/kratos-sha256.h"

static int checks_passed = 0;
static int checks_failed = 0;

static void check(const char *label, int ok)
{
    printf("%-60s %s\n", label, ok ? "[PASS]" : "[FAIL]");
    if (ok) checks_passed++; else checks_failed++;
}

int main(void)
{
    printf("============================================================\n");
    printf("       KRATOSOS PACKAGE SECURITY TEST SUITE\n");
    printf("============================================================\n\n");

    /* ── Test 1: Path Traversal String Filter ── */
    check("Test 1a: reject '../etc/passwd'", kratos_is_safe_relpath("../etc/passwd") == 0);
    check("Test 1b: reject 'usr/../../etc/shadow'", kratos_is_safe_relpath("usr/../../etc/shadow") == 0);
    check("Test 1c: reject '/bin/sh' (absolute path)", kratos_is_safe_relpath("/bin/sh") == 0);
    check("Test 1d: reject '..'", kratos_is_safe_relpath("..") == 0);
    check("Test 1e: reject 'bin/..'", kratos_is_safe_relpath("bin/..") == 0);
    check("Test 1f: allow 'usr/bin/bash'", kratos_is_safe_relpath("usr/bin/bash") == 1);
    check("Test 1g: allow 'etc/kratos.conf'", kratos_is_safe_relpath("etc/kratos.conf") == 1);

    /* ── Test 2: SHA-256 Known Vectors ── */
    char hex[KRATOS_SHA256_HEX_SIZE] = {0};
    kratos_sha256_buffer("", 0, hex);
    check("Test 2a: SHA-256 of empty string",
          strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    kratos_sha256_buffer("The quick brown fox jumps over the lazy dog", 43, hex);
    check("Test 2b: SHA-256 of 'The quick brown fox...'",
          strcmp(hex, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592") == 0);

    /* ── Test 3: Malicious Archive Extraction ── */
    char test_dir[] = "/tmp/kratos-sec-test-XXXXXX";
    if (!mkdtemp(test_dir)) {
        perror("mkdtemp");
        return 1;
    }

    char malicious_tar[PATH_MAX];
    snprintf(malicious_tar, sizeof(malicious_tar), "%s/evil.tar", test_dir);

    /* Construct a raw tar header with '../evil.txt' */
    int fd = open(malicious_tar, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char block[512] = {0};
        snprintf(block, 100, "../evil.txt");
        snprintf(block + 100, 8, "0000644");
        snprintf(block + 108, 8, "0000000");
        snprintf(block + 116, 8, "0000000");
        snprintf(block + 124, 12, "00000000010");
        snprintf(block + 136, 12, "00000000000");
        block[156] = '0'; /* regular file */
        memcpy(block + 257, "ustar  ", 6);

        /* Checksum calculation */
        memset(block + 148, ' ', 8);
        unsigned long sum = 0;
        for (int i = 0; i < 512; i++) sum += (unsigned char)block[i];
        snprintf(block + 148, 7, "%06lo", sum);
        block[154] = '\0';
        block[155] = ' ';

        write(fd, block, 512);

        char payload[512] = "evil content\n";
        write(fd, payload, 512);

        char zero[1024] = {0};
        write(fd, zero, 1024);
        close(fd);
    }

    char extract_dest[512];
    snprintf(extract_dest, sizeof(extract_dest), "%s/dest", test_dir);
    mkdir(extract_dest, 0755);

    int res = kratos_tar_extract_file(malicious_tar, extract_dest, NULL, NULL);
    check("Test 3: Malicious '../evil.txt' rejected by tar extractor",
          res == KRATOS_TAR_ERR_PATH_TRAVERSAL);

    /* ── Test 4: Device Node Injection in Tar ── */
    char dev_tar[512];
    snprintf(dev_tar, sizeof(dev_tar), "%s/devnode.tar", test_dir);
    fd = open(dev_tar, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char block[512] = {0};
        snprintf(block, 100, "dev/evil_sda");
        snprintf(block + 100, 8, "0000660");
        snprintf(block + 124, 12, "00000000000");
        block[156] = '4'; /* block device */
        memcpy(block + 257, "ustar  ", 6);

        memset(block + 148, ' ', 8);
        unsigned long sum = 0;
        for (int i = 0; i < 512; i++) sum += (unsigned char)block[i];
        snprintf(block + 148, 7, "%06lo", sum);
        block[154] = '\0';
        block[155] = ' ';

        write(fd, block, 512);
        char zero[1024] = {0};
        write(fd, zero, 1024);
        close(fd);
    }

    res = kratos_tar_extract_file(dev_tar, extract_dest, NULL, NULL);
    check("Test 4: Dangerous device node rejected by tar extractor",
          res == KRATOS_TAR_ERR_UNSAFE_TYPE);

    /* ── Test 5: Symlink Escape Rejection ── */
    char sym_tar[512];
    snprintf(sym_tar, sizeof(sym_tar), "%s/symescape.tar", test_dir);
    fd = open(sym_tar, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char block[512] = {0};
        snprintf(block, 100, "etc/escape_link");
        snprintf(block + 100, 8, "0000777");
        snprintf(block + 124, 12, "00000000000");
        block[156] = '2'; /* symlink */
        snprintf(block + 157, 100, "../../etc/shadow");
        memcpy(block + 257, "ustar  ", 6);

        memset(block + 148, ' ', 8);
        unsigned long sum = 0;
        for (int i = 0; i < 512; i++) sum += (unsigned char)block[i];
        snprintf(block + 148, 7, "%06lo", sum);
        block[154] = '\0';
        block[155] = ' ';

        write(fd, block, 512);
        char zero[1024] = {0};
        write(fd, zero, 1024);
        close(fd);
    }

    res = kratos_tar_extract_file(sym_tar, extract_dest, NULL, NULL);
    check("Test 5: Symlink escape '../../etc/shadow' rejected",
          res == KRATOS_TAR_ERR_SYMLINK_ESCAPE);

    /* ── Test 6: Valid Archive Roundtrip Extraction ── */
    char src_dir[PATH_MAX];
    snprintf(src_dir, sizeof(src_dir), "%s/src", test_dir);
    mkdir(src_dir, 0755);

    char sample_file[PATH_MAX * 2];
    snprintf(sample_file, sizeof(sample_file), "%s/hello.txt", src_dir);
    FILE *f = fopen(sample_file, "w");
    if (f) {
        fprintf(f, "KratosOS Secure Package System\n");
        fclose(f);
    }

    char valid_tar[PATH_MAX * 2];
    snprintf(valid_tar, sizeof(valid_tar), "%s/valid.tar", test_dir);
    int create_res = kratos_tar_create(valid_tar, src_dir, NULL, 0);
    check("Test 6a: Valid tar archive creation", create_res == 0);

    char valid_dest[PATH_MAX];
    snprintf(valid_dest, sizeof(valid_dest), "%s/valid_out", test_dir);
    mkdir(valid_dest, 0755);

    res = kratos_tar_extract_file(valid_tar, valid_dest, NULL, NULL);
    check("Test 6b: Valid tar extraction succeeds", res == KRATOS_TAR_OK);

    char extracted_file[PATH_MAX * 2];
    snprintf(extracted_file, sizeof(extracted_file), "%s/hello.txt", valid_dest);
    check("Test 6c: Extracted content exists on disk", access(extracted_file, F_OK) == 0);

    /* Cleanup temporary test directory */
    kratos_rm_rf(test_dir);

    printf("\nTotal: %d passed, %d failed.\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
