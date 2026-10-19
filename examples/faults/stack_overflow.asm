; stack_overflow.asm - unbounded recursion runs out of the 1 MiB stack.
;
; Each call uses 12 bytes (return address + saved fp + 4-byte local), so the
; hardware raises StackOverflow after roughly 87,000 nested calls, at the
; exact instruction that would move sp below the stack segment.

        .data
msg:    .asciiz "recursing forever...\n"

        .text
main:
        la      a0, msg
        sys     SYS_PRINT_STR
        li      a0, 0
        call    down
        ret                         ; never reached

down:                               ; void down(int depth) { down(depth + 1); }
        enter   4
        sw      a0, -4(fp)
        addi    a0, a0, 1
        call    down
        leave
        ret
