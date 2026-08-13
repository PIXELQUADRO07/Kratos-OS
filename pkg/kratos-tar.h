/* kratos-tar.h — Secure In-Process Tar Parser & Extractor for KratosOS
 *
 * Provides safe archive extraction with built-in defenses against:
 *   - Zip-Slip / path traversal (../, leading /)
 *   - Symlink escape attacks (targets pointing outside target root)
 *   - Unsafe file types (device nodes, FIFOs)
 *   - Archive bombs (decompression/file size limits)
 *   - Dangerous permissions
 */

#ifndef KRATOS_TAR_H
#define KRATOS_TAR_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KRATOS_TAR_OK                   0
#define KRATOS_TAR_ERR_OPEN            -1
#define KRATOS_TAR_ERR_READ            -2
#define KRATOS_TAR_ERR_WRITE           -3
#define KRATOS_TAR_ERR_CORRUPT         -4
#define KRATOS_TAR_ERR_PATH_TRAVERSAL  -5
#define KRATOS_TAR_ERR_UNSAFE_TYPE     -6
#define KRATOS_TAR_ERR_SYMLINK_ESCAPE  -7
#define KRATOS_TAR_ERR_SIZE_LIMIT      -8
#define KRATOS_TAR_ERR_DECOMPRESS      -9

typedef struct {
    char name[512];
    char linkname[512];
    mode_t mode;
    uid_t uid;
    gid_t gid;
    size_t size;
    time_t mtime;
    char typeflag;
} kratos_tar_entry_t;

typedef int (*kratos_tar_entry_cb)(const kratos_tar_entry_t *entry, void *user_data);

/* Validates that a relative path is strictly contained within dest_dir.
 * Returns 1 if safe, 0 if unsafe/traversal attempt. */
int kratos_is_safe_relpath(const char *rel_path);

/* Safe in-process extraction of a raw tar stream from an open file descriptor.
 * Calls callback for each extracted entry (can be NULL). */
int kratos_tar_extract_fd(int fd, const char *dest_dir, kratos_tar_entry_cb cb, void *user_data);

/* Safe extraction of an uncompressed .tar file */
int kratos_tar_extract_file(const char *tar_path, const char *dest_dir, kratos_tar_entry_cb cb, void *user_data);

/* Safe extraction of a gzip-compressed .tar.gz / .tgz archive (using fork/execve to gunzip/gzip without system()) */
int kratos_tar_extract_gz(const char *gz_path, const char *dest_dir, kratos_tar_entry_cb cb, void *user_data);

/* Create a standard tar archive from a source directory and file list */
int kratos_tar_create(const char *tar_path, const char *src_dir, const char **files, size_t file_count);

/* Create a gzip-compressed tar archive (.tar.gz) using pipe/fork/execve without system() */
int kratos_tar_create_gz(const char *gz_path, const char *src_dir);

/* Safe recursive directory remover using POSIX openat/fdopendir/unlinkat/rmdir (replaces rm -rf) */
int kratos_rm_rf(const char *path);

/* Safe single file copy (replaces cp) */
int kratos_copy_file(const char *src, const char *dst, mode_t mode);

/* Safe recursive directory copy (replaces cp -r) */
int kratos_copy_tree(const char *src, const char *dst);

/* Safe hook execution (replaces system() for package hooks) */
int kratos_run_hook_safe(const char *hook_path, const char *root_dir);

#ifdef __cplusplus
}
#endif

#endif /* KRATOS_TAR_H */
