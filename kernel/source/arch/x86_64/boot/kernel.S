.text

.extern kernel_stack_top
.extern kmain

_start:
    mov rsp, kernel_stack_top
    call kmain
    ud2
.global _start