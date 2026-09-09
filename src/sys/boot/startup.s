.syntax unified
.global _Reset
.global __bss_start
.global __bss_end

_Reset:
    /* Set up stack pointer */
    LDR sp, =stack_top

#if defined(TARGET_BPI)
    /* Enable CP10 and CP11 (Full Access for VFP/NEON on Cortex-A7) */
    MRC p15, 0, r0, c1, c0, 2
    ORR r0, r0, #(0xF << 20)
    MCR p15, 0, r0, c1, c0, 2
    ISB
    /* Enable VFP / NEON unit via FPEXC */
    MOV r0, #(1 << 30)
    MCR p10, 7, r0, cr8, cr0, 0
#endif

    /* Clear .bss section */
    LDR r0, =__bss_start
    LDR r1, =__bss_end
    MOV r2, #0
1:
    CMP r0, r1
    BGE 2f
    STR r2, [r0], #4
    B 1b
2:

    /* Call main() in C */
    BL main

    /* Infinite loop if main returns */
3:  B 3b
