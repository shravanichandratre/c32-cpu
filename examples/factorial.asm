; factorial.asm - recursive factorial, fact(0) .. fact(12).
;
;   int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
;
; Shows: CALL/RET (return address pushed on the stack by hardware) and
; ENTER/LEAVE stack frames holding a local variable across the recursive call.
;
; Frame of fact after `enter 4`:
;     fp+4  return address   (pushed by CALL)
;     fp+0  caller's fp      (pushed by ENTER)
;     fp-4  saved n          (local)          <- sp

        .data
s_open:  .asciiz "fact("
s_close: .asciiz ") = "

        .text
main:
        li      s0, 0               ; n = 0
next:
        mov     a0, s0
        call    fact                ; a0 = fact(n)
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
        li      t0, 13
        blt     s0, t0, next        ; 13! overflows 32 bits, so stop at 12

        li      a0, 0
        sys     SYS_EXIT

; int fact(int n)        a0 = n  ->  a0 = n!
fact:
        enter   4                   ; new frame with one 4-byte local
        sw      a0, -4(fp)          ; save n in the frame
        li      t0, 1
        bgt     a0, t0, fact_rec    ; n > 1 -> recurse
        li      a0, 1               ; base case
        leave
        ret
fact_rec:
        addi    a0, a0, -1
        call    fact                ; a0 = fact(n - 1)
        lw      t0, -4(fp)          ; reload n from this frame
        mul     a0, a0, t0          ; n * fact(n - 1)
        leave
        ret
