# 32-bit CPU simulator with a custom ISA

C32 is a 32-bit processor modelled in C++17. The project includes a custom instruction set, a
segmented, permission-checked memory system and hardware support for stack-based function
calls and recursion.

- **Custom ISA**: 48 fixed-width 32-bit instructions in 4 encoding formats, 16 registers.
  The emulator runs a full **fetch → decode → execute** cycle per instruction.
- **Segmented memory**: text, data, heap and stack, 1 MiB each. Accesses are **byte** or
  **word** wide, alignment-checked and permission-checked (`r-x`, `rw-`).
- **Recursion support**: `CALL`/`RET` push and pop return addresses on the stack inside the
  execute stage.
