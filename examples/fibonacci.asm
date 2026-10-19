; fibonacci.asm - naive doubly-recursive Fibonacci, fib(0) .. fib(20).
;
;   int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
;
; Shows: two recursive calls per invocation (about 57,000 calls in total)
; and PUSH/POP to preserve callee-saved registers s0/s1 across them.

        .data
s_open:  .asciiz "fib("
s_close: .asciiz ") = "

        .text
main:
        li      s0, 0
loop:
        mov     a0, s0
        call    fib
        mov     s1, a0

        la      a0, s_open
        sys     SYS_PRINT_STR
        mov     a0, s0
        sys     SYS_PRINT_INT
        la      a0, s_close
        sys     SYS_PRINT_STR
        mov     a0, s1
        sys     SYS_PRINT_INT
        li      a0, '\n'
        sys     SYS_PRINT_CHAR

        inc     s0
        li      t0, 20
        ble     s0, t0, loop

        li      a0, 0
        ret

; int fib(int n)        a0 = n  ->  a0 = fib(n)
fib:
        li      t0, 2
        blt     a0, t0, fib_base    ; n < 2 -> return n
        push    s0                  ; callee-saved registers we are about to use
        push    s1
        mov     s0, a0              ; s0 = n
        addi    a0, s0, -1
        call    fib
        mov     s1, a0              ; s1 = fib(n - 1)
        addi    a0, s0, -2
        call    fib                 ; a0 = fib(n - 2)
        add     a0, a0, s1
        pop     s1                  ; restore in reverse order
        pop     s0
fib_base:
        ret
