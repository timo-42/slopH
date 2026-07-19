#ifndef SLOPH_INTERNAL_H
#define SLOPH_INTERNAL_H

#include "sloph/context.h"

#include <stdbool.h>

typedef struct SlophBuffer {
    SlophContext *context;
    unsigned char *data;
    size_t length;
    size_t capacity;
    size_t max_bytes;
} SlophBuffer;

void sloph_buffer_init(SlophBuffer *buffer, SlophContext *context,
                       size_t max_bytes);
void sloph_buffer_destroy(SlophBuffer *buffer);
SlophStatus sloph_buffer_reserve(SlophBuffer *buffer, size_t additional);
SlophStatus sloph_buffer_append(SlophBuffer *buffer, const void *data,
                                size_t length);
SlophStatus sloph_buffer_append_byte(SlophBuffer *buffer, unsigned char value);
unsigned char *sloph_buffer_take(SlophBuffer *buffer, size_t *out_length,
                                 size_t *out_allocation_size);

typedef struct SlophArenaBlock SlophArenaBlock;
typedef struct SlophArena {
    SlophContext *context;
    SlophArenaBlock *head;
    size_t allocated_bytes;
    size_t max_bytes;
    size_t block_size;
} SlophArena;

void sloph_arena_init(SlophArena *arena, SlophContext *context,
                      size_t max_bytes, size_t block_size);
void sloph_arena_destroy(SlophArena *arena);
SlophStatus sloph_arena_allocate(SlophArena *arena, size_t size,
                                 size_t alignment, void **out_pointer);
SlophStatus sloph_arena_copy_string(SlophArena *arena, const char *source,
                                    size_t length, char **out_string);

bool sloph_size_add(size_t left, size_t right, size_t *out_value);
bool sloph_size_multiply(size_t left, size_t right, size_t *out_value);

#endif
