; hello.asm - the smallest useful C32 program.
;
; Shows: a string in the .data segment, LA (load address), a system call,
; and returning from main (which halts the machine with exit code a0).

        .data
greeting:
        .asciiz "Hello from the C32 CPU!\n"

        .text
main:
        la      a0, greeting        ; a0 = address of the string (data segment)
        sys     SYS_PRINT_STR       ; print it
        li      a0, 0               ; exit code 0
        ret                         ; return from main -> machine halts
