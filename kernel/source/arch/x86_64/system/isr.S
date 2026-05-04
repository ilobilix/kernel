# Copyright (C) 2024-2026  ilobilo

.include "arch/x86_64/system/cpu.S"

.extern isr_handler
isr_common_stub:
    cld

    test qword ptr [rsp + 24], 0x03
    je 1f
    swapgs
1:

    pushall
    mov rdi, rsp
    call isr_handler
    popall
    add rsp, 16

    test qword ptr [rsp + 8], 0x03
    je 1f
    swapgs
1:

    iretq

# we can't trust saved cs in nmi because it may fire between swapgs and iretq
# so we read gsbase directly and test if it's a kernel or user address
# r15 is used to indicate whether we swapped or not
isr_nmi_stub:
    cld

    push rax
    push rcx
    push rdx
    mov ecx, 0xC0000101
    rdmsr
    test edx, 0x80000000
    pop rdx
    pop rcx
    pop rax

    pushall

    jnz 1f
    swapgs
    mov r15, 1
    jmp 2f
1:
    mov r15, 0
2:

    mov rdi, rsp
    call isr_handler

    test r15, r15
    jz 1f
    swapgs
1:

    popall
    add rsp, 16

    iretq

.macro isr number
    isr_\number:
.if !(\number == 8 || (\number >= 10 && \number <= 14) || \number == 17 || \number == 21 || \number == 29 || \number == 30)
    push 0
.endif
    push \number
.if \number == 2
    jmp isr_nmi_stub
.else
    jmp isr_common_stub
.endif
.endm

.altmacro
.macro isr_insert number
    .section .text
    isr \number

    .section .data
    .quad isr_\number
.endm

.section .data
isr_table:
.set i, 0
.rept 256
    isr_insert %i
    .set i, i + 1
.endr
.global isr_table
