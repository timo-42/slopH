#ifndef SLOPH_DARWIN_MEMORY_H
#define SLOPH_DARWIN_MEMORY_H

#if !defined(__APPLE__) || !(defined(__aarch64__) || defined(__arm64__))
#error "this memory boundary requires Darwin ARM64"
#endif

#include <stddef.h>
void *sloph_syscall_map_pages(size_t requested, size_t *mapped_size);
int sloph_syscall_unmap_pages(void *address, size_t mapped_size);

#endif
