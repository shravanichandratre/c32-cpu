# 32-bit CPU simulator with a custom ISA

C32 is a 32-bit processor modelled in C++17. The project includes a custom instruction set, a
segmented, permission-checked memory system and hardware support for stack-based function
calls and recursion.

- **Custom ISA**: 48 fixed-width 32-bit instructions in 4 encoding formats, 16 registers.
  The emulator runs a full **fetch → decode → execute** cycle per instruction.
- **Segmented memory**: text, data, heap and stack, 1 MiB each. Accesses are **byte** or
  **word** wide, alignment-checked and permission-checked (`r-x`, `rw-`).
- **Recursion support**: `CALL`/`RET` push and pop return addresses on the stack inside the
  execute stage. `ENTER`/`LEAVE` build stack frames, and the stack pointer is bounds-checked
  so runaway recursion raises a precise `StackOverflow` fault.

```
$ ./build/c32sim --stats examples/fibonacci.asm
fib(0) = 0
...
fib(20) = 6765
---- execution statistics ----
  instructions      : 487239
  cycles (model)    : 860041  (CPI 1.77)
  calls / returns   : 57291 / 57291
  max call depth    : 20
  peak stack usage  : 236 bytes (lowest sp 0x004FFF14)
```

---

## Contents

1. [Quick start](#1-quick-start)
2. [Project layout](#2-project-layout)
3. [Architecture overview](#3-architecture-overview)
4. [Instruction Set Architecture](#4-instruction-set-architecture)
5. [Memory model and segmentation](#5-memory-model-and-segmentation)
6. [Function calls, the stack and recursion](#6-function-calls-the-stack-and-recursion)
7. [The fetch–decode–execute cycle](#7-the-fetchdecodeexecute-cycle)
8. [Faults (hardware exceptions)](#8-faults-hardware-exceptions)
9. [Assembly language reference](#9-assembly-language-reference)
10. [Tools: assembler, simulator, debugger, disassembler](#10-tools)
11. [The .c32 binary format](#11-the-c32-binary-format)
12. [Testing](#12-testing)
13. [Design decisions](#13-design-decisions)
14. [Extending the ISA](#14-extending-the-isa)
15. [Limitations and future work](#15-limitations-and-future-work)

---

## 1. Quick start

Requirements: a C++17 compiler (g++ ≥ 7 / clang++ ≥ 6 / MSVC 2019), plus `make` **or**
CMake ≥ 3.14. No third-party libraries. `requirements.txt` has full step-by-step instructions.

```bash
make                                   # build tools into ./build
make test                              # 35 unit tests + 16 example programs
./build/c32sim examples/factorial.asm  # run a program (assembled on the fly)
```

With CMake instead:

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake -j
ctest --test-dir build-cmake --output-on-failure
```

More things to try:

```bash
./build/c32sim --trace examples/factorial.asm         # every instruction and its effects
./build/c32sim --debug examples/hanoi.asm             # interactive debugger
./build/c32asm examples/hanoi.asm -o hanoi.c32 -l     # assemble + listing
./build/c32sim hanoi.c32                              # run the binary
./build/c32dis --isa                                  # opcode reference
./build/c32sim examples/faults/stack_overflow.asm     # watch the stack guard fire
```

---

## 2. Project layout

```
c32-cpu/
├── include/c32/
│   ├── layout.hpp        memory map constants (segment bases, 1 MiB size, exit trap)
│   ├── isa.hpp           opcodes, formats, Instruction, encode/decode, registers, syscalls
│   ├── fault.hpp         FaultType + CpuFault exception
│   ├── memory.hpp        segmented memory (byte/word access, permissions, heap break)
│   ├── cpu.hpp           CPU: registers, PC, fetch/decode/execute, stats, tracing
│   ├── program.hpp       Program image, .c32 file format, loader
│   ├── assembler.hpp     two-pass assembler
│   ├── disassembler.hpp  disassembly, listings, ISA reference
│   └── util.hpp          formatting helpers
├── src/                  implementations of the above (libc32.a)
├── tools/
│   ├── c32asm.cpp        assembler CLI      (.asm -> .c32)
│   ├── c32sim.cpp        simulator CLI      (run, trace, stats, debugger)
│   └── c32dis.cpp        disassembler CLI   (.c32 -> listing, --isa)
├── tests/test_main.cpp   35 unit tests (self-contained framework)
├── examples/
│   ├── *.asm             9 example programs     expected/  recorded outputs
│   ├── inputs/           stdin for examples that read input
│   └── faults/           7 programs that deliberately trigger each protection fault
├── scripts/run_examples.sh   runs every example and diffs against expected output
├── Makefile, CMakeLists.txt
├── requirements.txt      system requirements + step-by-step run guide
└── README.md
```

The core is a static library (`libc32.a`) with no I/O assumptions. The CPU takes
`std::istream&`/`std::ostream&`, so the unit tests run whole programs against string streams.

---

## 3. Architecture overview

```
              ┌──────────────────────────── CPU ─────────────────────────────┐
              │                                                              │
   ┌──────┐   │  ┌─────────┐  raw   ┌──────────┐  Instruction  ┌───────────┐ │
   │  PC  │───┼─▶│  FETCH  │──────▶ │  DECODE  │─────────────▶ │  EXECUTE  │ │
   └──────┘   │  └────┬────┘ word   └──────────┘ op,rd,rs,imm  └─┬───┬───┬─┘ │
      ▲       │       │ fetch32 (needs X permission)             │   │   │   │
      │       │       │                                ALU ◀─────┘   │   │   │
      │       │       │       ┌───────────────────┐  load/store ◀────┘   │   │
      └───────┼───────┼───────┤  next PC / branch │  CALL/RET/PUSH/POP ◀─┘   │
              │       │       └───────────────────┘         │                │
              │  ┌────┴─────────────────────────────────────┴───────┐        │
              │  │ Register file: r0(zero) a0-a3 t0-t4 s0-s2 fp sp at│        │
              │  └──────────────────────────────────────────────────┘        │
              └──────────────┬───────────────────────────────────────────────┘
                             │ read8/read32/write8/write32/fetch32
              ┌──────────────▼───────────────────────────────────────────────┐
              │ MEMORY  (segment table: bounds + r/w/x + alignment + brk)    │
              │  TEXT r-x │ DATA rw- │ HEAP rw- (< brk) │ STACK rw- (sp-guard)│
              └──────────────────────────────────────────────────────────────┘
```

---

## 4. Instruction Set Architecture

### 4.1 Registers

16 general-purpose 32-bit registers plus a program counter. Every register has an ABI name;
both `r5` and `t0` are accepted.

| Register  | ABI name   | Role                                  | Saved by |
|-----------|------------|---------------------------------------|----------|
| r0        | `zero`     | always reads 0, writes are discarded  | —        |
| r1        | `a0`       | argument 0 **and return value**       | caller   |
| r2 – r4   | `a1`–`a3`  | arguments 1–3                         | caller   |
| r5 – r9   | `t0`–`t4`  | temporaries                           | caller   |
| r10 – r12 | `s0`–`s2`  | saved registers                       | callee   |
| r13       | `fp`       | frame pointer (set by `ENTER`)        | callee   |
| r14       | `sp`       | stack pointer (hardware bounds-checked) | —      |
| r15       | `at`       | assembler temporary (used by symbolic loads/stores) | — |
| —         | `pc`       | program counter (not directly addressable) | —   |

There is no flags register. Comparisons are folded into branches (`BLT rs1, rs2, label`) or
produce 0/1 in a register (`SLT`). This removes hidden state from the pipeline.

### 4.2 Instruction formats

Every instruction is exactly **32 bits**, little-endian in memory. The opcode is always the
top 6 bits.

```
        31      26 25   22 21   18 17   14 13                       0
   R   | opcode  |   a   |   b   |   c   |        (unused, 0)       |
   I   | opcode  |   a   |   b   |              imm18               |
   U   | opcode  |   a   |                 imm22                    |
   J   | opcode  |                      imm26                       |
```

How the generic fields `a`/`b`/`c` map to operands depends on the instruction:

| Operand shape          | Format | a      | b      | c     | immediate                       |
|------------------------|--------|--------|--------|-------|---------------------------------|
| `rd, rs1, rs2`         | R      | rd     | rs1    | rs2   | —                               |
| `rd, rs1`  (`NOT`)     | R      | rd     | rs1    | —     | —                               |
| `rs1` (`PUSH JR CALLR`)| R      | rs1    | —      | —     | —                               |
| `rd`  (`POP`)          | R      | rd     | —      | —     | —                               |
| `rd, rs1, imm`         | I      | rd     | rs1    |       | imm18 (signed or zero-extended) |
| `rd, imm(rs1)` loads   | I      | rd     | rs1    |       | signed byte offset              |
| `rs2, imm(rs1)` stores | I      | rs2    | rs1    |       | signed byte offset              |
| `rs1, rs2, label`      | I      | rs1    | rs2    |       | signed offset in *instructions* |
| `imm` (`SYS ENTER`)    | I      | —      | —      |       | unsigned imm18                  |
| `rd, imm22` (`LUI`)    | U      | rd     |        |       | imm22                           |
| `label` (`JMP CALL`)   | J      |        |        |       | target address >> 2             |

Example: `addi s0, zero, 0` → opcode `0x12`, a=10, b=0, imm=0 → **`0x4A800000`**.

### 4.3 Opcode table

Run `./build/c32dis --isa` to print this table from the code itself.

| Op   | Mnemonic | Fmt | Operands           | Cycles | Semantics |
|------|----------|-----|--------------------|-------:|-----------|
| 0x00 | `HALT`   | –   |                    | 1  | stop the machine (exit code 0) |
| 0x01 | `NOP`    | –   |                    | 1  | no operation |
| 0x02 | `ADD`    | R   | rd, rs1, rs2       | 1  | rd = rs1 + rs2 |
| 0x03 | `SUB`    | R   | rd, rs1, rs2       | 1  | rd = rs1 − rs2 |
| 0x04 | `MUL`    | R   | rd, rs1, rs2       | 3  | rd = low 32 bits of rs1 × rs2 |
| 0x05 | `DIV`    | R   | rd, rs1, rs2       | 12 | signed divide (truncates toward 0; traps on 0) |
| 0x06 | `DIVU`   | R   | rd, rs1, rs2       | 12 | unsigned divide (traps on 0) |
| 0x07 | `REM`    | R   | rd, rs1, rs2       | 12 | signed remainder (traps on 0) |
| 0x08 | `REMU`   | R   | rd, rs1, rs2       | 12 | unsigned remainder (traps on 0) |
| 0x09 | `AND`    | R   | rd, rs1, rs2       | 1  | bitwise and |
| 0x0A | `OR`     | R   | rd, rs1, rs2       | 1  | bitwise or |
| 0x0B | `XOR`    | R   | rd, rs1, rs2       | 1  | bitwise xor |
| 0x0C | `SHL`    | R   | rd, rs1, rs2       | 1  | rd = rs1 << (rs2 & 31) |
| 0x0D | `SHR`    | R   | rd, rs1, rs2       | 1  | logical shift right |
| 0x0E | `SRA`    | R   | rd, rs1, rs2       | 1  | arithmetic shift right |
| 0x0F | `SLT`    | R   | rd, rs1, rs2       | 1  | rd = (rs1 < rs2) signed ? 1 : 0 |
| 0x10 | `SLTU`   | R   | rd, rs1, rs2       | 1  | rd = (rs1 < rs2) unsigned ? 1 : 0 |
| 0x11 | `NOT`    | R   | rd, rs1            | 1  | rd = ~rs1 |
| 0x12 | `ADDI`   | I   | rd, rs1, imm       | 1  | rd = rs1 + sext(imm18) |
| 0x13 | `ANDI`   | I   | rd, rs1, imm       | 1  | rd = rs1 & zext(imm18) |
| 0x14 | `ORI`    | I   | rd, rs1, imm       | 1  | rd = rs1 \| zext(imm18) |
| 0x15 | `XORI`   | I   | rd, rs1, imm       | 1  | rd = rs1 ^ zext(imm18) |
| 0x16 | `SHLI`   | I   | rd, rs1, 0..31     | 1  | shift left by immediate |
| 0x17 | `SHRI`   | I   | rd, rs1, 0..31     | 1  | logical shift right by immediate |
| 0x18 | `SRAI`   | I   | rd, rs1, 0..31     | 1  | arithmetic shift right by immediate |
| 0x19 | `SLTI`   | I   | rd, rs1, imm       | 1  | rd = (rs1 < sext(imm18)) ? 1 : 0 |
| 0x1A | `LUI`    | U   | rd, imm22          | 1  | rd = imm22 << 10 |
| 0x1B | `LW`     | I   | rd, imm(rs1)       | 2  | load **word** (address must be 4-aligned) |
| 0x1C | `LB`     | I   | rd, imm(rs1)       | 2  | load **byte**, sign-extend |
| 0x1D | `LBU`    | I   | rd, imm(rs1)       | 2  | load **byte**, zero-extend |
| 0x1E | `SW`     | I   | rs2, imm(rs1)      | 2  | store **word** (address must be 4-aligned) |
| 0x1F | `SB`     | I   | rs2, imm(rs1)      | 2  | store low **byte** |
| 0x20 | `BEQ`    | I   | rs1, rs2, label    | 1+ | branch if equal |
| 0x21 | `BNE`    | I   | rs1, rs2, label    | 1+ | branch if not equal |
| 0x22 | `BLT`    | I   | rs1, rs2, label    | 1+ | branch if less (signed) |
| 0x23 | `BGE`    | I   | rs1, rs2, label    | 1+ | branch if greater/equal (signed) |
| 0x24 | `BLTU`   | I   | rs1, rs2, label    | 1+ | branch if less (unsigned) |
| 0x25 | `BGEU`   | I   | rs1, rs2, label    | 1+ | branch if greater/equal (unsigned) |
| 0x26 | `JMP`    | J   | label              | 2  | pc = target |
| 0x27 | `JR`     | R   | rs1                | 2  | pc = rs1 |
| 0x28 | `CALL`   | J   | label              | 3  | push(pc+4); pc = target |
| 0x29 | `CALLR`  | R   | rs1                | 3  | push(pc+4); pc = rs1 (function pointers) |
| 0x2A | `RET`    | –   |                    | 3  | pc = pop() |
| 0x2B | `PUSH`   | R   | rs1                | 2  | sp −= 4; mem32[sp] = rs1 |
| 0x2C | `POP`    | R   | rd                 | 2  | rd = mem32[sp]; sp += 4 |
| 0x2D | `ENTER`  | I   | imm                | 3  | push(fp); fp = sp; sp −= imm |
| 0x2E | `LEAVE`  | –   |                    | 3  | sp = fp; fp = pop() |
| 0x2F | `SYS`    | I   | imm                | 5  | system call number imm |

Branch target = `pc + 4 + imm × 4` (±512 KiB reach). Jump target = `imm26 << 2` (covers the
whole text segment). Opcodes 0x30–0x3F are unassigned and raise `IllegalInstruction`.

### 4.4 Pseudo-instructions (expanded by the assembler)

| Pseudo                 | Expansion                                         |
|------------------------|---------------------------------------------------|
| `LI rd, imm32`         | `ADDI rd, zero, imm` if it fits in 18 bits, else `LUI rd, imm>>10` + `ORI rd, rd, imm & 0x3FF` |
| `LA rd, label`         | always `LUI` + `ORI` (any 32-bit address)         |
| `MOV rd, rs`           | `ADD rd, rs, zero`                                |
| `NEG rd, rs`           | `SUB rd, zero, rs`                                |
| `INC rd` / `DEC rd`    | `ADDI rd, rd, ±1`                                 |
| `B label`              | `JMP label`                                       |
| `BEQZ rs, l` / `BNEZ rs, l` | `BEQ/BNE rs, zero, l`                        |
| `BGT a, b, l` / `BLE a, b, l` | `BLT b, a, l` / `BGE b, a, l` (operands swapped) |
| `BGTU` / `BLEU`        | unsigned versions of the above                    |
| `LW rd, label` (also `LB LBU SW SB`) | `LUI at` + `ORI at` + `LW rd, 0(at)` (symbolic address) |

### 4.5 System calls (`SYS n`)

Arguments go in `a0`, results come back in `a0`. A system call modifies only `a0` (and `a1`
for `READ_INT`). The assembler predefines a `SYS_*` constant for every number.

| n | Name              | Behaviour |
|---|-------------------|-----------|
| 0 | `SYS_EXIT`        | halt with exit code `a0` |
| 1 | `SYS_PRINT_INT`   | print `a0` as signed decimal |
| 2 | `SYS_PRINT_CHAR`  | print the low byte of `a0` |
| 3 | `SYS_PRINT_STR`   | print the NUL-terminated string at address `a0` |
| 4 | `SYS_READ_INT`    | `a0` = integer read from stdin; `a1` = 0 ok, 1 end of input |
| 5 | `SYS_SBRK`        | grow/shrink the heap by `a0` bytes; `a0` = old break (−1 on failure) |
| 6 | `SYS_PRINT_HEX`   | print `a0` as `0xXXXXXXXX` |
| 7 | `SYS_PRINT_UINT`  | print `a0` as unsigned decimal |
| 8 | `SYS_READ_CHAR`   | `a0` = next byte of stdin, or −1 at end of input |
| 9 | `SYS_CYCLES`      | `a0` = low 32 bits of the cycle counter |

---

## 5. Memory model and segmentation

### 5.1 Memory map

The address space is 32 bits wide (4 GiB). Four **1 MiB segments** are mapped, so the
emulator backs 4 MiB of real memory.

```
0xFFFFFFFF ┌──────────────────────┐
           │      unmapped        │   0xFFFFFFF0 = EXIT_TRAP (main's return address)
0x00500000 ├──────────────────────┤ ◀── STACK_TOP (initial sp, exclusive)
           │  STACK   rw-  1 MiB  │   grows ↓, sp bounds-checked
0x00400000 ├──────────────────────┤ ◀── STACK_BASE
           │  HEAP    rw-  1 MiB  │   grows ↑, valid only below the break (sbrk)
0x00300000 ├──────────────────────┤ ◀── HEAP_BASE (initial break)
           │  DATA    rw-  1 MiB  │   .data: globals, strings, arrays
0x00200000 ├──────────────────────┤ ◀── DATA_BASE
           │  TEXT    r-x  1 MiB  │   .text: machine code (up to 262,144 instructions)
0x00100000 ├──────────────────────┤ ◀── TEXT_BASE (entry point default)
           │      unmapped        │   NULL guard: *(int*)0 traps
0x00000000 └──────────────────────┘
```

The memory map lives in one header (`include/c32/layout.hpp`). The assembler exposes it as
predefined constants: `TEXT_BASE`, `DATA_BASE`, `HEAP_BASE`, `STACK_BASE`, `STACK_TOP`,
`SEGMENT_SIZE`.

### 5.2 Access rules (checked on every access, in this order)

1. **Alignment**: word accesses and instruction fetches need `addr % 4 == 0`. Byte accesses
   can be at any address. Violation → `MisalignedAccess`.
2. **Mapping**: the address must fall in one of the four segments. Violation →
   `SegmentationFault`.
3. **Permissions**:

   | Segment | Read | Write | Execute | Extra rule |
   |---------|:----:|:-----:|:-------:|------------|
   | text    | ✓ | ✗ → `WriteViolation` | ✓ | loaded by the loader only |
   | data    | ✓ | ✓ | ✗ → `ExecuteViolation` | |
   | heap    | ✓ | ✓ | ✗ | `addr + size ≤ brk`, else `HeapViolation` |
   | stack   | ✓ | ✓ | ✗ | `sp` must stay within `[STACK_BASE, STACK_TOP]` |

### 5.3 Byte vs word granularity

Memory is byte-addressed and **little-endian**: the word `0x11223344` at address A is stored
as bytes `44 33 22 11` at A..A+3. `LW/SW` move 4 bytes. `LB/LBU/SB` move 1 byte, and
`LB` sign-extends while `LBU` zero-extends. `examples/memory_demo.asm` shows all of this:

```
word  read of value        : 0x11223344
bytes value+0 .. value+3   : 44 33 22 11
LB  of byte 0x80 (signed)  : -128
LBU of byte 0x80 (unsigned): 128
after SB 0xAB to value+0   : 0x112233AB
```

### 5.4 Heap

The heap starts empty: the program break equals `HEAP_BASE`. `SYS_SBRK n` moves the break by
`n` bytes (negative values release memory, which is zeroed) and returns the old break. That
old break is the start of the newly allocated block. Touching heap memory at or after the
break is a `HeapViolation`, which catches use-after-free and buffer overruns past the
allocation. See `examples/heap_list.asm` (a linked list) and `examples/faults/heap_overflow.asm`.

---

## 6. Function calls, the stack and recursion

### 6.1 Hardware-managed calls

Return addresses live on the **stack**, not in a link register. The execute stage does the
work:

```
CALL target :  sp -= 4 ; mem32[sp] = pc + 4 ; pc = target
RET         :  pc = mem32[sp] ; sp += 4
```

Every nested call therefore gets its own return-address slot, so recursion of any depth
works without any special handling by the programmer.

### 6.2 Stack frames

`ENTER n` / `LEAVE` create and destroy a frame with `n` bytes of locals (n must be a
multiple of 4):

```
ENTER n :  push(fp) ; fp = sp ; sp -= n
LEAVE   :  sp = fp  ; fp = pop()
```

Frame layout after `CALL f` followed by `ENTER n`:

```
higher addresses
   ...            caller's frame
   fp + 8         stack-passed arguments (5th onward), if any
   fp + 4         return address           ← pushed by CALL
   fp + 0         caller's saved fp        ← pushed by ENTER      (fp points here)
   fp - 4 ...     local variables
   fp - n         last local               ← sp
lower addresses
```

### 6.3 Calling convention

- Arguments 1–4 go in `a0`–`a3`; more are pushed on the stack. The result comes back in `a0`.
- `t0`–`t4` and `a0`–`a3` are **caller-saved**: a callee may overwrite them.
- `s0`–`s2` and `fp` are **callee-saved**: preserve them with `PUSH`/`POP` or `ENTER`/`LEAVE`.
- `at` is reserved for the assembler's symbolic loads and stores.

A recursive function in C32 (from `examples/factorial.asm`):

```asm
fact:                           ; int fact(int n)   a0 = n -> a0 = n!
        enter   4               ; frame with one local
        sw      a0, -4(fp)      ; save n
        li      t0, 1
        bgt     a0, t0, fact_rec
        li      a0, 1           ; base case
        leave
        ret
fact_rec:
        addi    a0, a0, -1
        call    fact            ; a0 = fact(n - 1)
        lw      t0, -4(fp)      ; n survives in our own frame
        mul     a0, a0, t0
        leave
        ret
```

### 6.4 Program start and exit

On reset the CPU sets `sp = fp = STACK_TOP` and pushes the special return address
`EXIT_TRAP` (0xFFFFFFF0), then sets `pc` to the entry point. That lets `main` behave like an
ordinary function. When `main` executes `RET`, the CPU fetches from `EXIT_TRAP` and halts
cleanly with exit code `a0`. A program can also stop with `SYS_EXIT` or `HALT`. Text memory
past the program is zero-filled, and opcode `0x00` is `HALT`, so running off the end stops
the machine instead of executing garbage.

### 6.5 Stack overflow detection

`sp` is treated like a hardware stack-limit register. Any write that would move it below
`STACK_BASE` (from `PUSH`, `CALL`, `ENTER`, or `ADDI sp, sp, -n`) raises `StackOverflow` at
that exact instruction. A write that moves it above `STACK_TOP` raises `StackUnderflow`.
Without this check, deep recursion would silently run into the heap segment. With it:

```
$ ./build/c32sim examples/faults/stack_overflow.asm
recursing forever...
*** CPU FAULT: StackOverflow at pc=0x00100024 <down+0xC> (address 0x003FFFFC)
    stack overflow: push would move sp to 0x003FFFFC, below the stack segment base 0x00400000 (recursion too deep?)
    instruction: call   0x00100018 <down>
    after 349528 instructions, call depth 87381
```

With 12-byte frames, 1 MiB allows 87,381 nested calls. A plain 4-byte `CALL` chain reaches
262,143.

---

## 7. The fetch–decode–execute cycle

`CPU::step()` runs one complete instruction cycle (`src/cpu.cpp`):

```cpp
if (pc == EXIT_TRAP) halt(a0);                 // main returned
uint32_t raw   = fetch();                      // 1. FETCH
Instruction in = decode(raw);                  // 2. DECODE
execute(in);                                   // 3. EXECUTE + WRITE-BACK + PC update
stats.instructions++; stats.cycles += cost(in);
```

1. **Fetch**: `Memory::fetch32(pc)` checks alignment and *execute* permission and returns
   the instruction word. The tentative next PC is `pc + 4`.
2. **Decode**: the top 6 bits select an `OpInfo` from the opcode table. The table entry's
   `Syntax` says which bit-fields hold `rd`/`rs1`/`rs2`. Its `ImmKind` says how to
   sign- or zero-extend the immediate. Undefined opcodes raise `IllegalInstruction`.
3. **Execute**: one `switch` on the opcode runs the ALU operation, memory access, branch,
   call/return or system call. Results are written back through `write_reg()`. That function
   keeps `r0` at zero and bounds-checks `sp`. Finally `pc = next_pc` (sequential, branch
   target, jump target or popped return address).

A fault raised in any stage aborts the instruction: `pc` stays on the faulting instruction
and the CPU enters the `Faulted` state with a `FaultInfo` describing what happened. Faults
are precise for every single-step instruction. The multi-step `ENTER` may already have
pushed `fp` when its final `sp` adjustment overflows; the machine stops there, so this
never affects a running program.

**Timing model.** Each opcode has a nominal cycle cost: 1 for ALU and branches, +1 for a
taken branch, 2 for memory and jumps, 3 for `MUL` and call/return, 12 for divide, 5 for
syscalls. `--stats` reports total cycles and CPI. This is a simple non-pipelined cost model
for comparing programs, not a cycle-accurate microarchitecture.

---

## 8. Faults (hardware exceptions)

| Fault                | Raised when | Demo |
|----------------------|-------------|------|
| `IllegalInstruction` | opcode ≥ 0x30 | — |
| `MisalignedAccess`   | word load/store or fetch at an address not divisible by 4 | `faults/misaligned.asm` |
| `SegmentationFault`  | address outside every segment (e.g. NULL) | `faults/null_pointer.asm` |
| `WriteViolation`     | store into the text segment | `faults/write_text.asm` |
| `ExecuteViolation`   | PC outside the text segment | `faults/exec_data.asm` |
| `HeapViolation`      | heap access at/after the program break | `faults/heap_overflow.asm` |
| `StackOverflow`      | `sp` would drop below `STACK_BASE` | `faults/stack_overflow.asm` |
| `StackUnderflow`     | pop from an empty stack / `sp` above `STACK_TOP` | unit test |
| `DivideByZero`       | `DIV/DIVU/REM/REMU` by zero | `faults/divide_by_zero.asm` |
| `InvalidSyscall`     | `SYS n` with an unknown `n` | unit test |

The simulator prints the fault type, the faulting PC (with its symbol), the memory address,
a description and the disassembled instruction, then exits with status **3**.

---

## 9. Assembly language reference

```asm
; comments start with ';', '#' or '//'
        .equ    COUNT, 10           ; named constant
        .data
table:  .word   1, 2, 3, table      ; words (auto-aligned to 4); labels allowed as values
name:   .asciiz "C32\n"             ; NUL-terminated string
        .text
main:   li      t0, COUNT           ; label + instruction
loop:   dec     t0
        bnez    t0, loop
        ret
```

- **Case**: mnemonics, directives and registers are case-insensitive. Labels and constants
  are case-sensitive.
- **Labels**: `name:` at the start of a line. Several labels may precede one statement, and a
  label may sit on a line of its own. Names may not be register names.
- **Numbers**: decimal `42`, `-7`, hex `0xFF`, binary `0b1010`, octal `0o17`, character `'A'`
  `'\n'`. Underscores are allowed as separators (`0xFFFF_0000`).
- **Expressions**: `+`, `-` and parentheses over numbers, labels and constants
  (`buffer+8`, `END-START`).
- **Memory operands**: `imm(reg)`, `(reg)`, or a bare symbol such as `lw a0, counter`. The
  bare-symbol form expands through `at`.
- **Entry point**: the label given by `.entry name`, otherwise `main`, otherwise `_start`,
  otherwise the start of `.text`.

| Directive                | Section | Meaning |
|--------------------------|---------|---------|
| `.text` / `.data`        | —       | switch section (default: `.text`) |
| `.word v, ...`           | data    | 32-bit values, auto-aligned to 4 bytes |
| `.byte v, ...`           | data    | 8-bit values (−128..255) |
| `.ascii "s", ...`        | data    | string bytes, no terminator |
| `.asciiz "s", ...`       | data    | string bytes + NUL |
| `.space n` / `.zero n`   | data    | n zero bytes |
| `.align n`               | data    | pad to a multiple of n bytes (power of two) |
| `.equ NAME, expr` / `.set` | any   | define a constant |
| `.entry label`           | any     | set the entry point |
| `.globl` / `.global`     | any     | accepted and ignored (compatibility) |

String escapes: `\n \t \r \0 \\ \" \' \xHH`.

Predefined constants: `SYS_EXIT` … `SYS_CYCLES`, `TEXT_BASE`, `DATA_BASE`, `HEAP_BASE`,
`STACK_BASE`, `STACK_TOP`, `SEGMENT_SIZE`.

Errors are reported as `file:line: error: message` with a hint where possible, for example
`immediate 300000 out of range for addi (-131072..131071); use LI into a register`.

---

## 10. Tools

### `c32asm` — assembler

```
c32asm <input.asm> [-o output.c32] [-l|--listing] [--symbols]
```

It writes a `.c32` binary (default name: the input file with a `.c32` extension). `--listing`
prints each address, its machine word, the disassembly and the source line that produced it.
This makes pseudo-instruction expansion easy to see:

```
  00100010:  68400800   lui    a0, 0x800        ;   25: la      a0, s_open
  00100014:  50440000   ori    a0, a0, 0        ;   25: la      a0, s_open
```

### `c32sim` — simulator

```
c32sim [options] <program.asm | program.c32>
  -t, --trace        log every executed instruction and its register/memory effects to stderr
  -d, --debug        interactive debugger
  -s, --stats        execution statistics (stderr)
  -r, --regs         register dump at exit (stderr)
  -m, --max N        instruction limit (default 100,000,000; 0 = unlimited)
      --dump A:N     hex-dump N bytes at address A after the run
```

Exit status: the program's exit code; **1** for an assembly or load error; **3** for a CPU
fault; **4** when the instruction limit is reached. Program output goes to stdout and all
diagnostics go to stderr, so output can be piped or diffed cleanly.

Trace output:

```
       3  00100008:  A0040015  call   0x00100054 <fact>        sp=0x004FFFF8  mem32[0x004FFFF8]=0x0010000C
       4  00100054:  B4000004  enter  4                        sp=0x004FFFF4  fp=0x004FFFF4  sp=0x004FFFF0 ...
```

### Debugger (`c32sim --debug`)

| Command            | Action |
|--------------------|--------|
| `s [n]`            | step n instructions, showing each one and its effects |
| `c`                | continue until a breakpoint, halt or fault |
| `b <addr\|label>`  | set a breakpoint; `d [addr]` deletes; `bl` lists |
| `r`                | registers |
| `m <addr> [n]`     | n memory words (addr may be a label or register name) |
| `st [n]`           | top n stack words, with `sp` and `fp` marked |
| `x [addr] [n]`     | disassemble around pc or addr |
| `seg`              | segments, program break, sp, fp |
| `stats`, `h`, `q`  | statistics, help, quit |

Note: the debugger and `SYS_READ_INT` both read from the terminal.

### `c32dis` — disassembler

```
c32dis <program.c32 | program.asm>     # listing of text + hex dump of data with labels
c32dis --isa                           # opcode and syscall reference
```

---

## 11. The .c32 binary format

All integers are little-endian.

| Offset | Size | Field |
|--------|------|-------|
| 0      | 4    | magic `"C32X"` |
| 4      | 4    | version (1) |
| 8      | 4    | entry address |
| 12     | 4    | text size in bytes (multiple of 4) |
| 16     | 4    | data size in bytes |
| 20     | 4    | symbol count |
| 24     | text | machine code, loaded at `TEXT_BASE` |
| …      | data | initialised data, loaded at `DATA_BASE` |
| …      | …    | symbols: `u32 address, u16 length, char name[length]` (for debugging) |

The loader zeroes all memory, copies text and data into their segments, resets the heap
break and resets the CPU. The simulator recognises the magic number, so it accepts either a
binary or assembly source.

---

## 12. Testing

```bash
make test        # = ./build/c32tests  +  ./scripts/run_examples.sh
```

- **35 unit tests** (`tests/test_main.cpp`, no external framework) cover:
  - encode/decode round-trips for every opcode, and sign extension
  - little-endian byte/word layout and every memory fault type
  - ALU edge cases (`INT_MIN / -1`, unsigned compares, arithmetic shifts) and 32-bit constants
  - recursion (factorial depth and stack accounting, Fibonacci with callee-saved registers,
    indirect calls)
  - stack overflow/underflow, `sp` bounds, exit-trap halting, syscalls and `sbrk`
  - assembler directives, error reporting and line numbers
  - binary file round-trips and disassembly text
- **16 example programs**: 9 normal programs whose stdout must match
  `examples/expected/*.out`, and 7 fault demos that must trap with exit status 3 and the
  recorded diagnostic. After an intentional output change, re-record with
  `./scripts/run_examples.sh --update`.

The code builds without warnings under `-Wall -Wextra -Wpedantic -Wshadow` with both g++ and
clang++.

---

## 13. Design decisions

| Decision | Choice | Why |
|----------|--------|-----|
| Instruction width | Fixed 32-bit, opcode in the top 6 bits | Fetch is always one aligned word. Decode is a table lookup. PC arithmetic is trivial. 6 bits leaves room for 64 opcodes (48 used). |
| Register count | 16 (4-bit fields) | Enough for arguments, temporaries and saved registers. Three register fields still leave an 18-bit immediate in I-format. |
| `r0` hard-wired to zero | yes | Gives `MOV`, `NEG`, `LI` small, `BEQZ` and "discard result" for free, with no extra opcodes. |
| No flags register | compare-and-branch | Removes hidden state. Each branch is self-contained and easier to reason about and trace. |
| Immediates | 18-bit; arithmetic sign-extended, logical **zero-extended** | `ORI` can then fill low bits without sign smearing, which makes `LUI`+`ORI` build any constant. |
| Constant building | `LUI` loads 22 bits << 10, `ORI` adds the low 10 bits | Two instructions for any 32-bit value. 22 + 10 = 32 bits with no overlap or carry fix-up. |
| Branch offsets | PC-relative, in instructions | Position-independent loops. Counting instructions instead of bytes gives 4× the reach (±128 K instructions). |
| Calls | `CALL`/`RET` push/pop the return address on the stack | This is the "stack-based function calls in the execute stage" requirement. Recursion needs no link-register spilling, and every activation has its own slot. |
| Frames | `ENTER n` / `LEAVE` | One instruction each for frame setup and teardown. Locals sit at fixed `fp` offsets even when `sp` moves. |
| Segment size | **1 MiB each** (4 MiB total) | Large enough for 262 K instructions, 1 MiB of globals and ~87 K recursive frames. Small enough to allocate up front and zero on reset. One constant changes it. |
| Segment placement | starts at 0x00100000, low 1 MiB unmapped | NULL and small-offset pointer bugs trap immediately as `SegmentationFault`. |
| Permissions | text `r-x`, data/heap/stack `rw-` | W^X: code can't be modified, and data can't be executed, which blocks self-modifying code and code injection. |
| Endianness | little-endian | Matches x86/ARM/RISC-V conventions and the host, so byte-level demos behave as people expect. |
| Alignment | words must be 4-aligned (trap), bytes anywhere | Simple, realistic hardware behaviour. It catches pointer-arithmetic bugs instead of hiding them. |
| Heap | `sbrk`-style break, accesses checked against it | Mirrors how a real OS gives a process heap memory, and catches overruns past an allocation. |
| Stack guard | `sp` bounds-checked on every write | Turns runaway recursion into a precise `StackOverflow` at the culprit instruction, instead of silent heap corruption. |
| Exit trap | reset pushes `0xFFFFFFF0` as main's return address | `main` can simply `ret` like any function. The address is unmapped, so it can never collide with code. |
| Opcode 0 = `HALT` | yes | Zero-filled memory halts the machine, so running past the program is safe. |
| Faults as C++ exceptions | `CpuFault` thrown from memory/CPU, caught in `step()` | Keeps the fast path free of error checks, and gives one place to record precise fault state. |
| Single source of truth | one `OpInfo` table drives the assembler, decoder, disassembler, cycle costs and docs | Adding an instruction is local, and the tools can't disagree with each other. |
| I/O | CPU takes `istream`/`ostream` | Unit tests run full programs against string streams. |
| Dependencies | none (C++17 standard library only) | Builds anywhere with one command. |

---

## 14. Extending the ISA

To add an instruction (for example `MAX rd, rs1, rs2`):

1. Add `MAX` to `enum class Opcode` in `include/c32/isa.hpp` (before `COUNT`).
2. Add its row to `kOps` in `src/isa.cpp` at the matching position, e.g.
   `{Opcode::MAX, "MAX", S::RRR, K::None, 1, "rd = max(rs1, rs2), signed"}`.
   A `static_assert` checks the table order at compile time.
3. Implement it in the `switch` in `CPU::execute()` (`src/cpu.cpp`).
4. Add a unit test. The assembler, disassembler and `--isa` table pick it up automatically.

---

## 15. Limitations and future work

- Single-cycle, non-pipelined model. The cycle counts are a cost model, not a
  microarchitecture. Natural extensions: a 5-stage pipeline with hazard detection, caches.
- No interrupts, privilege levels or virtual memory. Segments are fixed in place.
- No floating point, halfword (16-bit) loads/stores or atomic operations.
- The assembler has no macros and produces single-module programs (no linker).
