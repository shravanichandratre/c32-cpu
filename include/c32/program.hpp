// program.hpp - an assembled program image, the .c32 binary file format,
// and the loader that places a program into memory.
//
// .c32 file layout (all integers little-endian):
//   char[4]  magic        "C32X"
//   u32      version      1
//   u32      entry        entry-point address
//   u32      text_size    bytes of machine code
//   u32      data_size    bytes of initialised data
//   u32      symbol_count
//   u8[text_size]         text image (loaded at TEXT_BASE)
//   u8[data_size]         data image (loaded at DATA_BASE)
//   symbol_count x { u32 address; u16 name_length; char name[name_length] }
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "c32/isa.hpp"

namespace c32 {

class CPU;
class Memory;

struct Program {
    std::vector<uint8_t> text;
    std::vector<uint8_t> data;
    uint32_t entry = 0;
    std::map<std::string, uint32_t> symbols;  // label -> address
    std::map<uint32_t, int> line_map;          // text address -> source line (not saved)
    std::vector<std::string> source_lines;     // (not saved)
    std::string source_name;

    SymbolTable address_symbols() const;  // address -> label
};

constexpr uint32_t kBinaryVersion = 1;

void save_binary(const Program& program, const std::string& path);
Program load_binary(const std::string& path);
bool is_binary_file(const std::string& path);

// Loads a .c32 binary, or assembles the file if it is assembly source.
Program load_program_file(const std::string& path);

// Resets memory, copies text/data into their segments and resets the CPU.
void load_program(const Program& program, Memory& memory, CPU& cpu);

}  // namespace c32
