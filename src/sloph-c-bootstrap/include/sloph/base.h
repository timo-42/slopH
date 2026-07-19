#ifndef SLOPH_BASE_H
#define SLOPH_BASE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlophStatus {
    SLOPH_STATUS_OK = 0,
    SLOPH_STATUS_INVALID_ARGUMENT,
    SLOPH_STATUS_OUT_OF_MEMORY,
    SLOPH_STATUS_LIMIT_EXCEEDED,
    SLOPH_STATUS_IO_ERROR,
    SLOPH_STATUS_PROCESS_ERROR,
    SLOPH_STATUS_INTERNAL_ERROR
} SlophStatus;

typedef struct SlophSpan {
    size_t start;
    size_t end;
} SlophSpan;

#define SLOPH_UNKNOWN_SPAN ((SlophSpan){0u, 0u})

typedef struct SlophBytes {
    const unsigned char *data;
    size_t length;
} SlophBytes;

typedef struct SlophAllocator {
    void *user_data;
    void *(*allocate)(void *user_data, size_t size);
    void *(*resize)(void *user_data, void *pointer, size_t old_size,
                    size_t new_size);
    void (*deallocate)(void *user_data, void *pointer, size_t size);
} SlophAllocator;

const char *sloph_status_name(SlophStatus status);
SlophAllocator sloph_system_allocator(void);

#ifdef __cplusplus
}
#endif

#endif
