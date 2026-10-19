; divide_by_zero.asm - integer division by zero raises a hardware fault.

        .text
main:
        li      t0, 42
        li      t1, 0
        div     a0, t0, t1          ; DivideByZero
        ret
