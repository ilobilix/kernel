OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

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

    _skernel = .;

    .requests : {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :requests

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .text : {
        _text = .;
        _stext = .;
        *(.text .text.*)
        _etext = .;
    } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    .modules : {
        __start_section_modules = .;
        KEEP(*(.modules))
        __end_section_modules = .;
    } :rodata

    .init_array : {
        PROVIDE_HIDDEN(__start_init_array = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__end_init_array = .);
    } :rodata

    .percpu_init : {
        __start_percpu_init = .;
        *(.percpu_init)
        __end_percpu_init = .;
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
        __start_percpu = .;
        *(.percpu_head)
        *(.percpu)
        . = ALIGN(CONSTANT(MAXPAGESIZE));
        __end_percpu = .;
    } :data

    _ekernel = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}