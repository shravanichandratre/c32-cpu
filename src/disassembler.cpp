// disassembler.cpp - machine code -> assembly text, listings, ISA reference.
#include "c32/disassembler.hpp"

#include <cctype>
#include <ostream>

#include "c32/fault.hpp"
#include "c32/layout.hpp"
#include "c32/util.hpp"

namespace c32 {
namespace {

std::string lower(const char* s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::string pad(std::string s, size_t width) {
    if (s.size() < width) s.append(width - s.size(), ' ');
    return s;
}

const char* syntax_text(Syntax s) {
    switch (s) {
        case Syntax::None: return "";
        case Syntax::RRR: return "rd, rs1, rs2";
        case Syntax::RR: return "rd, rs1";
        case Syntax::RRI: return "rd, rs1, imm";
        case Syntax::RI: return "rd, imm22";
        case Syntax::Load: return "rd, imm(rs1)";
        case Syntax::Store: return "rs2, imm(rs1)";
        case Syntax::Branch: return "rs1, rs2, label";
        case Syntax::Jump: return "label";
        case Syntax::RegS: return "rs1";
        case Syntax::RegD: return "rd";
        case Syntax::Imm: return "imm";
    }
    return "";
}

const char* format_letter(Syntax s) {
    switch (s) {
        case Syntax::RRR: case Syntax::RR: case Syntax::RegS: case Syntax::RegD: return "R";
        case Syntax::RRI: case Syntax::Load: case Syntax::Store: case Syntax::Branch: case Syntax::Imm: return "I";
        case Syntax::RI: return "U";
        case Syntax::Jump: return "J";
        case Syntax::None: return "-";
    }
    return "-";
}

}  // namespace

std::string symbolize(uint32_t addr, const SymbolTable* symbols) {
    if (symbols == nullptr || symbols->empty()) return "";
    auto it = symbols->upper_bound(addr);
    if (it == symbols->begin()) return "";
    --it;
    const uint32_t off = addr - it->first;
    if (off == 0) return " <" + it->second + ">";
    if (off < 0x1000) return strfmt(" <%s+0x%X>", it->second.c_str(), off);
    return "";
}

std::string disassemble(const Instruction& ins, uint32_t pc, const SymbolTable* symbols) {
    const OpInfo& info = op_info(ins.op);
    const std::string m = pad(lower(info.mnemonic), 7);
    auto R = [](uint8_t r) { return std::string(reg_name(r)); };

    switch (info.syntax) {
        case Syntax::None: return lower(info.mnemonic);
        case Syntax::RRR: return m + R(ins.rd) + ", " + R(ins.rs1) + ", " + R(ins.rs2);
        case Syntax::RR: return m + R(ins.rd) + ", " + R(ins.rs1);
        case Syntax::RRI:
            if (info.imm == ImmKind::Unsigned18 && ins.imm > 9)
                return m + R(ins.rd) + ", " + R(ins.rs1) + strfmt(", 0x%X", ins.imm);
            return m + R(ins.rd) + ", " + R(ins.rs1) + ", " + std::to_string(ins.imm);
        case Syntax::RI: return m + R(ins.rd) + strfmt(", 0x%X", ins.imm);
        case Syntax::Load: return m + R(ins.rd) + ", " + std::to_string(ins.imm) + "(" + R(ins.rs1) + ")";
        case Syntax::Store: return m + R(ins.rs2) + ", " + std::to_string(ins.imm) + "(" + R(ins.rs1) + ")";
        case Syntax::Branch: {
            const uint32_t target = pc + 4 + static_cast<uint32_t>(ins.imm) * 4u;
            return m + R(ins.rs1) + ", " + R(ins.rs2) + ", " + hex32(target) + symbolize(target, symbols);
        }
        case Syntax::Jump: {
            const uint32_t target = static_cast<uint32_t>(ins.imm);
            return m + hex32(target) + symbolize(target, symbols);
        }
        case Syntax::RegS: return m + R(ins.rs1);
        case Syntax::RegD: return m + R(ins.rd);
        case Syntax::Imm:
            if (ins.op == Opcode::SYS) {
                const char* name = syscall_name(static_cast<uint32_t>(ins.imm));
                return m + std::to_string(ins.imm) + (name ? std::string("  (") + name + ")" : "");
            }
            return m + std::to_string(ins.imm);
    }
    return "?";
}

std::string disassemble_word(uint32_t raw, uint32_t pc, const SymbolTable* symbols) {
    try {
        return disassemble(decode(raw), pc, symbols);
    } catch (const CpuFault&) {
        return strfmt(".word   0x%08X   (illegal opcode)", raw);
    }
}

void print_listing(const Program& p, std::ostream& os) {
    const SymbolTable syms = p.address_symbols();
    os << "; ---- .text  (" << p.text.size() / 4 << " instructions, entry " << hex32(p.entry) << ")\n";
    for (size_t off = 0; off + 4 <= p.text.size(); off += 4) {
        const uint32_t addr = TEXT_BASE + static_cast<uint32_t>(off);
        const uint32_t raw = static_cast<uint32_t>(p.text[off]) | (static_cast<uint32_t>(p.text[off + 1]) << 8) |
                             (static_cast<uint32_t>(p.text[off + 2]) << 16) |
                             (static_cast<uint32_t>(p.text[off + 3]) << 24);
        auto label = syms.find(addr);
        if (label != syms.end()) os << label->second << ":\n";
        std::string line = strfmt("  %08X:  %08X   ", addr, raw) + pad(disassemble_word(raw, addr, &syms), 40);
        auto src = p.line_map.find(addr);
        if (src != p.line_map.end() && src->second >= 1 &&
            static_cast<size_t>(src->second) <= p.source_lines.size()) {
            std::string s = p.source_lines[static_cast<size_t>(src->second - 1)];
            size_t first = s.find_first_not_of(" \t");
            s = first == std::string::npos ? "" : s.substr(first);
            line += strfmt("; %4d: ", src->second) + s;
        }
        os << line << "\n";
    }
    if (!p.data.empty()) {
        os << "\n; ---- .data  (" << p.data.size() << " bytes)\n";
        for (size_t off = 0; off < p.data.size(); off += 16) {
            const uint32_t addr = DATA_BASE + static_cast<uint32_t>(off);
            std::string hex, ascii;
            for (size_t i = 0; i < 16; ++i) {
                if (off + i < p.data.size()) {
                    const uint8_t b = p.data[off + i];
                    hex += strfmt("%02X ", b);
                    ascii += (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
                } else {
                    hex += "   ";
                }
            }
            os << strfmt("  %08X:  ", addr) << hex << " |" << ascii << "|";
            for (const auto& kv : syms)
                if (kv.first >= addr && kv.first < addr + 16) os << "  " << kv.second << "@+" << (kv.first - addr);
            os << "\n";
        }
    }
}

void print_isa_reference(std::ostream& os) {
    os << "C32 instruction set (32-bit fixed-width instructions, 6-bit opcode)\n\n";
    os << "  op    hex   fmt  mnemonic  operands          cycles  semantics\n";
    os << "  ----  ----  ---  --------  ----------------  ------  ---------------------------------\n";
    for (size_t i = 0; i < kNumOpcodes; ++i) {
        const OpInfo& info = op_info(static_cast<Opcode>(i));
        os << strfmt("  %4zu  0x%02zX  %-3s  %-8s  %-16s  %6u  %s\n", i, i, format_letter(info.syntax),
                     info.mnemonic, syntax_text(info.syntax), static_cast<unsigned>(info.cycles),
                     info.description);
    }
    os << "\nSystem calls (SYS n):\n";
    for (uint32_t i = 0; i < SYS_COUNT; ++i) os << strfmt("  %2u  SYS_%s\n", i, syscall_name(i));
}

}  // namespace c32
