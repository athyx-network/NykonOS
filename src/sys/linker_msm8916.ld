ENTRY(_Reset)
SECTIONS
{
    /* Qualcomm Snapdragon 410 (MSM8916 / Cortex-A53) RAM Execution Address */
    . = 0x80008000;
    
    .startup . : { ../build_tab/startup.o(.text) }
    .text : { *(.text) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { 
        __bss_start = .;
        *(.bss* COMMON) 
        __bss_end = .;
    }
    
    . = ALIGN(8);
    . = . + 0x20000; /* 128kB of stack memory */
    stack_top = .;
}
