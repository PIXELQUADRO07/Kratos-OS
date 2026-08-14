/* kratos-tar.c — Secure In-Process Tar Parser & Extractor Implementation
 *
 * Designed for KratosOS Package Manager (KPM).
 */

#define _GNU_SOURCE

#include "kratos-tar.h"
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
#include <unistd.h>

#define TAR_BLOCK_SIZE 512
#define MAX_EXTRACT_FILE_SIZE (512ULL * 1024 * 1024) /* 512 MB per file limit */

/* POSIX ustar header format (512 bytes) */
struct posix_tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

/* ------------------------------------------------------------------ */
/* Octal String Helpers                                                */
/* ------------------------------------------------------------------ */

static unsigned long parse_octal(const char *str, size_t len)
{
    unsigned long val = 0;
    while (len > 0 && (*str == ' ' || *str == '\0')) {
        str++;
        len--;
    }
    while (len > 0 && *str >= '0' && *str <= '7') {
        val = (val << 3) | (*str - '0');
        str++;
        len--;
    }
    return val;
}

static int verify_tar_checksum(const struct posix_tar_header *hdr)
{
    unsigned long stored_chksum = parse_octal(hdr->chksum, sizeof(hdr->chksum));
    unsigned long calc_chksum_unsigned = 0;
    signed long calc_chksum_signed = 0;

    const unsigned char *bytes = (const unsigned char *)hdr;
    for (size_t i = 0; i < sizeof(struct posix_tar_header); i++) {
        if (i >= offsetof(struct posix_tar_header, chksum) &&
            i < offsetof(struct posix_tar_header, chksum) + sizeof(hdr->chksum)) {
            calc_chksum_unsigned += ' ';
            calc_chksum_signed += ' ';
        } else {
            calc_chksum_unsigned += bytes[i];
            calc_chksum_signed += (signed char)bytes[i];
        }
    }

    return (stored_chksum == calc_chksum_unsigned || (signed long)stored_chksum == calc_chksum_signed);
}

/* ------------------------------------------------------------------ */
/* Security Validations: Path Traversal & Symlink Checks              */
/* ------------------------------------------------------------------ */

int kratos_is_safe_relpath(const char *rel_path)
{
    if (!rel_path || rel_path[0] == '\0') return 0;

    /* Disallow absolute paths */
    if (rel_path[0] == '/' || rel_path[0] == '\\') return 0;

    /* Check for forbidden directory traversal tokens */
    if (strcmp(rel_path, "..") == 0 ||
        strncmp(rel_path, "../", 3) == 0 ||
        strstr(rel_path, "/../") != NULL ||
        strstr(rel_path, "/..") != NULL ||
        strstr(rel_path, "\\..") != NULL) {
        return 0;
    }

    /* Check for control characters */
    for (const char *p = rel_path; *p; p++) {
        if ((unsigned char)*p < 32 && *p != '\t') return 0;
    }

    /* Check length limit */
    if (strlen(rel_path) >= PATH_MAX - 32) return 0;

    return 1;
}

static int is_safe_symlink_target(const char *linkname)
{
    if (!linkname || linkname[0] == '\0') return 0;

    /* Reject absolute targets: a symlink pointing outside dest_dir (e.g.
     * "/etc") lets any later archive entry that writes "through" it land
     * on the real filesystem instead of inside the extraction root. */
    if (linkname[0] == '/' || linkname[0] == '\\') return 0;

    /* Reject traversal out of bounds */
    if (strcmp(linkname, "..") == 0 ||
        strncmp(linkname, "../", 3) == 0 ||
        strstr(linkname, "/../") != NULL ||
        strstr(linkname, "/..") != NULL) {
        return 0;
    }

    /* Reject control characters */
    for (const char *p = linkname; *p; p++) {
        if ((unsigned char)*p < 32) return 0;
    }

    return 1;
}

