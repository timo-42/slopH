#include "syscall.h"

#include <unistd.h>

ssize_t sloph_syscall_read(int descriptor, void *buffer, size_t count) {
    return read(descriptor, buffer, count);
}

ssize_t sloph_syscall_write(
    int descriptor,
    const void *buffer,
    size_t count
) {
    return write(descriptor, buffer, count);
}
