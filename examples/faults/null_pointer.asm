; null_pointer.asm - the low 1 MiB is unmapped, so dereferencing NULL traps.

        .text
main:
        li      t0, 0               ; a NULL pointer
        lw      a0, 0(t0)           ; SegmentationFault
        ret
