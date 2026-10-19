// c32asm - assemble C32 assembly source into a .c32 binary.
#include <iostream>
#include <string>

#include "c32/assembler.hpp"
#include "c32/disassembler.hpp"
#include "c32/program.hpp"
#include "c32/util.hpp"

namespace {

void usage() {
    std::cerr << "usage: c32asm <input.asm> [-o output.c32] [-l|--listing] [--symbols]\n"
                 "\n"
                 "  -o FILE        output file (default: input name with .c32 extension)\n"
                 "  -l, --listing  print an annotated listing (address, machine code, source)\n"
                 "      --symbols  print the symbol table\n"
                 "  -h, --help     show this help\n";
}

std::string default_output(const std::string& in) {
    const size_t slash = in.find_last_of("/\\");
    const size_t dot = in.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) return in.substr(0, dot) + ".c32";
    return in + ".c32";
}

}  // namespace

int main(int argc, char** argv) {
    std::string input, output;
    bool listing = false, symbols = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { usage(); return 0; }
        if (arg == "-o" && i + 1 < argc) { output = argv[++i]; continue; }
        if (arg == "-l" || arg == "--listing") { listing = true; continue; }
        if (arg == "--symbols") { symbols = true; continue; }
        if (!arg.empty() && arg[0] == '-') { std::cerr << "c32asm: unknown option '" << arg << "'\n"; usage(); return 1; }
        if (!input.empty()) { std::cerr << "c32asm: only one input file is supported\n"; return 1; }
        input = arg;
    }
    if (input.empty()) { usage(); return 1; }
    if (output.empty()) output = default_output(input);

    try {
        const c32::Program p = c32::assemble_file(input);
        c32::save_binary(p, output);
        std::cout << "c32asm: " << input << " -> " << output << "  (" << p.text.size() / 4 << " instructions, "
                  << p.data.size() << " data bytes, entry " << c32::hex32(p.entry) << ")\n";
        if (listing) {
            std::cout << "\n";
            c32::print_listing(p, std::cout);
        }
        if (symbols) {
            std::cout << "\nsymbols:\n";
            for (const auto& kv : p.address_symbols()) std::cout << "  " << c32::hex32(kv.first) << "  " << kv.second << "\n";
        }
    } catch (const c32::AssemblyError& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "c32asm: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
