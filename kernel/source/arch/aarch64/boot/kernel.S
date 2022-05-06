.text

.extern kernel_stack_top
.extern kmain

_start:
    ldr x0, = kernel_stack_top
    mov sp, x0
    bl kmain
    udf 0xDEAD
.global _start

.extern arch_core_entry

cpu_entry:
    ldr x1, [x0, 24]
    ldr x0, [x1, 24]
    mov sp, x0
    bl arch_core_entry
    udf 0xDEAD
.global cpu_entry