#ifndef SLOPH_LINUX_SYSCALL_H
#define SLOPH_LINUX_SYSCALL_H

#if !defined(__linux__) || !defined(__x86_64__)
#error "this syscall boundary requires Linux AMD64"
#endif

#ifdef __ASSEMBLER__
#define SLOPH_LINUX_SYS_READ 0
#define SLOPH_LINUX_SYS_WRITE 1
#else
#include <stddef.h>
#include <sys/types.h>
ssize_t sloph_syscall_read(int descriptor, void *buffer, size_t count);
ssize_t sloph_syscall_write(int descriptor, const void *buffer, size_t count);
#endif

#endif
