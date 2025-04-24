OUTPUT_FORMAT(elf64-x86-64)

ENTRY(kentry)

PHDRS
{
    limine_requests PT_LOAD;
    text            PT_LOAD FLAGS(RX);
    rodata          PT_LOAD FLAGS(R);
    data            PT_LOAD FLAGS(RW);
}

SECTIONS
{
    . = 0xffffffff80000000;

    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    .text ALIGN(0x1000) : {
        __TEXT_START = .;
        *(.text .text.*)
        __TEXT_END = .;
    } :text

    .rodata ALIGN(0x1000) : {
        __RODATA_START = .;
        *(.rodata .rodata.*)
        __RODATA_END = .;
    } :rodata

    .data ALIGN(0x1000) : {
        __DATA_START = .;
        *(.data .data.*)
        __DATA_END = .;
    } :data

    .bss ALIGN(0x1000) : {
        __BSS_START = .;
        *(.bss .bss.*)
        *(COMMON)
        __BSS_END = .;
    } :bss

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}