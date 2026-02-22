# Copyright (C) 2024-2026  ilobilo

#if ILOBILIX_LIMINE_MP

.extern generic_mp_entry
.global mp_entry
mp_entry:
    ldr x1, [x0, 24]

    ldr x2, [x1, 0]
    msr ttbr1_el1, x2

    ldr x2, [x1, 8]
    ldr x1, [x2, 8]
    mov sp, x1

    bl generic_mp_entry
    udf 0xDEAD

#else
#  error mp without limine is not supported
#endif

.section ".note.GNU-stack", "", @progbits