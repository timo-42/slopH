#include "sloph/host.h"

#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static SlophStatus io_failure(SlophContext *context, const char *message) {
    (void)sloph_context_add_diagnostic(context, "host.io_error", "host",
                                       message, SLOPH_UNKNOWN_SPAN);
    return SLOPH_STATUS_IO_ERROR;
}

static SlophStatus posix_read_file(void *user_data, SlophContext *context,
                                   const char *path, size_t max_bytes,
                                   SlophOwnedBytes *out_bytes) {
    int descriptor;
    unsigned char chunk[16384];
    SlophBuffer buffer;
    SlophStatus status = SLOPH_STATUS_OK;
    (void)user_data;
    if (context == NULL || path == NULL || out_bytes == NULL || max_bytes == 0u)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    out_bytes->data = NULL;
    out_bytes->length = 0u;
    out_bytes->allocation_size = 0u;
    descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return io_failure(context, "could not open file");
    sloph_buffer_init(&buffer, context, max_bytes);
    for (;;) {
        ssize_t count = read(descriptor, chunk, sizeof(chunk));
        if (count < 0) {
            if (errno == EINTR) continue;
            status = io_failure(context, "could not read file");
            break;
        }
        if (count == 0) break;
        if ((size_t)count > max_bytes - buffer.length) {
            (void)sloph_context_add_diagnostic(
                context, "host.input_limit_exceeded", "host",
                "file exceeds configured byte limit", SLOPH_UNKNOWN_SPAN);
            status = SLOPH_STATUS_LIMIT_EXCEEDED;
            break;
        }
        status = sloph_buffer_append(&buffer, chunk, (size_t)count);
        if (status != SLOPH_STATUS_OK) break;
    }
    if (close(descriptor) != 0 && status == SLOPH_STATUS_OK)
        status = io_failure(context, "could not close file");
    if (status != SLOPH_STATUS_OK) {
        sloph_buffer_destroy(&buffer);
        return status;
    }
    out_bytes->data = sloph_buffer_take(&buffer, &out_bytes->length,
                                        &out_bytes->allocation_size);
    return SLOPH_STATUS_OK;
}

static void posix_release_bytes(void *user_data, SlophContext *context,
                                SlophOwnedBytes *bytes) {
    const SlophAllocator *allocator;
    (void)user_data;
    if (context == NULL || bytes == NULL) return;
    allocator = sloph_context_allocator(context);
    if (bytes->data != NULL)
        allocator->deallocate(allocator->user_data, bytes->data,
                              bytes->allocation_size);
    bytes->data = NULL;
    bytes->length = 0u;
    bytes->allocation_size = 0u;
}

static SlophStatus write_all(int descriptor, SlophBytes bytes) {
    size_t offset = 0u;
    while (offset < bytes.length) {
        ssize_t count = write(descriptor, bytes.data + offset,
                              bytes.length - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            return SLOPH_STATUS_IO_ERROR;
        }
        if (count == 0) return SLOPH_STATUS_IO_ERROR;
        offset += (size_t)count;
    }
    return SLOPH_STATUS_OK;
}

static SlophStatus posix_write_file_atomic(void *user_data,
                                           SlophContext *context,
                                           const char *path, SlophBytes bytes,
                                           unsigned int mode) {
    const SlophAllocator *allocator;
    static const char suffix[] = ".tmp.XXXXXX";
    size_t path_length;
    size_t temporary_size;
    char *temporary;
    int descriptor;
    SlophStatus status;
    (void)user_data;
    if (context == NULL || path == NULL ||
        (bytes.length != 0u && bytes.data == NULL) || mode > 0777u)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    if (bytes.length > sloph_context_limits(context)->output_bytes)
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    path_length = strlen(path);
    if (!sloph_size_add(path_length, sizeof(suffix), &temporary_size) ||
        temporary_size > sloph_context_limits(context)->output_bytes)
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    allocator = sloph_context_allocator(context);
    temporary = allocator->allocate(allocator->user_data, temporary_size);
    if (temporary == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, suffix, sizeof(suffix));
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        allocator->deallocate(allocator->user_data, temporary, temporary_size);
        return io_failure(context, "could not create temporary output file");
    }
    status = fchmod(descriptor, (mode_t)mode) == 0
                 ? write_all(descriptor, bytes) : SLOPH_STATUS_IO_ERROR;
    if (status == SLOPH_STATUS_OK && fsync(descriptor) != 0)
        status = SLOPH_STATUS_IO_ERROR;
    if (close(descriptor) != 0 && status == SLOPH_STATUS_OK)
        status = SLOPH_STATUS_IO_ERROR;
    if (status == SLOPH_STATUS_OK && rename(temporary, path) != 0)
        status = SLOPH_STATUS_IO_ERROR;
    if (status != SLOPH_STATUS_OK) {
        (void)unlink(temporary);
        status = io_failure(context, "could not atomically write file");
    }
    allocator->deallocate(allocator->user_data, temporary, temporary_size);
    return status;
}

static SlophStatus posix_target_info(void *user_data,
                                     SlophTargetInfo *out_target) {
    (void)user_data;
    if (out_target == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
#if defined(__APPLE__)
    out_target->operating_system = "darwin";
#elif defined(__linux__)
    out_target->operating_system = "linux";
#else
    out_target->operating_system = "unknown";
#endif
#if defined(__aarch64__) || defined(__arm64__)
    out_target->architecture = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    out_target->architecture = "amd64";
#else
    out_target->architecture = "unknown";
#endif
    return SLOPH_STATUS_OK;
}

static SlophStatus posix_monotonic_nanoseconds(void *user_data,
                                               uint64_t *out_nanoseconds) {
    struct timespec value;
    (void)user_data;
    if (out_nanoseconds == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return SLOPH_STATUS_IO_ERROR;
    *out_nanoseconds = (uint64_t)value.tv_sec * UINT64_C(1000000000) +
                       (uint64_t)value.tv_nsec;
    return SLOPH_STATUS_OK;
}

SlophHost sloph_posix_host(void) {
    SlophHost host;
    host.user_data = NULL;
    host.read_file = posix_read_file;
    host.write_file_atomic = posix_write_file_atomic;
    host.release_bytes = posix_release_bytes;
    host.target_info = posix_target_info;
    host.monotonic_nanoseconds = posix_monotonic_nanoseconds;
    return host;
}
