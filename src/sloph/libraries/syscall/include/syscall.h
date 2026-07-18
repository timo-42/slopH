#ifndef SLOPH_SYSCALL_H
#define SLOPH_SYSCALL_H

#include <stddef.h>
#include <sys/types.h>

ssize_t sloph_syscall_read(int descriptor, void *buffer, size_t count);
ssize_t sloph_syscall_write(
    int descriptor,
    const void *buffer,
    size_t count
);

#endif
