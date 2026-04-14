OUTPUT_FORMAT(elf64-aarch64)
OUTPUT_ARCH(elf64-littleaarch64:aarch64)
ENTRY(_start)

PHDRS
{
    requests PT_LOAD;
    text     PT_LOAD;
    rodata   PT_LOAD;
    data     PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;

    .requests : {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .modules : {
        __section_modules_start = .;
        KEEP(*(.modules))
        __section_modules_end = .;
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :rodata

    .percpu_init : {
        __percpu_init_start = .;
        *(.percpu_init)
        __percpu_init_end = .;
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .percpu : {
        __percpu_start = .;
        *(.percpu_head)
        *(.percpu)
        . = ALIGN(CONSTANT(MAXPAGESIZE));
        __percpu_end = .;
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}