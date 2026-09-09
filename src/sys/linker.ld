ENTRY(_Reset)
SECTIONS
{
    /* VersatilePB load address */
    . = 0x10000;
    
    .startup . : { ../build/startup.o(.text) }
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { 
        __bss_start = .;
        *(.bss* COMMON) 
        __bss_end = .;
    }
    
    . = ALIGN(8);
    . = . + 0x1000; /* 4kB of stack memory */
    stack_top = .;
}
