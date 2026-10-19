; memory_demo.asm - byte vs word access and the segmented address space.
;
; Shows:
;   * little-endian layout: one word read vs. the same four bytes read singly
;   * LB (sign-extending) vs LBU (zero-extending) byte loads
;   * SB changing exactly one byte inside a word
;   * a word array indexed with  index << 2
;   * a recursive strlen walking a string byte by byte
;   * where each segment lives in the address space

        .data
value:     .word 0x11223344
bytes:     .byte 0x80, 0x7F, 0xFF, 0x01
array:     .word 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
message:   .asciiz "segmented memory"
hexdigits: .ascii "0123456789ABCDEF"

s_word:  .asciiz "word  read of value        : "
s_bytes: .asciiz "bytes value+0 .. value+3   : "
s_lb:    .asciiz "LB  of byte 0x80 (signed)  : "
s_lbu:   .asciiz "LBU of byte 0x80 (unsigned): "
s_sb:    .asciiz "after SB 0xAB to value+0   : "
s_sum:   .asciiz "sum of 10-word array       : "
s_len:   .asciiz "strlen(\"segmented memory\") : "
s_text:  .asciiz "text  segment  (main)      : "
s_data:  .asciiz "data  segment  (value)     : "
s_heap:  .asciiz "heap  segment  (break)     : "
s_stack: .asciiz "stack segment  (sp)        : "

        .text
main:
        ; 1) a single 32-bit word read
        la      a0, s_word
        call    puts
        la      s0, value
        lw      a0, 0(s0)
        sys     SYS_PRINT_HEX
        call    newline

        ; 2) the same word, one byte at a time: lowest address = lowest byte
        la      a0, s_bytes
        call    puts
        li      s1, 0
byte_loop:
        add     t0, s0, s1
        lbu     a0, 0(t0)
        call    print_hex8
        li      a0, ' '
        sys     SYS_PRINT_CHAR
        inc     s1
        li      t0, 4
        blt     s1, t0, byte_loop
        call    newline

        ; 3) sign- vs zero-extension of the byte 0x80
        la      s1, bytes
        la      a0, s_lb
        call    puts
        lb      a0, 0(s1)
        sys     SYS_PRINT_INT
        call    newline
        la      a0, s_lbu
        call    puts
        lbu     a0, 0(s1)
        sys     SYS_PRINT_INT
        call    newline

        ; 4) a byte store only touches one byte of the word
        la      a0, s_sb
        call    puts
        li      t0, 0xAB
        sb      t0, 0(s0)
        lw      a0, 0(s0)
        sys     SYS_PRINT_HEX
        call    newline

        ; 5) word array: element i lives at array + i*4
        la      a0, s_sum
        call    puts
        la      s1, array
        li      s2, 0               ; i
        li      t2, 0               ; sum
sum_loop:
        shli    t0, s2, 2
        add     t0, s1, t0
        lw      t1, 0(t0)
        add     t2, t2, t1
        inc     s2
        li      t0, 10
        blt     s2, t0, sum_loop
        mov     a0, t2
        sys     SYS_PRINT_INT
        call    newline

        ; 6) recursive strlen over bytes
        la      a0, s_len
        call    puts
        la      a0, message
        call    strlen
        sys     SYS_PRINT_INT
        call    newline

        ; 7) the four segments
        la      a0, s_text
        call    puts
        la      a0, main
        sys     SYS_PRINT_HEX
        call    newline
        la      a0, s_data
        call    puts
        la      a0, value
        sys     SYS_PRINT_HEX
        call    newline
        la      a0, s_heap
        call    puts
        li      a0, 0
        sys     SYS_SBRK            ; sbrk(0) returns the current break
        sys     SYS_PRINT_HEX
        call    newline
        la      a0, s_stack
        call    puts
        mov     a0, sp
        sys     SYS_PRINT_HEX
        call    newline

        li      a0, 0
        ret

; int strlen(char* p)  - recursive: *p == 0 ? 0 : 1 + strlen(p + 1)
strlen:
        lbu     t0, 0(a0)
        beqz    t0, strlen_end
        addi    a0, a0, 1
        call    strlen
        addi    a0, a0, 1
        ret
strlen_end:
        li      a0, 0
        ret

; print_hex8(a0): print the low byte of a0 as two hex digits (table lookup)
print_hex8:
        mov     t4, a0
        la      t1, hexdigits
        shri    t0, t4, 4
        andi    t0, t0, 0xF
        add     t0, t1, t0
        lbu     a0, 0(t0)
        sys     SYS_PRINT_CHAR
        andi    t0, t4, 0xF
        add     t0, t1, t0
        lbu     a0, 0(t0)
        sys     SYS_PRINT_CHAR
        ret

puts:
        sys     SYS_PRINT_STR
        ret

newline:
        li      a0, '\n'
        sys     SYS_PRINT_CHAR
        ret
