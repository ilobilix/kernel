# Copyright (C) 2024-2025  ilobilo

# memcpy:
#     mov rcx, rdx
#     mov rax, rdi
#     rep movsb
#     ret
# .global memcpy

# memset:
#     push rdi
#     mov rax, rsi
#     mov rcx, rdx
#     rep stosb
#     pop rax
#     ret
# .global memset

memmove:
    mov rcx, rdx
    mov rax, rdi

    cmp rdi, rsi
    ja 1f

    rep movsb
    jmp 2f

1:
    lea rdi, [rdi + rcx - 1]
    lea rsi, [rsi + rcx - 1]
    std
    rep movsb
    cld

2:
    ret
.global memmove

memcmp:
    test rdx, rdx
    je 2f

    xor eax, eax
    nop word ptr [rax + rax]

1:
    movzx ecx, byte ptr [rsi + rax]
    cmp byte ptr [rdi + rax], cl
    jne 3f

    add rax, 1
    cmp rdx, rax
    jne 1b

2:
    xor eax, eax
    ret

3:
    setae al
    movzx eax, al
    add eax, eax
    add eax, -1
    ret
.global memcmp