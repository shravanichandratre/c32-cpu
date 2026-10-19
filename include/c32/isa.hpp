// isa.hpp - the C32 Instruction Set Architecture.
//
// Every instruction is exactly 32 bits. The top 6 bits are the opcode; the
// remaining 26 bits are laid out in one of five shapes:
//
//   R  | opcode:6 | a:4 | b:4 | c:4 | unused:14      |   register-register
//   I  | opcode:6 | a:4 | b:4 | imm18                |   register-immediate / memory / branch
//   U  | opcode:6 | a:4 | imm22                      |   LUI
//   J  | opcode:6 | imm26 (word address)             |   JMP / CALL
//
// Field "a" is bits 25..22, "b" bits 21..18, "c" bits 17..14. Which logical
// operand (rd, rs1, rs2) lives in which field depends on the Syntax of the
// instruction - see encode()/decode() in isa.cpp.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace c32 {

// ---------------------------------------------------------------- registers
constexpr int NUM_REGS = 16;

constexpr uint8_t REG_ZERO = 0;  // hard-wired zero
constexpr uint8_t REG_A0 = 1;    // a0..a3: arguments, a0 = return value
constexpr uint8_t REG_A1 = 2;
constexpr uint8_t REG_A2 = 3;
constexpr uint8_t REG_A3 = 4;
constexpr uint8_t REG_T0 = 5;    // t0..t4 (r5..r9): caller-saved temporaries
constexpr uint8_t REG_S0 = 10;   // s0..s2 (r10..r12): callee-saved
constexpr uint8_t REG_FP = 13;   // frame pointer
constexpr uint8_t REG_SP = 14;   // stack pointer (bounds-checked by hardware)
constexpr uint8_t REG_AT = 15;   // assembler temporary

// ---------------------------------------------------------------- opcodes
enum class Opcode : uint8_t {
    HALT, NOP,
    // register-register ALU
    ADD, SUB, MUL, DIV, DIVU, REM, REMU, AND, OR, XOR, SHL, SHR, SRA, SLT, SLTU, NOT,
    // register-immediate ALU
    ADDI, ANDI, ORI, XORI, SHLI, SHRI, SRAI, SLTI,
    LUI,
    // memory (byte and word)
    LW, LB, LBU, SW, SB,
    // conditional branches (PC-relative)
    BEQ, BNE, BLT, BGE, BLTU, BGEU,
    // jumps and stack-based procedure calls
    JMP, JR, CALL, CALLR, RET,
    PUSH, POP, ENTER, LEAVE,
    // operating-system services
    SYS,
    COUNT
};

constexpr size_t kNumOpcodes = static_cast<size_t>(Opcode::COUNT);

// Assembly operand shape; also determines which encoding fields are used.
enum class Syntax : uint8_t {
    None,    // HALT                       -
    RRR,     // ADD rd, rs1, rs2           R: a=rd b=rs1 c=rs2
    RR,      // NOT rd, rs1                R: a=rd b=rs1
    RRI,     // ADDI rd, rs1, imm          I: a=rd b=rs1
    RI,      // LUI rd, imm22              U: a=rd
    Load,    // LW rd, imm(rs1)            I: a=rd b=rs1
    Store,   // SW rs2, imm(rs1)           I: a=rs2 b=rs1
    Branch,  // BEQ rs1, rs2, label        I: a=rs1 b=rs2 imm=word offset
    Jump,    // CALL label                 J: imm26 = target >> 2
    RegS,    // PUSH rs1 / JR rs1          R: a=rs1
    RegD,    // POP rd                     R: a=rd
    Imm,     // SYS imm / ENTER imm        I: imm18 unsigned
};

enum class ImmKind : uint8_t {
    None,
    Signed18,    // -131072 .. 131071, sign-extended
    Unsigned18,  // 0 .. 262143, zero-extended (logical ops, SYS, ENTER)
    Shamt,       // 0 .. 31
    Unsigned22,  // LUI
    Addr26,      // absolute word address (byte address >> 2)
};

struct OpInfo {
    Opcode op;
    const char* mnemonic;
    Syntax syntax;
    ImmKind imm;
    uint8_t cycles;  // cost in the simple timing model (taken branches +1)
    const char* description;
};

const OpInfo& op_info(Opcode op);
std::optional<Opcode> lookup_mnemonic(std::string_view mnemonic);  // case-insensitive

// ---------------------------------------------------------------- decoded form
struct Instruction {
    Opcode op = Opcode::HALT;
    uint8_t rd = 0;
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    // Branch: offset in instructions. Jump: absolute byte address.
    // Otherwise: the (already sign/zero extended) immediate.
    int32_t imm = 0;
    uint32_t raw = 0;
};

uint32_t encode(const Instruction& ins);
// Throws CpuFault(IllegalInstruction) for an undefined opcode.
Instruction decode(uint32_t raw);

int32_t sign_extend(uint32_t value, int bits);

// ---------------------------------------------------------------- registers by name
std::optional<uint8_t> parse_register(std::string_view name);  // "r5", "t0", "sp", ...
const char* reg_name(uint8_t r);                               // ABI name: "t0"

// ---------------------------------------------------------------- system calls
enum Syscall : uint32_t {
    SYS_EXIT = 0,        // exit(a0)
    SYS_PRINT_INT = 1,   // print a0 as signed decimal
    SYS_PRINT_CHAR = 2,  // print low byte of a0
    SYS_PRINT_STR = 3,   // print NUL-terminated string at address a0
    SYS_READ_INT = 4,    // a0 = integer from stdin, a1 = 0 ok / 1 EOF
    SYS_SBRK = 5,        // a0 = old break; break += a0  (a0 = -1 on failure)
    SYS_PRINT_HEX = 6,   // print a0 as 0xXXXXXXXX
    SYS_PRINT_UINT = 7,  // print a0 as unsigned decimal
    SYS_READ_CHAR = 8,   // a0 = next byte of stdin or -1 on EOF
    SYS_CYCLES = 9,      // a0 = low 32 bits of the cycle counter
    SYS_COUNT
};

const char* syscall_name(uint32_t code);  // "PRINT_INT", or nullptr if unknown

// Address -> label, used for disassembly and traces.
using SymbolTable = std::map<uint32_t, std::string>;

}  // namespace c32
