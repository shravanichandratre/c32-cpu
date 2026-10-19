; heap_overflow.asm - heap memory is only valid below the program break.

        .text
main:
        li      a0, 16
        sys     SYS_SBRK            ; allocate 16 bytes: a0 = block address
        mov     t0, a0
        li      t1, 7
        sw      t1, 12(t0)          ; last word of the block: fine
        sw      t1, 16(t0)          ; one word past the end: HeapViolation
        ret