static int make_parent_dirs(const char *filepath)
{
    char tmp[PATH_MAX];
    strncpy(tmp, filepath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return 0;

    *slash = '\0';

    char path[PATH_MAX] = "";
    char *token = strtok(tmp, "/");
    if (filepath[0] == '/') {
        strcat(path, "/");
    }

    while (token != NULL) {
        if (path[0] != '\0' && path[strlen(path)-1] != '/') {
            strcat(path, "/");
        }
        strcat(path, token);
        mkdir(path, 0755);
        token = strtok(NULL, "/");
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* In-Process Tar Stream Extraction Engine                            */
/* ------------------------------------------------------------------ */

int kratos_tar_extract_fd(int fd, const char *dest_dir, kratos_tar_entry_cb cb, void *user_data)
{
    struct posix_tar_header hdr;
    int consecutive_zero_blocks = 0;

    while (1) {
        ssize_t n = read(fd, &hdr, TAR_BLOCK_SIZE);
        if (n == 0) break;
        if (n != TAR_BLOCK_SIZE) {
            return (consecutive_zero_blocks >= 1) ? KRATOS_TAR_OK : KRATOS_TAR_ERR_READ;
        }

        /* Check for EOF (two consecutive zero blocks) */
        int is_zero = 1;
        const unsigned char *b = (const unsigned char *)&hdr;
        for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
            if (b[i] != 0) { is_zero = 0; break; }
        }

        if (is_zero) {
            consecutive_zero_blocks++;
            if (consecutive_zero_blocks >= 2) break;
            continue;
        }
        consecutive_zero_blocks = 0;

        /* Verify checksum */
        if (!verify_tar_checksum(&hdr)) {
            return KRATOS_TAR_ERR_CORRUPT;
        }

        /* Assemble full entry name (prefix + name) */
        char full_name[512] = {0};
        if (hdr.prefix[0] != '\0' && strncmp(hdr.magic, "ustar", 5) == 0) {
            snprintf(full_name, sizeof(full_name), "%.155s/%.100s", hdr.prefix, hdr.name);
        } else {
            snprintf(full_name, sizeof(full_name), "%.100s", hdr.name);
        }

        /* Strip leading ./ or / */
        const char *clean_rel = full_name;
        while (clean_rel[0] == '.' && clean_rel[1] == '/') clean_rel += 2;
        while (clean_rel[0] == '/') clean_rel++;

        if (clean_rel[0] == '\0') continue;

        /* Security Check: Validate relative path containment */
        if (!kratos_is_safe_relpath(clean_rel)) {
            return KRATOS_TAR_ERR_PATH_TRAVERSAL;
        }

        kratos_tar_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.name, sizeof(entry.name), "%s", clean_rel);
        snprintf(entry.linkname, sizeof(entry.linkname), "%s", hdr.linkname);
        entry.mode = (mode_t)parse_octal(hdr.mode, sizeof(hdr.mode));
        entry.uid  = (uid_t)parse_octal(hdr.uid, sizeof(hdr.uid));
        entry.gid  = (gid_t)parse_octal(hdr.gid, sizeof(hdr.gid));
        entry.size = (size_t)parse_octal(hdr.size, sizeof(hdr.size));
        entry.mtime = (time_t)parse_octal(hdr.mtime, sizeof(hdr.mtime));
        entry.typeflag = hdr.typeflag ? hdr.typeflag : '0';

        if (entry.size > MAX_EXTRACT_FILE_SIZE) {
            return KRATOS_TAR_ERR_SIZE_LIMIT;
        }

        /* Destination target path on disk */
        char target_path[PATH_MAX];
        snprintf(target_path, sizeof(target_path), "%s/%s", dest_dir, clean_rel);

        /* Handle Callback */
        if (cb && cb(&entry, user_data) != 0) {
            return KRATOS_TAR_ERR_CORRUPT;
        }

        /* Process according to entry type */
        if (entry.typeflag == '5') {
            /* Directory */
            make_parent_dirs(target_path);
            mkdir(target_path, (entry.mode ? (entry.mode & 0777) : 0755));
        } else if (entry.typeflag == '2') {
            /* Symlink */
            if (!is_safe_symlink_target(entry.linkname)) {
                return KRATOS_TAR_ERR_SYMLINK_ESCAPE;
            }
            make_parent_dirs(target_path);
            unlink(target_path);
            if (symlink(entry.linkname, target_path) < 0 && errno != EEXIST) {
                /* Failed creating symlink */
            }
        } else if (entry.typeflag == '1') {
            /* Hardlink */
            char link_target[PATH_MAX];
            const char *clean_link = entry.linkname;
            while (clean_link[0] == '.' && clean_link[1] == '/') clean_link += 2;
            while (clean_link[0] == '/') clean_link++;

            if (!kratos_is_safe_relpath(clean_link)) {
                return KRATOS_TAR_ERR_PATH_TRAVERSAL;
            }
            snprintf(link_target, sizeof(link_target), "%s/%s", dest_dir, clean_link);
            make_parent_dirs(target_path);
            unlink(target_path);
            if (link(link_target, target_path) < 0 && errno != EEXIST) {
                /* Failed creating link */
            }
        } else if (entry.typeflag == '0' || entry.typeflag == '\0') {
            /* Regular File */
            make_parent_dirs(target_path);
            unlink(target_path);

            mode_t file_mode = (entry.mode ? (entry.mode & 0777) : 0644);
            int out_fd = open(target_path, O_WRONLY | O_CREAT | O_TRUNC, file_mode);
            if (out_fd < 0) {
                return KRATOS_TAR_ERR_WRITE;
            }

            size_t bytes_left = entry.size;
            char block[TAR_BLOCK_SIZE];
            while (bytes_left > 0) {
                ssize_t rd = read(fd, block, TAR_BLOCK_SIZE);
                if (rd != TAR_BLOCK_SIZE) {
                    close(out_fd);
                    return KRATOS_TAR_ERR_READ;
                }
                size_t to_write = (bytes_left < TAR_BLOCK_SIZE) ? bytes_left : TAR_BLOCK_SIZE;
                if (write(out_fd, block, to_write) != (ssize_t)to_write) {
                    close(out_fd);
                    return KRATOS_TAR_ERR_WRITE;
                }
                bytes_left -= to_write;
            }
            close(out_fd);
            chmod(target_path, file_mode);
        } else {
            /* Reject dangerous types (device nodes, FIFOs, etc.) */
            return KRATOS_TAR_ERR_UNSAFE_TYPE;
        }
    }

    return KRATOS_TAR_OK;
}

int kratos_tar_extract_file(const char *tar_path, const char *dest_dir, kratos_tar_entry_cb cb, void *user_data)
{
    int fd = open(tar_path, O_RDONLY);
    if (fd < 0) return KRATOS_TAR_ERR_OPEN;

    int res = kratos_tar_extract_fd(fd, dest_dir, cb, user_data);
    close(fd);
    return res;
}

int kratos_tar_extract_gz(const char *gz_path, const char *dest_dir, kratos_tar_entry_cb cb, void *user_data)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return KRATOS_TAR_ERR_DECOMPRESS;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return KRATOS_TAR_ERR_DECOMPRESS;
    }

    if (pid == 0) {
        /* Child: Decompress using gzip / gunzip to pipe */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /* Redirect stderr to /dev/null */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execl("/bin/gzip", "gzip", "-dc", gz_path, (char *)NULL);
        execl("/usr/bin/gzip", "gzip", "-dc", gz_path, (char *)NULL);
        execl("/bin/gunzip", "gunzip", "-c", gz_path, (char *)NULL);
        execl("/usr/bin/gunzip", "gunzip", "-c", gz_path, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    int res = kratos_tar_extract_fd(pipefd[0], dest_dir, cb, user_data);
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (res == KRATOS_TAR_OK) res = KRATOS_TAR_ERR_DECOMPRESS;
    }

    return res;
}

