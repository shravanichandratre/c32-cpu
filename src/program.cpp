// program.cpp - .c32 binary format and program loader.
#include "c32/program.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

#include "c32/assembler.hpp"
#include "c32/cpu.hpp"
#include "c32/layout.hpp"
#include "c32/memory.hpp"

namespace c32 {
namespace {

constexpr char kMagic[4] = {'C', '3', '2', 'X'};

void put32(std::ostream& os, uint32_t v) {
    const char b[4] = {static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16),
                       static_cast<char>(v >> 24)};
    os.write(b, 4);
}

void put16(std::ostream& os, uint16_t v) {
    const char b[2] = {static_cast<char>(v), static_cast<char>(v >> 8)};
    os.write(b, 2);
}

uint32_t get32(std::istream& is) {
    unsigned char b[4];
    if (!is.read(reinterpret_cast<char*>(b), 4)) throw std::runtime_error("truncated .c32 file");
    return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
           (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
}

uint16_t get16(std::istream& is) {
    unsigned char b[2];
    if (!is.read(reinterpret_cast<char*>(b), 2)) throw std::runtime_error("truncated .c32 file");
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

}  // namespace

SymbolTable Program::address_symbols() const {
    SymbolTable t;
    for (const auto& kv : symbols) t.emplace(kv.second, kv.first);  // first name wins
    return t;
}

void save_binary(const Program& p, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    if (!os) throw std::runtime_error("cannot write '" + path + "'");
    os.write(kMagic, 4);
    put32(os, kBinaryVersion);
    put32(os, p.entry);
    put32(os, static_cast<uint32_t>(p.text.size()));
    put32(os, static_cast<uint32_t>(p.data.size()));
    put32(os, static_cast<uint32_t>(p.symbols.size()));
    os.write(reinterpret_cast<const char*>(p.text.data()), static_cast<std::streamsize>(p.text.size()));
    os.write(reinterpret_cast<const char*>(p.data.data()), static_cast<std::streamsize>(p.data.size()));
    for (const auto& kv : p.symbols) {
        put32(os, kv.second);
        put16(os, static_cast<uint16_t>(kv.first.size()));
        os.write(kv.first.data(), static_cast<std::streamsize>(kv.first.size()));
    }
    if (!os) throw std::runtime_error("error while writing '" + path + "'");
}

bool is_binary_file(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    char m[4] = {};
    return is.read(m, 4) && std::memcmp(m, kMagic, 4) == 0;
}

Program load_binary(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) throw std::runtime_error("cannot open '" + path + "'");
    char m[4] = {};
    if (!is.read(m, 4) || std::memcmp(m, kMagic, 4) != 0)
        throw std::runtime_error("'" + path + "' is not a C32 binary (bad magic)");
    const uint32_t version = get32(is);
    if (version != kBinaryVersion)
        throw std::runtime_error("unsupported .c32 version " + std::to_string(version));
    Program p;
    p.source_name = path;
    p.entry = get32(is);
    const uint32_t text_size = get32(is);
    const uint32_t data_size = get32(is);
    const uint32_t nsyms = get32(is);
    if (text_size > SEGMENT_SIZE || data_size > SEGMENT_SIZE || text_size % 4 != 0)
        throw std::runtime_error("corrupt .c32 header in '" + path + "'");
    p.text.resize(text_size);
    p.data.resize(data_size);
    if (!is.read(reinterpret_cast<char*>(p.text.data()), text_size) ||
        !is.read(reinterpret_cast<char*>(p.data.data()), data_size))
        throw std::runtime_error("truncated .c32 file '" + path + "'");
    for (uint32_t i = 0; i < nsyms; ++i) {
        const uint32_t addr = get32(is);
        const uint16_t len = get16(is);
        std::string name(len, '\0');
        if (len && !is.read(&name[0], len)) throw std::runtime_error("truncated symbol table");
        p.symbols[name] = addr;
    }
    return p;
}

Program load_program_file(const std::string& path) {
    if (is_binary_file(path)) return load_binary(path);
    return assemble_file(path);
}

void load_program(const Program& p, Memory& memory, CPU& cpu) {
    memory.reset();
    memory.load(SegmentId::Text, p.text);
    memory.load(SegmentId::Data, p.data);
    cpu.reset(p.entry);
}

}  // namespace c32
