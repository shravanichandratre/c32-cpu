// disassembler.hpp - turns machine words back into readable assembly.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

#include "c32/isa.hpp"
#include "c32/program.hpp"

namespace c32 {

// " <label>" or " <label+0x8>" for an address, or "" if nothing is near.
std::string symbolize(uint32_t addr, const SymbolTable* symbols);

std::string disassemble(const Instruction& ins, uint32_t pc, const SymbolTable* symbols = nullptr);
// Like disassemble(), but never throws: undefined opcodes become ".word".
std::string disassemble_word(uint32_t raw, uint32_t pc, const SymbolTable* symbols = nullptr);

// Full listing of a program: text disassembly (with source lines when
// available) followed by a hex dump of the data segment.
void print_listing(const Program& program, std::ostream& os);

// Reference table of every opcode and system call.
void print_isa_reference(std::ostream& os);

}  // namespace c32