/* ------------------------------------------------------------------ */
/* Tar Archive Creation Engine                                        */
/* ------------------------------------------------------------------ */

static void write_tar_octal(char *dst, size_t size, unsigned long val)
{
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%0%zulo", size - 1);
    snprintf(dst, size, fmt, val);
}

static int append_file_to_tar(int tar_fd, const char *fs_path, const char *rel_name)
{
    struct stat st;
    if (lstat(fs_path, &st) < 0) return -1;

    struct posix_tar_header hdr;
    memset(&hdr, 0, sizeof(hdr));

    strncpy(hdr.name, rel_name, sizeof(hdr.name) - 1);
    write_tar_octal(hdr.mode, sizeof(hdr.mode), st.st_mode & 07777);
    write_tar_octal(hdr.uid,  sizeof(hdr.uid),  st.st_uid);
    write_tar_octal(hdr.gid,  sizeof(hdr.gid),  st.st_gid);
    write_tar_octal(hdr.mtime,sizeof(hdr.mtime),st.st_mtime);
    memcpy(hdr.magic, "ustar  ", 6);

    if (S_ISDIR(st.st_mode)) {
        hdr.typeflag = '5';
        write_tar_octal(hdr.size, sizeof(hdr.size), 0);
        if (hdr.name[strlen(hdr.name)-1] != '/') {
            strncat(hdr.name, "/", sizeof(hdr.name) - strlen(hdr.name) - 1);
        }
    } else if (S_ISLNK(st.st_mode)) {
        hdr.typeflag = '2';
        write_tar_octal(hdr.size, sizeof(hdr.size), 0);
        ssize_t rlen = readlink(fs_path, hdr.linkname, sizeof(hdr.linkname) - 1);
        if (rlen >= 0) hdr.linkname[rlen] = '\0';
    } else if (S_ISREG(st.st_mode)) {
        hdr.typeflag = '0';
        write_tar_octal(hdr.size, sizeof(hdr.size), st.st_size);
    } else {
        return -1;
    }

    /* Compute and store checksum */
    memset(hdr.chksum, ' ', sizeof(hdr.chksum));
    unsigned long sum = 0;
    const unsigned char *p = (const unsigned char *)&hdr;
    for (size_t i = 0; i < sizeof(hdr); i++) sum += p[i];
    write_tar_octal(hdr.chksum, sizeof(hdr.chksum) - 1, sum);
    hdr.chksum[sizeof(hdr.chksum) - 1] = ' ';

    if (write(tar_fd, &hdr, sizeof(hdr)) != sizeof(hdr)) return -1;

    if (S_ISREG(st.st_mode) && st.st_size > 0) {
        int in_fd = open(fs_path, O_RDONLY);
        if (in_fd < 0) return -1;

        char buf[TAR_BLOCK_SIZE];
        ssize_t rd;
        while ((rd = read(in_fd, buf, sizeof(buf))) > 0) {
            if (rd < TAR_BLOCK_SIZE) {
                memset(buf + rd, 0, TAR_BLOCK_SIZE - rd);
                rd = TAR_BLOCK_SIZE;
            }
            if (write(tar_fd, buf, rd) != rd) {
                close(in_fd);
                return -1;
            }
        }
        close(in_fd);
    }

    return 0;
}

