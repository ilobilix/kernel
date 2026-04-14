# Copyright (C) 2024-2025  ilobilo

.include "arch/x86_64/system/cpu.inc"

.extern syscall_handler
syscall_entry:
    swapgs

    mov gs:[8], rsp
    mov rsp, gs:[16]

    push 0x1B # ss
    push gs:[8] # rsp
    push r11 # rflags
    push 0x23 # cs
    push rcx # rip
    push 0 # error_code
    push 0 # int_no

    pushall

    mov rdi, rsp
    call syscall_handler

    popall

    add rsp, 56
    mov rsp, gs:[8]

    swapgs
    sysretq
.global syscall_entry