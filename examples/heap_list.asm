; heap_list.asm - a linked list allocated on the heap with SYS_SBRK.
;
;   struct node { int value; struct node* next; };   // 8 bytes
;
; Builds a list of the squares 1..100 by allocating each node with sbrk,
; then walks it iteratively, recursively (printing in reverse) and sums it
; recursively. Finally releases the memory with a negative sbrk.

        .equ    NODE_SIZE, 8
        .equ    VALUE, 0            ; field offsets
        .equ    NEXT, 4

        .data
s_list:  .asciiz "list (head first)    : "
s_rev:   .asciiz "list (recursive, rev): "
s_sum:   .asciiz "sum (recursive)      : "
s_used:  .asciiz "heap bytes in use    : "
s_freed: .asciiz "after sbrk(-used)    : "

        .text
main:
        li      s0, 0               ; head = NULL
        li      s1, 1               ; i = 1
build:
        li      a0, NODE_SIZE
        sys     SYS_SBRK            ; a0 = address of a fresh 8-byte block
        mul     t0, s1, s1
        sw      t0, VALUE(a0)       ; node->value = i * i
        sw      s0, NEXT(a0)        ; node->next  = head
        mov     s0, a0              ; head = node
        inc     s1
        li      t1, 10
        ble     s1, t1, build

        la      a0, s_list
        sys     SYS_PRINT_STR
        mov     a0, s0
        call    print_list
        call    newline

        la      a0, s_rev
        sys     SYS_PRINT_STR
        mov     a0, s0
        call    print_reverse
        call    newline

        la      a0, s_sum
        sys     SYS_PRINT_STR
        mov     a0, s0
        call    list_sum
        sys     SYS_PRINT_INT
        call    newline

        la      a0, s_used
        sys     SYS_PRINT_STR
        li      a0, 0
        sys     SYS_SBRK            ; current break
        li      t0, HEAP_BASE
        sub     s2, a0, t0          ; bytes allocated
        mov     a0, s2
        sys     SYS_PRINT_INT
        call    newline

        neg     a0, s2              ; give it all back
        sys     SYS_SBRK
        la      a0, s_freed
        sys     SYS_PRINT_STR
        li      a0, 0
        sys     SYS_SBRK
        li      t0, HEAP_BASE
        sub     a0, a0, t0
        sys     SYS_PRINT_INT
        call    newline

        li      a0, 0
        ret

; void print_list(node* p) - iterative walk
print_list:
        mov     t0, a0
pl_loop:
        beqz    t0, pl_done
        lw      a0, VALUE(t0)
        sys     SYS_PRINT_INT
        li      a0, ' '
        sys     SYS_PRINT_CHAR
        lw      t0, NEXT(t0)
        b       pl_loop
pl_done:
        ret

; void print_reverse(node* p) - recursion prints the tail first
print_reverse:
        beqz    a0, pr_done
        push    s0
        mov     s0, a0
        lw      a0, NEXT(s0)
        call    print_reverse
        lw      a0, VALUE(s0)
        sys     SYS_PRINT_INT
        li      a0, ' '
        sys     SYS_PRINT_CHAR
        pop     s0
pr_done:
        ret

; int list_sum(node* p) - recursive: p ? p->value + list_sum(p->next) : 0
list_sum:
        beqz    a0, ls_null
        push    s0
        lw      s0, VALUE(a0)
        lw      a0, NEXT(a0)
        call    list_sum
        add     a0, a0, s0
        pop     s0
        ret
ls_null:
        li      a0, 0
        ret

newline:
        li      a0, '\n'
        sys     SYS_PRINT_CHAR
        ret
