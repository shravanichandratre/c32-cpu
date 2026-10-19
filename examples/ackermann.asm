; ackermann.asm - the Ackermann function A(m, n) for m = 0..3, n = 0..4.
;
;   A(0, n) = n + 1
;   A(m, 0) = A(m - 1, 1)
;   A(m, n) = A(m - 1, A(m, n - 1))
;
; Shows: deep, non-primitive recursion (A(3,4) alone makes ~10,000 calls)
; plus tail calls: the outer recursive call is a JMP, so it reuses the
; current stack frame instead of growing the stack.

        .data
s_a:     .asciiz "A("
s_comma: .asciiz ", "
s_eq:    .asciiz ") = "

        .text
main:
        li      s0, 0               ; m
m_loop:
        li      s1, 0               ; n
n_loop:
        mov     a0, s0
        mov     a1, s1
        call    ack
        mov     s2, a0

        la      a0, s_a
        sys     SYS_PRINT_STR
        mov     a0, s0
        sys     SYS_PRINT_INT
        la      a0, s_comma
        sys     SYS_PRINT_STR
        mov     a0, s1
        sys     SYS_PRINT_INT
        la      a0, s_eq
        sys     SYS_PRINT_STR
        mov     a0, s2
        sys     SYS_PRINT_INT
        li      a0, '\n'
        sys     SYS_PRINT_CHAR

        inc     s1
        li      t0, 5
        blt     s1, t0, n_loop
        inc     s0
        li      t0, 4
        blt     s0, t0, m_loop

        li      a0, 0
        ret

; int ack(int m = a0, int n = a1)  ->  a0
ack:
        bnez    a0, ack_m
        addi    a0, a1, 1           ; A(0, n) = n + 1
        ret
ack_m:
        bnez    a1, ack_mn
        addi    a0, a0, -1          ; A(m, 0) = A(m - 1, 1)
        li      a1, 1
        jmp     ack                 ; tail call
ack_mn:
        push    a0                  ; save m across the inner call
        addi    a1, a1, -1
        call    ack                 ; a0 = A(m, n - 1)
        mov     a1, a0
        pop     a0
        addi    a0, a0, -1
        jmp     ack                 ; tail call A(m - 1, A(m, n - 1))
