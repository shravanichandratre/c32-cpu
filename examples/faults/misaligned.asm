; misaligned.asm - word accesses must be 4-byte aligned (byte accesses need not be).

        .data
buffer: .word 0x11223344, 0x55667788

        .text
main:
        la      t0, buffer
        lbu     a0, 2(t0)           ; byte access at buffer+2: fine
        sys     SYS_PRINT_HEX
        li      a0, '\n'
        sys     SYS_PRINT_CHAR
        lw      a0, 2(t0)           ; word access at buffer+2: MisalignedAccess
        ret
