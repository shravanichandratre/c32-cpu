; exec_data.asm - only the text segment is executable (no code injection).

        .data
payload: .word 0x00000000          ; would decode as HALT, but may not run

        .text
main:
        la      t0, payload
        jr      t0                  ; next fetch raises ExecuteViolation
