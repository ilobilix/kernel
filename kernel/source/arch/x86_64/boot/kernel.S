.text

.extern kernel_stack_top
.extern kmain

_start:
    xor rax, rax
    xor rdx, rdx
    mov ecx, 0xC0000101
    wrmsr

    mov rsp, kernel_stack_top
    call kmain
    ud2
.global _start