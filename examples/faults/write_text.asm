; write_text.asm - self-modifying code is blocked: .text is read + execute only.

        .text
main:
        la      t0, main
        lw      t1, 0(t0)           ; reading code is allowed
        sw      t1, 0(t0)           ; writing it raises WriteViolation
        ret
