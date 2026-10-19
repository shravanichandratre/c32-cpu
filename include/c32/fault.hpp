// fault.hpp - hardware exceptions ("faults") raised by the CPU and memory.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace c32 {

enum class FaultType {
    IllegalInstruction,  // opcode not defined by the ISA
    MisalignedAccess,    // word access or instruction fetch not 4-byte aligned
    SegmentationFault,   // address is not inside any mapped segment
    WriteViolation,      // store into the read-only text segment
    ExecuteViolation,    // instruction fetch outside the text segment
    HeapViolation,       // heap access at/after the program break
    StackOverflow,       // sp would move below the stack segment
    StackUnderflow,      // pop with an empty stack / sp above the stack top
    DivideByZero,        // DIV/DIVU/REM/REMU with a zero divisor
    InvalidSyscall,      // SYS with an unknown service number
};

inline const char* fault_name(FaultType t) {
    switch (t) {
        case FaultType::IllegalInstruction: return "IllegalInstruction";
        case FaultType::MisalignedAccess: return "MisalignedAccess";
        case FaultType::SegmentationFault: return "SegmentationFault";
        case FaultType::WriteViolation: return "WriteViolation";
        case FaultType::ExecuteViolation: return "ExecuteViolation";
        case FaultType::HeapViolation: return "HeapViolation";
        case FaultType::StackOverflow: return "StackOverflow";
        case FaultType::StackUnderflow: return "StackUnderflow";
        case FaultType::DivideByZero: return "DivideByZero";
        case FaultType::InvalidSyscall: return "InvalidSyscall";
    }
    return "Unknown";
}

// Thrown by Memory and CPU; caught by CPU::step(), which records it and stops.
class CpuFault : public std::runtime_error {
public:
    CpuFault(FaultType type, uint32_t addr, const std::string& message)
        : std::runtime_error(message), type_(type), addr_(addr) {}
    FaultType type() const { return type_; }
    uint32_t addr() const { return addr_; }

private:
    FaultType type_;
    uint32_t addr_;
};

}  // namespace c32
