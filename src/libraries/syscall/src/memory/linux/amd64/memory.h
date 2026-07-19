#ifndef SLOPH_LINUX_MEMORY_H
#define SLOPH_LINUX_MEMORY_H

#if !defined(__linux__) || !defined(__x86_64__)
#error "this memory boundary requires Linux AMD64"
#endif

#include <stddef.h>
void *sloph_syscall_map_pages(size_t requested, size_t *mapped_size);
int sloph_syscall_unmap_pages(void *address, size_t mapped_size);

#endif
