#ifndef SLOPH_MACOS_SYSCALL_H
#define SLOPH_MACOS_SYSCALL_H

#if !defined(__APPLE__) || !(defined(__aarch64__) || defined(__arm64__))
#error "this syscall boundary requires macOS ARM64"
#endif

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <sys/types.h>

ssize_t sloph_syscall_read(int descriptor, void *buffer, size_t count);
ssize_t sloph_syscall_write(
    int descriptor,
    const void *buffer,
    size_t count
);

#endif

#endif
