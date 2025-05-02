.text

.extern kernel_stack_top
.extern kmain

_start:
    ldr x0, = kernel_stack_top
    mov sp, x0
    bl kmain
    udf 0xDEAD
.global _start

.extern generic_core_entry

cpu_entry:
    ldr x1, [x0, 24]

    ldr x0, [x1, 0]
    msr ttbr1_el1, x0

    ldr x0, [x1, 8]
    ldr x1, [x0, 8]
    mov sp, x1

    bl generic_core_entry
    udf 0xDEAD
.global cpu_entry