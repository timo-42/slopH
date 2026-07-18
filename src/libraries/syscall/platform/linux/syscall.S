#include "syscall.h"

.text

.p2align 4
.globl sloph_syscall_read
.type sloph_syscall_read, @function
sloph_syscall_read:
    movl $SLOPH_LINUX_SYS_READ, %eax
    syscall
    cmpq $-4095, %rax
    jae .Lread_error
    ret
.Lread_error:
    negq %rax
    pushq %rax
    call __errno_location@PLT
    popq %rdx
    movl %edx, (%rax)
    movq $-1, %rax
    ret
.size sloph_syscall_read, .-sloph_syscall_read

.p2align 4
.globl sloph_syscall_write
.type sloph_syscall_write, @function
sloph_syscall_write:
    movl $SLOPH_LINUX_SYS_WRITE, %eax
    syscall
    cmpq $-4095, %rax
    jae .Lwrite_error
    ret
.Lwrite_error:
    negq %rax
    pushq %rax
    call __errno_location@PLT
    popq %rdx
    movl %edx, (%rax)
    movq $-1, %rax
    ret
.size sloph_syscall_write, .-sloph_syscall_write

.section .note.GNU-stack,"",@progbits
