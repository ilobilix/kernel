.text

.extern kernel_stack_top
.extern kmain

_start:
    ldr x0, = kernel_stack_top
    mov sp, x0
    bl kmain
    udf 0xDEAD
.global _start