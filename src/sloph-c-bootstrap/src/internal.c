#include "internal.h"

#include <stdint.h>
#include <string.h>

struct SlophArenaBlock {
    SlophArenaBlock *next;
    size_t allocation_size;
    size_t used;
    size_t capacity;
    max_align_t alignment;
    unsigned char data[];
};

bool sloph_size_add(size_t left, size_t right, size_t *out_value) {
    if (out_value == NULL || SIZE_MAX - left < right) return false;
    *out_value = left + right;
    return true;
}

bool sloph_size_multiply(size_t left, size_t right, size_t *out_value) {
    if (out_value == NULL || (left != 0u && right > SIZE_MAX / left)) return false;
    *out_value = left * right;
    return true;
}

void sloph_buffer_init(SlophBuffer *buffer, SlophContext *context,
                       size_t max_bytes) {
    if (buffer == NULL) return;
    buffer->context = context;
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
    buffer->max_bytes = max_bytes;
}

void sloph_buffer_destroy(SlophBuffer *buffer) {
    const SlophAllocator *allocator;
    if (buffer == NULL || buffer->context == NULL) return;
    allocator = sloph_context_allocator(buffer->context);
    if (buffer->data != NULL)
        allocator->deallocate(allocator->user_data, buffer->data, buffer->capacity);
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

SlophStatus sloph_buffer_reserve(SlophBuffer *buffer, size_t additional) {
    const SlophAllocator *allocator;
    size_t required;
    size_t capacity;
    unsigned char *grown;
    if (buffer == NULL || buffer->context == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    if (!sloph_size_add(buffer->length, additional, &required) ||
        required > buffer->max_bytes)
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    if (required <= buffer->capacity) return SLOPH_STATUS_OK;
    capacity = buffer->capacity == 0u ? 64u : buffer->capacity;
    if (capacity > buffer->max_bytes) capacity = buffer->max_bytes;
    while (capacity < required) {
        if (capacity > buffer->max_bytes / 2u) {
            capacity = buffer->max_bytes;
            break;
        }
        capacity *= 2u;
    }
    allocator = sloph_context_allocator(buffer->context);
    grown = allocator->resize(allocator->user_data, buffer->data,
                              buffer->capacity, capacity);
    if (grown == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    buffer->data = grown;
    buffer->capacity = capacity;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_buffer_append(SlophBuffer *buffer, const void *data,
                                size_t length) {
    SlophStatus status;
    if (length != 0u && data == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    status = sloph_buffer_reserve(buffer, length);
    if (status != SLOPH_STATUS_OK) return status;
    if (length != 0u) memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_buffer_append_byte(SlophBuffer *buffer, unsigned char value) {
    return sloph_buffer_append(buffer, &value, 1u);
}

unsigned char *sloph_buffer_take(SlophBuffer *buffer, size_t *out_length,
                                 size_t *out_allocation_size) {
    unsigned char *data;
    if (buffer == NULL) return NULL;
    data = buffer->data;
    if (out_length != NULL) *out_length = buffer->length;
    if (out_allocation_size != NULL) *out_allocation_size = buffer->capacity;
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
    return data;
}

void sloph_arena_init(SlophArena *arena, SlophContext *context,
                      size_t max_bytes, size_t block_size) {
    if (arena == NULL) return;
    arena->context = context;
    arena->head = NULL;
    arena->allocated_bytes = 0u;
    arena->max_bytes = max_bytes;
    arena->block_size = block_size == 0u ? 4096u : block_size;
}

void sloph_arena_destroy(SlophArena *arena) {
    const SlophAllocator *allocator;
    SlophArenaBlock *block;
    if (arena == NULL || arena->context == NULL) return;
    allocator = sloph_context_allocator(arena->context);
    block = arena->head;
    while (block != NULL) {
        SlophArenaBlock *next = block->next;
        allocator->deallocate(allocator->user_data, block,
                              block->allocation_size);
        block = next;
    }
    arena->head = NULL;
    arena->allocated_bytes = 0u;
}

static int valid_alignment(size_t alignment) {
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

SlophStatus sloph_arena_allocate(SlophArena *arena, size_t size,
                                 size_t alignment, void **out_pointer) {
    const SlophAllocator *allocator;
    SlophArenaBlock *block;
    size_t padding;
    size_t needed;
    size_t capacity;
    size_t allocation_size;
    uintptr_t address;
    if (arena == NULL || arena->context == NULL || out_pointer == NULL ||
        size == 0u || !valid_alignment(alignment) || alignment > _Alignof(max_align_t))
        return SLOPH_STATUS_INVALID_ARGUMENT;
    *out_pointer = NULL;
    block = arena->head;
    if (block != NULL) {
        address = (uintptr_t)(block->data + block->used);
        padding = (alignment - (address & (alignment - 1u))) & (alignment - 1u);
        if (sloph_size_add(padding, size, &needed) &&
            needed <= block->capacity - block->used) {
            block->used += padding;
            *out_pointer = block->data + block->used;
            block->used += size;
            return SLOPH_STATUS_OK;
        }
    }
    if (!sloph_size_add(size, alignment - 1u, &needed))
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    capacity = needed > arena->block_size ? needed : arena->block_size;
    if (capacity > arena->max_bytes - arena->allocated_bytes ||
        !sloph_size_add(offsetof(SlophArenaBlock, data), capacity,
                        &allocation_size))
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    allocator = sloph_context_allocator(arena->context);
    block = allocator->allocate(allocator->user_data, allocation_size);
    if (block == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    block->next = arena->head;
    block->allocation_size = allocation_size;
    block->used = 0u;
    block->capacity = capacity;
    arena->head = block;
    arena->allocated_bytes += capacity;
    address = (uintptr_t)block->data;
    padding = (alignment - (address & (alignment - 1u))) & (alignment - 1u);
    block->used = padding;
    *out_pointer = block->data + block->used;
    block->used += size;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_arena_copy_string(SlophArena *arena, const char *source,
                                    size_t length, char **out_string) {
    SlophStatus status;
    void *destination;
    size_t size;
    if (source == NULL || out_string == NULL ||
        !sloph_size_add(length, 1u, &size))
        return SLOPH_STATUS_INVALID_ARGUMENT;
    status = sloph_arena_allocate(arena, size, 1u, &destination);
    if (status != SLOPH_STATUS_OK) return status;
    memcpy(destination, source, length);
    ((char *)destination)[length] = '\0';
    *out_string = destination;
    return SLOPH_STATUS_OK;
}