static int append_tree_recursive(int tar_fd, const char *base_dir, const char *rel_sub)
{
    char cur_path[PATH_MAX * 2];
    if (rel_sub && rel_sub[0] != '\0') {
        snprintf(cur_path, sizeof(cur_path), "%s/%s", base_dir, rel_sub);
    } else {
        snprintf(cur_path, sizeof(cur_path), "%s", base_dir);
    }

    DIR *d = opendir(cur_path);
    if (!d) return -1;

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

        append_file_to_tar(tar_fd, full_fs, next_rel);

        if (S_ISDIR(st.st_mode)) {
            append_tree_recursive(tar_fd, base_dir, next_rel);
        }
    }
    closedir(d);
    return 0;
}

int kratos_tar_create(const char *tar_path, const char *src_dir, const char **files, size_t file_count)
{
    int fd = open(tar_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    if (files != NULL && file_count > 0) {
        for (size_t i = 0; i < file_count; i++) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", src_dir, files[i]);
            struct stat st;
            if (lstat(full_path, &st) == 0) {
                append_file_to_tar(fd, full_path, files[i]);
                if (S_ISDIR(st.st_mode)) {
                    append_tree_recursive(fd, src_dir, files[i]);
                }
            }
        }
    } else {
        append_tree_recursive(fd, src_dir, "");
    }

    /* Write two 512-byte zero blocks for EOF */
    char zero[TAR_BLOCK_SIZE * 2] = {0};
    if (write(fd, zero, sizeof(zero)) != sizeof(zero)) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int kratos_tar_create_gz(const char *gz_path, const char *src_dir)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: Compress stdin with gzip to target file */
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        int out = open(gz_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out < 0) _exit(1);
        dup2(out, STDOUT_FILENO);
        close(out);

        execl("/bin/gzip", "gzip", "-c", (char *)NULL);
        execl("/usr/bin/gzip", "gzip", "-c", (char *)NULL);
        _exit(127);
    }

    close(pipefd[0]);
    append_tree_recursive(pipefd[1], src_dir, "");
    char zero[TAR_BLOCK_SIZE * 2] = {0};
    if (write(pipefd[1], zero, sizeof(zero)) != sizeof(zero)) {
        /* Continue to close */
    }
    close(pipefd[1]);

    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* POSIX File System Utilities (Replacing system() calls)             */
