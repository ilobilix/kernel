PHDRS
{
    null        PT_NULL;
    text        PT_LOAD     FLAGS(5);
    rodata      PT_LOAD     FLAGS(4);
    data        PT_LOAD     FLAGS(6);
    dynamic     PT_DYNAMIC  FLAGS(6);
    modules     0x60000001  FLAGS(4);
}

SECTIONS
{
    .text : {
        *(.init)
        *(.text)
        *(.text.*)
        *(.fini)
        *(.plt)
        *(.plt.*)
    } :text

    .rodata : {
        *(.rodata)
        *(.rodata.*)
        *(.got)
        *(.got.*)
    } :rodata

    .modules : {
        KEEP(*(.modules))
    } :rodata :modules

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :rodata

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } :rodata

    .data : {
        *(.data)
        *(.data.*)
    } :data

    .bss : {
        *(.*bss)
        *(.*bss.*)
        *(COMMON)
    } :data

    .dynsym : {
        *(.dynsym)
    } :data

    .dynstr : {
        *(.dynstr)
    } :data

    .rela.dyn : {
        *(.rela.dyn)
    } :data

    .dynamic : {
        *(.dynamic)
    } :data :dynamic

    /DISCARD/ :
    {
        *(.comment*)
        *(.eh_frame*)
        *(.rel.eh_frame*)
        *(.gcc_except_table*)
        *(.note*)
        *(.interp)
    }
}