.text

.extern __start_percpu
.extern kernel_stack_top
.extern kmain

_start:
    adrp x0, __start_percpu
    add x0, x0, :lo12:__start_percpu
    str x0, [x0]
    msr tpidr_el1, x0

    ldr x0, = kernel_stack_top
    mov sp, x0
    bl kmain
    udf 0xDEAD
.global _start
