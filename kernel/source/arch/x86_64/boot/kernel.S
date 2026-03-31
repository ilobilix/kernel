.text

.extern __start_percpu
.extern kernel_stack_top
.extern kmain

_start:
    lea rax, [rip + __start_percpu]
    mov [rax], rax
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xC0000101
    wrmsr

    mov rsp, kernel_stack_top
    call kmain
    ud2
.global _start
