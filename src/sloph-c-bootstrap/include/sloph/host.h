#ifndef SLOPH_HOST_H
#define SLOPH_HOST_H

#include "sloph/context.h"

typedef struct SlophOwnedBytes {
    unsigned char *data;
    size_t length;
    size_t allocation_size;
} SlophOwnedBytes;

typedef struct SlophTargetInfo {
    const char *operating_system;
    const char *architecture;
} SlophTargetInfo;

typedef struct SlophHost {
    void *user_data;
    SlophStatus (*read_file)(void *user_data, SlophContext *context,
                             const char *path, size_t max_bytes,
                             SlophOwnedBytes *out_bytes);
    SlophStatus (*write_file_atomic)(void *user_data, SlophContext *context,
                                     const char *path, SlophBytes bytes,
                                     unsigned int mode);
    void (*release_bytes)(void *user_data, SlophContext *context,
                          SlophOwnedBytes *bytes);
    SlophStatus (*target_info)(void *user_data, SlophTargetInfo *out_target);
    SlophStatus (*monotonic_nanoseconds)(void *user_data,
                                         uint64_t *out_nanoseconds);
} SlophHost;

SlophHost sloph_posix_host(void);

#endif
