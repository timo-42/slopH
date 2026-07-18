#include "syscall.h"

.text

.p2align 2
.globl _sloph_syscall_read
_sloph_syscall_read:
    b _read

.p2align 2
.globl _sloph_syscall_write
_sloph_syscall_write:
    b _write

.subsections_via_symbols
