; bubble_sort.asm - sort a word array stored in the data segment.
;
; Shows: nested loops, signed comparisons, pointer arithmetic (i << 2) and
; passing a pointer + length to subroutines.

        .data
array:    .word 64, -3, 25, 12, 22, 11, 90, 0, 7, -15
count:    .word 10
s_before: .asciiz "before: "
s_after:  .asciiz "after:  "

        .text
main:
        la      a0, s_before
        sys     SYS_PRINT_STR
        la      a0, array
        lw      a1, count
        call    print_array

        la      a0, array
        lw      a1, count
        call    bubble_sort

        la      a0, s_after
        sys     SYS_PRINT_STR
        la      a0, array
        lw      a1, count
        call    print_array

        li      a0, 0
        ret

; void bubble_sort(int* a = a0, int n = a1)
bubble_sort:
        addi    t0, a1, -1          ; last = n - 1
outer:
        ble     t0, zero, sorted
        li      t1, 0               ; j = 0
inner:
        bge     t1, t0, inner_done
        shli    t2, t1, 2
        add     t2, a0, t2          ; t2 = &a[j]
        lw      t3, 0(t2)           ; a[j]
        lw      t4, 4(t2)           ; a[j + 1]
        ble     t3, t4, no_swap
        sw      t4, 0(t2)
        sw      t3, 4(t2)
no_swap:
        inc     t1
        b       inner
inner_done:
        dec     t0
        b       outer
sorted:
        ret

; void print_array(int* a = a0, int n = a1)
print_array:
        mov     t0, a0
        li      t1, 0
pa_loop:
        bge     t1, a1, pa_done
        shli    t2, t1, 2
        add     t2, t0, t2
        lw      a0, 0(t2)
        sys     SYS_PRINT_INT
        li      a0, ' '
        sys     SYS_PRINT_CHAR
        inc     t1
        b       pa_loop
pa_done:
        li      a0, '\n'
        sys     SYS_PRINT_CHAR
        ret
