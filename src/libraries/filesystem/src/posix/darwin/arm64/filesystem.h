#ifndef SLOPH_FILESYSTEM_H
#define SLOPH_FILESYSTEM_H
#include <stddef.h>
#include <stdint.h>
int64_t sloph_filesystem_open_read(const void *path, size_t length);
int64_t sloph_filesystem_open_write(const void *path, size_t length, int64_t mode);
int64_t sloph_filesystem_close(int64_t descriptor);
int64_t sloph_filesystem_sync(int64_t descriptor);
int64_t sloph_filesystem_exists(const void *path, size_t length);
int64_t sloph_filesystem_metadata_size(const void *path, size_t length);
int64_t sloph_filesystem_metadata_kind(const void *path, size_t length);
int64_t sloph_filesystem_create_directory(const void *path, size_t length);
int64_t sloph_filesystem_remove_file(const void *path, size_t length);
int64_t sloph_filesystem_remove_directory(const void *path, size_t length);
int64_t sloph_filesystem_rename(const void *source, size_t source_length, const void *target, size_t target_length);
#endif
