OUTPUT_FORMAT(elf64-x86-64)

ENTRY(init)

PHDRS
{
    limine_requests PT_LOAD;
    text    PT_LOAD FLAGS((1<<2) | (1<<0));
    rodata  PT_LOAD FLAGS((1<<2));
    data    PT_LOAD FLAGS((1<<2) | (1<<1));
    bss     PT_LOAD FLAGS((1<<2) | (1<<1));
}

SECTIONS
{
    . = 0xffffffff80000000;

    .limine_requests : {
        __LIMINE_REQ_START = .;
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
        . = ALIGN(0x1000);
        __LIMINE_REQ_END = .;
    } :limine_requests

    .text : {
        __TEXT_START = .;
        *(.text .text.*)
        . = ALIGN(0x1000);
        __TEXT_END = .;
    } :text

    . = ALIGN(0x1000);
    .rodata : {
        __RODATA_START = .;
        *(.rodata .rodata.*)
        . = ALIGN(0x1000);
        __RODATA_END = .;
    } :rodata

    . = ALIGN(0x1000);
    .data : {
        __DATA_START = .;
        *(.data .data.*)

        . = ALIGN(8);
        init_targets_start = .;
        KEEP(*(init_targets))
        init_targets_end = .;

        . = ALIGN(0x1000);
        __DATA_END = .;
    } :data

    . = ALIGN(0x1000);
    .bss : {
        __BSS_START = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(0x1000);
        __BSS_END = .;
    } :bss

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}
