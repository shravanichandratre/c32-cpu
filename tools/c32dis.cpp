// c32dis - disassemble a C32 program, or print the ISA reference.
#include <iostream>
#include <string>

#include "c32/assembler.hpp"
#include "c32/disassembler.hpp"
#include "c32/program.hpp"

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        std::cerr << "usage: c32dis <program.c32 | program.asm>\n"
                     "       c32dis --isa        print the opcode and system-call reference\n";
        return argc == 2 ? 0 : 1;
    }
    const std::string arg = argv[1];
    if (arg == "--isa") {
        c32::print_isa_reference(std::cout);
        return 0;
    }
    try {
        const c32::Program p = c32::load_program_file(arg);
        c32::print_listing(p, std::cout);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
