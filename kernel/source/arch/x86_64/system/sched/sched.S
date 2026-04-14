# Copyright (C) 2024-2026  ilobilo

.include "arch/x86_64/system/cpu.S"

.extern sched_trampoline_entry
.extern sched_kthread_exit

sched_kthread_trampoline:
    call sched_trampoline_entry

    mov rdi, r13
    call r12
    jmp sched_kthread_exit
.global sched_kthread_trampoline

sched_uthread_trampoline:
    call sched_trampoline_entry

    test r12, r12
    jz .no_trampoline

    mov rdi, r13
    jmp r12

.no_trampoline:
    mov rdi, r13
    mov rsi, r15
    jmp sched_return_to_user
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

    mov rsp, [rsi + 0]
    mov rbp, [rsi + 8]
    mov rbx, [rsi + 16]
    mov r12, [rsi + 24]
    mov r13, [rsi + 32]
    mov r14, [rsi + 40]
    mov r15, [rsi + 48]

    mov rcx, 0xC0000101
    mov rax, rdx
    shr rdx, 32
    wrmsr

    push qword ptr [rsi + 56]
    popfq

    jmp [rsi + 64]

.resume:
    ret
.global sched_context_switch

sched_return_to_user:
    push 0x23 # ss
    push rsi # rsp
    pushfq # rflags
    push 0x2B # cs
    push rdi # rip

    cli
    swapgs
    iretq
.global sched_return_to_user
