#include "memory.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

void *sloph_syscall_map_pages(size_t requested, size_t *mapped_size) {
    long native_page = sysconf(_SC_PAGESIZE);
    if (native_page <= 0) { errno = EINVAL; return NULL; }
    size_t page = (size_t)native_page;
    size_t size = requested == 0 ? 1u : requested;
    if (size > SIZE_MAX - (page - 1u)) { errno = ENOMEM; return NULL; }
    size = ((size + page - 1u) / page) * page;
    void *address = mmap(NULL, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (address == MAP_FAILED) return NULL;
    *mapped_size = size;
    return address;
}

int sloph_syscall_unmap_pages(void *address, size_t mapped_size) {
    return munmap(address, mapped_size);
}
