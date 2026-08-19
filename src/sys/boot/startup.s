.global _Reset
_Reset:
    /* Set up stack pointer to the end of our stack space */
    LDR sp, =stack_top
    /* Call main() in C */
    BL main
    /* Infinite loop if main returns */
    B .
