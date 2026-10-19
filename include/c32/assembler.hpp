// assembler.hpp - two-pass assembler for C32 assembly language.
#pragma once

#include <stdexcept>
#include <string>

#include "c32/program.hpp"

namespace c32 {

class AssemblyError : public std::runtime_error {
public:
    AssemblyError(const std::string& file, int line, const std::string& message);
    int line() const { return line_; }

private:
    int line_;
};

// Assembles source text. Throws AssemblyError on the first error.
Program assemble(const std::string& source, const std::string& name = "<input>");
Program assemble_file(const std::string& path);

}  // namespace c32
