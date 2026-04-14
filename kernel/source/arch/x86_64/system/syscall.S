# Copyright (C) 2024-2026  ilobilo

.include "arch/x86_64/system/cpu.S"

.extern syscall_handler
syscall_entry:
    swapgs

    mov gs:[8], rsp
    mov rsp, gs:[16]

    push 0x23 # ss
    push gs:[8] # rsp
    push r11 # rflags
    push 0x2B # cs
    push rcx # rip
    push 0 # error_code
    push 0 # vector

    pushall

    sti
    mov rdi, rsp
    call syscall_handler
    cli

    popall

    add rsp, 16

    pop rcx
    add rsp, 8
    pop r11
    pop rsp

    swapgs
    sysretq
.global syscall_entry
