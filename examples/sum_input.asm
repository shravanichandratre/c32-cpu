; sum_input.asm - read integers from stdin until EOF; print count/sum/average.
;
; Run:  ./build/c32sim examples/sum_input.asm < examples/inputs/sum_input.txt
;   or  echo "1 2 3" | ./build/c32sim examples/sum_input.asm
;
; Shows: SYS_READ_INT (a0 = value, a1 = 0 ok / 1 end of input) and DIV.

        .data
s_count: .asciiz "count   = "
s_sum:   .asciiz "sum     = "
s_avg:   .asciiz "average = "
s_none:  .asciiz "no numbers were entered\n"

        .text
main:
        li      s0, 0               ; count
        li      s1, 0               ; sum
read_loop:
        sys     SYS_READ_INT
        bnez    a1, done            ; end of input
        add     s1, s1, a0
        inc     s0
        b       read_loop
done:
        beqz    s0, empty
        la      a0, s_count
        sys     SYS_PRINT_STR
        mov     a0, s0
        sys     SYS_PRINT_INT
        li      a0, '\n'
        sys     SYS_PRINT_CHAR

        la      a0, s_sum
        sys     SYS_PRINT_STR
        mov     a0, s1
        sys     SYS_PRINT_INT
        li      a0, '\n'
        sys     SYS_PRINT_CHAR

        la      a0, s_avg
        sys     SYS_PRINT_STR
        div     a0, s1, s0
        sys     SYS_PRINT_INT
        li      a0, '\n'
        sys     SYS_PRINT_CHAR
        li      a0, 0
        ret
empty:
        la      a0, s_none
        sys     SYS_PRINT_STR
        li      a0, 1               ; exit code 1
        ret
