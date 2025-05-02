.text

.extern kernel_stack_top
.extern kmain

_start:
    mov rsp, kernel_stack_top
    call kmain
    ud2
.global _start

.extern generic_core_entry

cpu_entry:
    mov rax, [rdi + 24]
    mov rcx, [rax]
    mov cr3, rcx

    mov rax, [rax + 8]
    mov rsp, [rax + 8]

    call generic_core_entry
    ud2
.global cpu_entry