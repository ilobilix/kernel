# Copyright (C) 2024-2025  ilobilo

load_early:
    lgdt [rdi]

    mov ss, si
    mov si, 0
    mov ds, si
    mov fs, si
    mov gs, si
    mov es, si

    push rdx
    lea rax, [rip + 1f]
    push rax
    retfq
    1:
    ret
.global load_early

load:
    call load_early
    ltr cx
    ret
.global load