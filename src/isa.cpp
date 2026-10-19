// isa.cpp - opcode table, instruction encoding and decoding.
#include "c32/isa.hpp"

#include <array>
#include <cctype>
#include <string>
#include <unordered_map>

#include "c32/fault.hpp"
#include "c32/util.hpp"

namespace c32 {
namespace {

using S = Syntax;
using K = ImmKind;

// Indexed by opcode value. The static_assert below guarantees the order.
constexpr std::array<OpInfo, kNumOpcodes> kOps = {{
    {Opcode::HALT, "HALT", S::None, K::None, 1, "stop the machine (exit code 0)"},
    {Opcode::NOP, "NOP", S::None, K::None, 1, "no operation"},
    {Opcode::ADD, "ADD", S::RRR, K::None, 1, "rd = rs1 + rs2"},
    {Opcode::SUB, "SUB", S::RRR, K::None, 1, "rd = rs1 - rs2"},
    {Opcode::MUL, "MUL", S::RRR, K::None, 3, "rd = low 32 bits of rs1 * rs2"},
    {Opcode::DIV, "DIV", S::RRR, K::None, 12, "rd = rs1 / rs2 (signed, traps on 0)"},
    {Opcode::DIVU, "DIVU", S::RRR, K::None, 12, "rd = rs1 / rs2 (unsigned, traps on 0)"},
    {Opcode::REM, "REM", S::RRR, K::None, 12, "rd = rs1 % rs2 (signed, traps on 0)"},
    {Opcode::REMU, "REMU", S::RRR, K::None, 12, "rd = rs1 % rs2 (unsigned, traps on 0)"},
    {Opcode::AND, "AND", S::RRR, K::None, 1, "rd = rs1 & rs2"},
    {Opcode::OR, "OR", S::RRR, K::None, 1, "rd = rs1 | rs2"},
    {Opcode::XOR, "XOR", S::RRR, K::None, 1, "rd = rs1 ^ rs2"},
    {Opcode::SHL, "SHL", S::RRR, K::None, 1, "rd = rs1 << (rs2 & 31)"},
    {Opcode::SHR, "SHR", S::RRR, K::None, 1, "rd = rs1 >> (rs2 & 31), logical"},
    {Opcode::SRA, "SRA", S::RRR, K::None, 1, "rd = rs1 >> (rs2 & 31), arithmetic"},
    {Opcode::SLT, "SLT", S::RRR, K::None, 1, "rd = (rs1 < rs2) ? 1 : 0, signed"},
    {Opcode::SLTU, "SLTU", S::RRR, K::None, 1, "rd = (rs1 < rs2) ? 1 : 0, unsigned"},
    {Opcode::NOT, "NOT", S::RR, K::None, 1, "rd = ~rs1"},
    {Opcode::ADDI, "ADDI", S::RRI, K::Signed18, 1, "rd = rs1 + sext(imm18)"},
    {Opcode::ANDI, "ANDI", S::RRI, K::Unsigned18, 1, "rd = rs1 & zext(imm18)"},
    {Opcode::ORI, "ORI", S::RRI, K::Unsigned18, 1, "rd = rs1 | zext(imm18)"},
    {Opcode::XORI, "XORI", S::RRI, K::Unsigned18, 1, "rd = rs1 ^ zext(imm18)"},
    {Opcode::SHLI, "SHLI", S::RRI, K::Shamt, 1, "rd = rs1 << imm (0..31)"},
    {Opcode::SHRI, "SHRI", S::RRI, K::Shamt, 1, "rd = rs1 >> imm, logical"},
    {Opcode::SRAI, "SRAI", S::RRI, K::Shamt, 1, "rd = rs1 >> imm, arithmetic"},
    {Opcode::SLTI, "SLTI", S::RRI, K::Signed18, 1, "rd = (rs1 < sext(imm18)) ? 1 : 0"},
    {Opcode::LUI, "LUI", S::RI, K::Unsigned22, 1, "rd = imm22 << 10"},
    {Opcode::LW, "LW", S::Load, K::Signed18, 2, "rd = mem32[rs1 + imm]"},
    {Opcode::LB, "LB", S::Load, K::Signed18, 2, "rd = sext(mem8[rs1 + imm])"},
    {Opcode::LBU, "LBU", S::Load, K::Signed18, 2, "rd = zext(mem8[rs1 + imm])"},
    {Opcode::SW, "SW", S::Store, K::Signed18, 2, "mem32[rs1 + imm] = rs2"},
    {Opcode::SB, "SB", S::Store, K::Signed18, 2, "mem8[rs1 + imm] = rs2 & 0xFF"},
    {Opcode::BEQ, "BEQ", S::Branch, K::Signed18, 1, "if rs1 == rs2: pc += 4 + imm*4"},
    {Opcode::BNE, "BNE", S::Branch, K::Signed18, 1, "if rs1 != rs2: pc += 4 + imm*4"},
    {Opcode::BLT, "BLT", S::Branch, K::Signed18, 1, "if rs1 <  rs2 (signed)"},
    {Opcode::BGE, "BGE", S::Branch, K::Signed18, 1, "if rs1 >= rs2 (signed)"},
    {Opcode::BLTU, "BLTU", S::Branch, K::Signed18, 1, "if rs1 <  rs2 (unsigned)"},
    {Opcode::BGEU, "BGEU", S::Branch, K::Signed18, 1, "if rs1 >= rs2 (unsigned)"},
    {Opcode::JMP, "JMP", S::Jump, K::Addr26, 2, "pc = target"},
    {Opcode::JR, "JR", S::RegS, K::None, 2, "pc = rs1"},
    {Opcode::CALL, "CALL", S::Jump, K::Addr26, 3, "push(pc + 4); pc = target"},
    {Opcode::CALLR, "CALLR", S::RegS, K::None, 3, "push(pc + 4); pc = rs1"},
    {Opcode::RET, "RET", S::None, K::None, 3, "pc = pop()"},
    {Opcode::PUSH, "PUSH", S::RegS, K::None, 2, "sp -= 4; mem32[sp] = rs1"},
    {Opcode::POP, "POP", S::RegD, K::None, 2, "rd = mem32[sp]; sp += 4"},
    {Opcode::ENTER, "ENTER", S::Imm, K::Unsigned18, 3, "push(fp); fp = sp; sp -= imm"},
    {Opcode::LEAVE, "LEAVE", S::None, K::None, 3, "sp = fp; fp = pop()"},
    {Opcode::SYS, "SYS", S::Imm, K::Unsigned18, 5, "system call number imm"},
}};

constexpr bool table_is_ordered() {
    for (size_t i = 0; i < kOps.size(); ++i)
        if (static_cast<size_t>(kOps[i].op) != i) return false;
    return true;
}
static_assert(table_is_ordered(), "opcode table out of order");
static_assert(kNumOpcodes <= 64, "opcodes must fit in 6 bits");

constexpr std::array<const char*, NUM_REGS> kRegNames = {
    "zero", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
    "t3",   "t4", "s0", "s1", "s2", "fp", "sp", "at"};

constexpr std::array<const char*, SYS_COUNT> kSyscallNames = {
    "EXIT", "PRINT_INT", "PRINT_CHAR", "PRINT_STR", "READ_INT",
    "SBRK", "PRINT_HEX", "PRINT_UINT", "READ_CHAR", "CYCLES"};

std::string to_upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

}  // namespace

const OpInfo& op_info(Opcode op) { return kOps[static_cast<size_t>(op)]; }

std::optional<Opcode> lookup_mnemonic(std::string_view mnemonic) {
    static const std::unordered_map<std::string, Opcode> table = [] {
        std::unordered_map<std::string, Opcode> t;
        for (const auto& info : kOps) t.emplace(info.mnemonic, info.op);
        return t;
    }();
    auto it = table.find(to_upper(mnemonic));
    if (it == table.end()) return std::nullopt;
    return it->second;
}

int32_t sign_extend(uint32_t value, int bits) {
    if (bits >= 32) return static_cast<int32_t>(value);
    const uint32_t mask = (1u << bits) - 1u;
    const uint32_t sign = 1u << (bits - 1);
    value &= mask;
    return static_cast<int32_t>((value ^ sign) - sign);
}

uint32_t encode(const Instruction& ins) {
    const OpInfo& info = op_info(ins.op);
    uint32_t w = (static_cast<uint32_t>(ins.op) & 0x3Fu) << 26;
    auto A = [&](uint32_t r) { w |= (r & 0xFu) << 22; };
    auto B = [&](uint32_t r) { w |= (r & 0xFu) << 18; };
    auto C = [&](uint32_t r) { w |= (r & 0xFu) << 14; };
    auto I18 = [&](int32_t v) { w |= static_cast<uint32_t>(v) & 0x3FFFFu; };

    switch (info.syntax) {
        case S::None: break;
        case S::RRR: A(ins.rd); B(ins.rs1); C(ins.rs2); break;
        case S::RR: A(ins.rd); B(ins.rs1); break;
        case S::RRI: A(ins.rd); B(ins.rs1); I18(ins.imm); break;
        case S::RI: A(ins.rd); w |= static_cast<uint32_t>(ins.imm) & 0x3FFFFFu; break;
        case S::Load: A(ins.rd); B(ins.rs1); I18(ins.imm); break;
        case S::Store: A(ins.rs2); B(ins.rs1); I18(ins.imm); break;
        case S::Branch: A(ins.rs1); B(ins.rs2); I18(ins.imm); break;
        case S::Jump: w |= (static_cast<uint32_t>(ins.imm) >> 2) & 0x3FFFFFFu; break;
        case S::RegS: A(ins.rs1); break;
        case S::RegD: A(ins.rd); break;
        case S::Imm: I18(ins.imm); break;
    }
    return w;
}

Instruction decode(uint32_t raw) {
    const uint32_t opv = raw >> 26;
    if (opv >= kNumOpcodes) {
        throw CpuFault(FaultType::IllegalInstruction, 0,
                       strfmt("illegal opcode 0x%02X in instruction word 0x%08X", opv, raw));
    }
    Instruction ins;
    ins.op = static_cast<Opcode>(opv);
    ins.raw = raw;
    const OpInfo& info = op_info(ins.op);

    const auto a = static_cast<uint8_t>((raw >> 22) & 0xFu);
    const auto b = static_cast<uint8_t>((raw >> 18) & 0xFu);
    const auto c = static_cast<uint8_t>((raw >> 14) & 0xFu);
    const uint32_t i18 = raw & 0x3FFFFu;
    const int32_t imm18 = info.imm == K::Signed18 ? sign_extend(i18, 18)
                                                  : static_cast<int32_t>(i18);

    switch (info.syntax) {
        case S::None: break;
        case S::RRR: ins.rd = a; ins.rs1 = b; ins.rs2 = c; break;
        case S::RR: ins.rd = a; ins.rs1 = b; break;
        case S::RRI: ins.rd = a; ins.rs1 = b; ins.imm = imm18; break;
        case S::RI: ins.rd = a; ins.imm = static_cast<int32_t>(raw & 0x3FFFFFu); break;
        case S::Load: ins.rd = a; ins.rs1 = b; ins.imm = imm18; break;
        case S::Store: ins.rs2 = a; ins.rs1 = b; ins.imm = imm18; break;
        case S::Branch: ins.rs1 = a; ins.rs2 = b; ins.imm = imm18; break;
        case S::Jump: ins.imm = static_cast<int32_t>((raw & 0x3FFFFFFu) << 2); break;
        case S::RegS: ins.rs1 = a; break;
        case S::RegD: ins.rd = a; break;
        case S::Imm: ins.imm = imm18; break;
    }
    return ins;
}

std::optional<uint8_t> parse_register(std::string_view name) {
    std::string s(name);
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (s.size() >= 2 && s[0] == 'r') {
        bool digits = true;
        for (size_t i = 1; i < s.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) digits = false;
        if (digits && s.size() <= 3) {
            int n = std::stoi(s.substr(1));
            if (n >= 0 && n < NUM_REGS) return static_cast<uint8_t>(n);
            return std::nullopt;
        }
    }
    for (int i = 0; i < NUM_REGS; ++i)
        if (s == kRegNames[static_cast<size_t>(i)]) return static_cast<uint8_t>(i);
    return std::nullopt;
}

const char* reg_name(uint8_t r) { return r < NUM_REGS ? kRegNames[r] : "?"; }

const char* syscall_name(uint32_t code) {
    return code < SYS_COUNT ? kSyscallNames[code] : nullptr;
}

}  // namespace c32