/* ------------------------------------------------------------------ */

int kratos_rm_rf(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char subpath[PATH_MAX];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            kratos_rm_rf(subpath);
        }
        closedir(d);
        return rmdir(path);
    } else {
        return unlink(path);
    }
}

int kratos_copy_file(const char *src, const char *dst, mode_t mode)
{
    int in = open(src, O_RDONLY);
    if (in < 0) return -1;

    make_parent_dirs(dst);
    unlink(dst);

    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode ? mode : 0644);
    if (out < 0) {
        close(in);
        return -1;
    }

    char buf[8192];
    ssize_t rd;
    while ((rd = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, rd) != rd) {
            close(in);
            close(out);
            return -1;
        }
    }

    close(in);
    close(out);
    if (mode) chmod(dst, mode);
    return 0;
}

int kratos_copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) < 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, st.st_mode & 07777);
        DIR *d = opendir(src);
        if (!d) return -1;

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            char src_sub[PATH_MAX];
            char dst_sub[PATH_MAX];
            snprintf(src_sub, sizeof(src_sub), "%s/%s", src, entry->d_name);
            snprintf(dst_sub, sizeof(dst_sub), "%s/%s", dst, entry->d_name);
            kratos_copy_tree(src_sub, dst_sub);
        }
        closedir(d);
        return 0;
    } else if (S_ISLNK(st.st_mode)) {
        char link_target[PATH_MAX] = {0};
        ssize_t rlen = readlink(src, link_target, sizeof(link_target) - 1);
        if (rlen >= 0) link_target[rlen] = '\0';
        unlink(dst);
        return symlink(link_target, dst);
    } else {
        return kratos_copy_file(src, dst, st.st_mode & 07777);
    }
}

int kratos_run_hook_safe(const char *hook_path, const char *root_dir)
{
    if (access(hook_path, X_OK) != 0) return 0;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        /* Child: set minimal clean environment */
        char env_root[PATH_MAX + 16];
        snprintf(env_root, sizeof(env_root), "ROOT=%s", root_dir ? root_dir : "/");

        char *const envp[] = {
            env_root,
            "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
            "TERM=xterm-256color",
            "LC_ALL=C",
            NULL
        };

        char *const argv[] = {
            (char *)hook_path,
            NULL
        };

        execve(hook_path, argv, envp);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}
