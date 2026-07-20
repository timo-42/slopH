#include "filesystem.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *sloph_path(const void *data, size_t length) {
    if (length == SIZE_MAX || (length != 0u && data == NULL) || memchr(data, 0, length) != NULL) {
        errno = EINVAL;
        return NULL;
    }
    char *path = malloc(length + 1u);
    if (path == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    if (length != 0u) memcpy(path, data, length);
    path[length] = '\0';
    return path;
}

static int descriptor(int64_t value) {
    if (value < 0 || value > INT_MAX) {
        errno = EBADF;
        return -1;
    }
    return (int)value;
}

int64_t sloph_filesystem_open_read(const void *data, size_t length) {
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    int result = open(path, O_RDONLY);
    free(path);
    return result;
}

int64_t sloph_filesystem_open_write(const void *data, size_t length, int64_t mode) {
    int flags;
    switch (mode) {
        case 0: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
        case 1: flags = O_WRONLY | O_CREAT | O_APPEND; break;
        case 2: flags = O_WRONLY | O_TRUNC; break;
        case 3: flags = O_WRONLY | O_APPEND; break;
        case 4: flags = O_WRONLY | O_CREAT | O_EXCL; break;
        default: errno = EINVAL; return -1;
    }
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    int result = open(path, flags, 0666);
    free(path);
    return result;
}

int64_t sloph_filesystem_close(int64_t value) {
    int fd = descriptor(value);
    return fd < 0 ? -1 : close(fd);
}

int64_t sloph_filesystem_sync(int64_t value) {
    int fd = descriptor(value);
    return fd < 0 ? -1 : fsync(fd);
}

int64_t sloph_filesystem_exists(const void *data, size_t length) {
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    struct stat status;
    int result = lstat(path, &status);
    int saved = errno;
    free(path);
    if (result == 0) return 1;
    if (saved == ENOENT || saved == ENOTDIR) { errno = 0; return 0; }
    errno = saved;
    return -1;
}

static int stat_path(const void *data, size_t length, struct stat *status) {
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    int result = lstat(path, status);
    int saved = errno;
    free(path);
    errno = saved;
    return result;
}

int64_t sloph_filesystem_metadata_size(const void *data, size_t length) {
    struct stat status;
    if (stat_path(data, length, &status) < 0) return -1;
    if (status.st_size < 0 || (uintmax_t)status.st_size > INT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    return (int64_t)status.st_size;
}

int64_t sloph_filesystem_metadata_kind(const void *data, size_t length) {
    struct stat status;
    if (stat_path(data, length, &status) < 0) return -1;
    if (S_ISREG(status.st_mode)) return 0;
    if (S_ISDIR(status.st_mode)) return 1;
    if (S_ISLNK(status.st_mode)) return 2;
    return 3;
}

int64_t sloph_filesystem_create_directory(const void *data, size_t length) {
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    int result = mkdir(path, 0777);
    free(path);
    return result;
}

int64_t sloph_filesystem_remove_file(const void *data, size_t length) {
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    int result = unlink(path);
    free(path);
    return result;
}

int64_t sloph_filesystem_remove_directory(const void *data, size_t length) {
    char *path = sloph_path(data, length);
    if (path == NULL) return -1;
    int result = rmdir(path);
    free(path);
    return result;
}

int64_t sloph_filesystem_rename(const void *source_data, size_t source_length,
                                const void *target_data, size_t target_length) {
    char *source = sloph_path(source_data, source_length);
    if (source == NULL) return -1;
    char *target = sloph_path(target_data, target_length);
    if (target == NULL) { free(source); return -1; }
    int result = rename(source, target);
    int saved = errno;
    free(source);
    free(target);
    errno = saved;
    return result;
}
