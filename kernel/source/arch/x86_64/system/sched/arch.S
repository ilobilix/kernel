# Copyright (C) 2024-2026  ilobilo

.include "arch/x86_64/system/cpu.S"

.extern sched_kthread_exit
sched_kthread_trampoline:
    mov rdi, r13
    call r12
    jmp sched_kthread_exit
.global sched_kthread_trampoline

sched_uthread_trampoline:
    # TODO
.global sched_uthread_trampoline

sched_context_switch:
    mov [rdi + 0], rsp
    mov [rdi + 8], rbp
    mov [rdi + 16], rbx
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    pushfq
    pop qword ptr [rdi + 56]

    lea  rcx, [rip + .resume]
    mov [rdi + 64], rcx

    cli

    mov rsp, [rsi + 0]
    mov rbp, [rsi + 8]
    mov rbx, [rsi + 16]
    mov r12, [rsi + 24]
    mov r13, [rsi + 32]
    mov r14, [rsi + 40]
    mov r15, [rsi + 48]
    push qword ptr [rsi + 56]
    popfq

    mov rcx, 0xC0000101
    # rdx already holds the next thread address
    shr rdx, 32
    wrmsr

    jmp [rsi + 64]
.resume:
    ret
.global sched_context_switch

sched_return_to_user:
    # TODO
.global sched_return_to_user
