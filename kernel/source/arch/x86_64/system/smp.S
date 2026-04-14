# Copyright (C) 2024-2025  ilobilo

#if ILOBILIX_LIMINE_MP

.code64
.extern generic_mp_entry
.global mp_entry
mp_entry:
    mov rax, [rdi + 24]
    mov rcx, [rax]
    mov cr3, rcx

    mov rax, [rax + 8]
    mov rsp, [rax + 8]

    call generic_mp_entry
    ud2

#else

# based on https://github.com/limine-bootloader/limine/blob/7952e9c79a1ee9e6536b366bf8fc42a662b3ba92/common/sys/smp_trampoline.asm_x86

#define SETX(name) .name: .set .L ## name, (.name - smp_trampoline_start);
#define SET(name) name: .set .L ## name, (name - smp_trampoline_start);

.code16
.section .rodata
.global smp_trampoline_start
smp_trampoline_start:
    cli
    cld

    mov ebx, cs
    shl ebx, 4

    lea eax, [ebx + .Ltemp_gdt]
    mov cs:[.Ltemp_gdtr.offset], eax

    lidt cs:[.Linvalid_idt]
    lgdt cs:[.Ltemp_gdtr]

    lea eax, [ebx + .Lmode32]
    mov cs:[.Lfarjmp_off], eax

    mov eax, 0x00000011
    mov cr0, eax

    jmp fword ptr cs:[.Lfarjmp]

  SETX(farjmp)
    SETX(farjmp_off) .int 0
    .farjmp_seg: .int 0x18

.code32
  SETX(mode32)
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    xor eax, eax
    lldt ax

    xor eax, eax
    mov cr4, eax

    mov esi, ebx
    mov eax, 1
    xor ecx, ecx
    cpuid
    test edx, 1 << 16
    jz .no_pat
    mov ecx, 0x277
    # x86_64/system/cpu.cppm: cpu::pat::boot_state
    mov eax, 0x00070406
    mov edx, 0x00000105
    wrmsr
  .no_pat:
    mov ebx, esi

    mov ecx, 0x1b
    mov eax, [ebx + .Lpassed_info.bsp_apic_addr_msr_lo]
    mov edx, [ebx + .Lpassed_info.bsp_apic_addr_msr_hi]
    bts eax, 11
    btr eax, 8
    wrmsr

  .nox2apic:
    mov esp, [ebx + .Lpassed_info.temp_stack]

    mov eax, cr4
    bts eax, 5
    mov cr4, eax

    mov ecx, 0xc0000080
    mov eax, 0x100
    xor edx, edx
    wrmsr

    # 5 level paging
    # mov eax, cr4
    # bts eax, 12
    # mov cr4, eax

    mov eax, dword ptr [ebx + .Lpassed_info.early_pagemap]
    mov cr3, eax

    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    lea eax, [ebx + .Lmode64]
    push 0x28
    push eax
    retf

.code64
  SETX(mode64)
    mov ax, 0x30
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov ebx, ebx

    # nx
    mov ecx, 0xc0000080
    rdmsr
    bts eax, 11
    wrmsr

    # wp
    mov rax, cr0
    bts rax, 16
    mov cr0, rax

    mov ebx, ebx

    # synchronise MTRRs with BSP
    call [rbx + .Lpassed_info.mtrr_restore]

    mov rax, qword ptr [rbx + .Lpassed_info.pagemap]
    mov cr3, rax

    mov rdi, qword ptr [rbx + .Lpassed_info.proc]

    mov rax, 1
    xchg qword ptr [rbx + .Lpassed_info.booted_flag], rax

    mov rsp, qword ptr [rdi + 8]
    jmp qword ptr [rbx + .Lpassed_info.jump_addr]
    ud2

SET(invalid_idt)
    .quad 0
    .quad 0

.balign 16
SET(temp_gdt)
    .quad 0x0000000000000000
    .quad 0x00009B000000FFFF
    .quad 0x000093000000FFFF
    .quad 0x00CF9B000000FFFF
    .quad 0x00CF93000000FFFF
    .quad 0x00209B0000000000
    .quad 0x0000930000000000
temp_gdt_end:

SET(temp_gdtr)
    .temp_gdtr.length: .short temp_gdt_end - temp_gdt - 1
    SETX(temp_gdtr.offset) .quad 0

.balign 16
passed_info:
    SETX(passed_info.booted_flag) .quad 0
    SETX(passed_info.early_pagemap) .quad 0
    SETX(passed_info.pagemap) .quad 0
    SETX(passed_info.bsp_apic_addr_msr_lo) .int 0
    SETX(passed_info.bsp_apic_addr_msr_hi) .int 0
    SETX(passed_info.mtrr_restore) .quad 0
    SETX(passed_info.temp_stack) .quad 0
    SETX(passed_info.proc) .quad 0
    SETX(passed_info.jump_addr) .quad 0

smp_trampoline_end:

.global smp_trampoline_size
smp_trampoline_size: .quad smp_trampoline_end - smp_trampoline_start

#endif

.section ".note.GNU-stack", "", @progbits