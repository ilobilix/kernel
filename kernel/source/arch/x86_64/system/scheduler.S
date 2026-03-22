# Copyright (C) 2024-2026  ilobilo

.include "arch/x86_64/system/cpu.S"

sched_switch_context:
    mov [rdi + 0], r15
    mov [rdi + 8], r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], r11
    mov [rdi + 40], r10
    mov [rdi + 48], r9
    mov [rdi + 56], r8
    mov [rdi + 64], rbp
    mov [rdi + 72], rdi
    mov [rdi + 80], rsi
    mov [rdi + 88], rdx
    mov [rdi + 96], rcx
    mov [rdi + 104], rbx
    mov [rdi + 112], rax

    mov rax, [rsp]
    mov [rdi + 136], rax

    mov rax, cs
    mov [rdi + 144], rax

    pushf
    pop rax
    mov [rdi + 152], rax

    lea rax, [rsp + 8]
    mov [rdi + 160], rax

    mov rax, ss
    mov [rdi + 168], rax

    mov rsp, rsi

    popall
    add rsp, 16

    test qword ptr [rsp + 8], 0x03
    je 1f
    swapgs
1:

    iretq
.global sched_switch_context
