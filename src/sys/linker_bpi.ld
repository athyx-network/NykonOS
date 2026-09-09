ENTRY(_Reset)
SECTIONS
{
    /* Banana Pi M2 Zero (Allwinner H2+/H3) DDR3 Execution Address */
    . = 0x42000000;
    
    .startup . : { ../build_bpi/startup.o(.text) }
    .text : { *(.text) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { 
        __bss_start = .;
        *(.bss* COMMON) 
        __bss_end = .;
    }
    
    . = ALIGN(8);
    . = . + 0x10000; /* 64kB of stack memory */
    stack_top = .;
}
