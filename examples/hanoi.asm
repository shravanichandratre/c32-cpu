; hanoi.asm - Towers of Hanoi with 4 disks.
;
;   void hanoi(int n, char from, char to, char via) {
;       if (n == 0) return;
;       hanoi(n - 1, from, via, to);
;       printf("Move disk %d from %c to %c\n", n, from, to);
;       moves++;
;       hanoi(n - 1, via, to, from);
;   }
;
; Shows: a recursive function with four arguments whose values must survive
; two nested calls (kept in an ENTER frame), and a global counter in the
; data segment updated with word loads/stores to a symbolic address.

        .data
moves:   .word 0
s_move:  .asciiz "Move disk "
s_from:  .asciiz " from "
s_to:    .asciiz " to "
s_total: .asciiz "Total moves: "

        .text
main:
        li      a0, 4               ; n = 4 disks
        li      a1, 'A'             ; from
        li      a2, 'C'             ; to
        li      a3, 'B'             ; via
        call    hanoi

        la      a0, s_total
        sys     SYS_PRINT_STR
        lw      a0, moves           ; symbolic address: expands via the `at` register
        sys     SYS_PRINT_INT
        li      a0, '\n'
        sys     SYS_PRINT_CHAR
        li      a0, 0
        ret

; void hanoi(n = a0, from = a1, to = a2, via = a3)
hanoi:
        beqz    a0, hanoi_done
        enter   16                  ; 4 locals: n, from, to, via
        sw      a0, -4(fp)
        sw      a1, -8(fp)
        sw      a2, -12(fp)
        sw      a3, -16(fp)

        addi    a0, a0, -1          ; hanoi(n-1, from, via, to)
        mov     t0, a2
        mov     a2, a3
        mov     a3, t0
        call    hanoi

        la      a0, s_move          ; print the move
        sys     SYS_PRINT_STR
        lw      a0, -4(fp)
        sys     SYS_PRINT_INT
        la      a0, s_from
        sys     SYS_PRINT_STR
        lw      a0, -8(fp)
        sys     SYS_PRINT_CHAR
        la      a0, s_to
        sys     SYS_PRINT_STR
        lw      a0, -12(fp)
        sys     SYS_PRINT_CHAR
        li      a0, '\n'
        sys     SYS_PRINT_CHAR

        lw      t0, moves           ; moves++
        addi    t0, t0, 1
        sw      t0, moves

        lw      a0, -4(fp)          ; hanoi(n-1, via, to, from)
        addi    a0, a0, -1
        lw      a1, -16(fp)
        lw      a2, -12(fp)
        lw      a3, -8(fp)
        call    hanoi
        leave
hanoi_done:
        ret
