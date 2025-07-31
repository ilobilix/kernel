; Copyright (C) 2024-2025  ilobilo

; based on https://github.com/limine-bootloader/limine/blob/7952e9c79a1ee9e6536b366bf8fc42a662b3ba92/common/sys/smp_trampoline.asm_x86

bits 16
section .rodata
global smp_trampoline_start
smp_trampoline_start:
    cli
    cld

    mov ebx, cs
    shl ebx, 4

    lea eax, [ebx + (temp_gdt - smp_trampoline_start)]
    mov [cs:(temp_gdtr.offset - smp_trampoline_start)], eax

    o32 lidt [cs:(invalid_idt - smp_trampoline_start)]
    o32 lgdt [cs:(temp_gdtr - smp_trampoline_start)]

    lea eax, [ebx + (.mode32 - smp_trampoline_start)]
    mov [cs:(.farjmp_off - smp_trampoline_start)], eax

    mov eax, 0x00000011
    mov cr0, eax
    o32 jmp far [cs:(.farjmp - smp_trampoline_start)]

  .farjmp:
    .farjmp_off: dd 0
    .farjmp_seg: dd 0x18

bits 32
  .mode32:
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
    mov eax, 0x00070406
    mov edx, 0x00000105
    wrmsr
  .no_pat:
    mov ebx, esi

    mov ecx, 0x1b
    mov eax, [ebx + (passed_info.bsp_apic_addr_msr_lo - smp_trampoline_start)]
    mov edx, [ebx + (passed_info.bsp_apic_addr_msr_hi - smp_trampoline_start)]
    bts eax, 11
    btr eax, 8
    wrmsr

  .nox2apic:
    mov esp, [ebx + (passed_info.temp_stack - smp_trampoline_start)]

    mov eax, cr4
    bts eax, 5
    mov cr4, eax

    mov ecx, 0xc0000080
    mov eax, 0x100
    xor edx, edx
    wrmsr

    ; mov eax, cr4
    ; bts eax, 12
    ; mov cr4, eax

    mov eax, dword [ebx + (passed_info.pagemap - smp_trampoline_start)]
    mov cr3, eax

    mov eax, cr0
    bts eax, 31
    mov cr0, eax

    lea eax, [ebx + (.mode64 - smp_trampoline_start)]
    push 0x28
    push eax
    retf

bits 64
  .mode64:
    mov ax, 0x30
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov ebx, ebx

    ; nx
    mov ecx, 0xc0000080
    rdmsr
    bts eax, 11
    wrmsr

    ; wp
    mov rax, cr0
    bts rax, 16
    mov cr0, rax

    mov ebx, ebx

    ; synchronise MTRRs with BSP
    call [rbx + (passed_info.mtrr_restore - smp_trampoline_start)]

    mov rdi, qword [rbx + (passed_info.proc - smp_trampoline_start)]

    mov rax, 1
    xchg qword [rbx + (passed_info.booted_flag - smp_trampoline_start)], rax

    mov rsp, qword [rdi + 8]
    jmp qword [rbx + (passed_info.jump_addr - smp_trampoline_start)]
    ud2

invalid_idt: times 2 dq 0

align 16
temp_gdt:
    dq 0x0000000000000000
    dq 0x00009B000000FFFF
    dq 0x000093000000FFFF
    dq 0x00CF9B000000FFFF
    dq 0x00CF93000000FFFF
    dq 0x00209B0000000000
    dq 0x0000930000000000
temp_gdt_end:

temp_gdtr:
    .length:
        dw temp_gdt_end - temp_gdt - 1
    .offset:
        dq 0

align 16
passed_info:
    .booted_flag:
        dq 0
    .pagemap:
        dq 0
    .bsp_apic_addr_msr_lo:
        dd 0
    .bsp_apic_addr_msr_hi:
        dd 0
    .mtrr_restore:
        dq 0
    .temp_stack:
        dq 0
    .proc:
        dq 0
    .jump_addr:
        dq 0

smp_trampoline_end:

global smp_trampoline_size
smp_trampoline_size: dq smp_trampoline_end - smp_trampoline_start

section .note.GNU-stack progbits