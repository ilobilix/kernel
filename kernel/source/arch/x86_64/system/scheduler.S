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

sched_enter_user:
    push 0x23
    push rsi
    push 0x202
    push 0x2B
    push rdi

    xor r15, r15
    xor r14, r14
    xor r13, r13
    xor r12, r12
    xor r11, r11
    xor r10, r10
    xor r9, r9
    xor r8, r8
    xor rbp, rbp
    xor rdi, rdi
    xor rsi, rsi
    xor rdx, rdx
    xor rcx, rcx
    xor rbx, rbx
    xor rax, rax

    swapgs
    iretq
.global sched_enter_user
