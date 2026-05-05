# Copyright (C) 2024-2026  ilobilo

.macro user_safe_start
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov rbx, rdi
    mov [rbx], rsp
    lea rax, [rip + 1f]
    mov [rbx + 8], rax
.endm

.macro user_safe_end
2:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.endm

.extern memcpy
user_copy_safe:
    user_safe_start
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    call memcpy
    mov qword ptr [rbx + 8], 0
    xor eax, eax
    jmp 2f
1:
    mov eax, 1
    user_safe_end
.global user_copy_safe

.extern memset
user_fill_safe:
    user_safe_start
    mov rdi, rsi
    mov esi, edx
    mov rdx, rcx
    call memset
    mov qword ptr [rbx + 8], 0
    xor eax, eax
    jmp 2f
1:
    mov eax, 1
    user_safe_end
.global user_fill_safe

.extern strnlen
user_strnlen_safe:
    user_safe_start
    mov rdi, rsi
    mov rsi, rdx
    call strnlen
    mov qword ptr [rbx + 8], 0
    jmp 2f
1:
    mov rax, -1
    user_safe_end
.global user_strnlen_safe
