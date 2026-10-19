// cpu.hpp - the C32 processor core: registers, PC and the
// fetch -> decode -> execute cycle.
#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

#include "c32/isa.hpp"
#include "c32/layout.hpp"
#include "c32/memory.hpp"

namespace c32 {

enum class CpuState { Running, Halted, Faulted };
enum class RunResult { Halted, Faulted, LimitReached };

struct FaultInfo {
    FaultType type;
    uint32_t pc;    // address of the faulting instruction
    uint32_t addr;  // faulting memory address (or pc for fetch/decode faults)
    std::string message;
};

struct CpuStats {
    uint64_t instructions = 0;
    uint64_t cycles = 0;
    uint64_t calls = 0;
    uint64_t returns = 0;
    uint64_t branches_taken = 0;
    uint64_t syscalls = 0;
    uint32_t call_depth = 0;      // current nesting of CALL/CALLR
    uint32_t max_call_depth = 0;  // deepest recursion seen
    uint32_t min_sp = STACK_TOP;  // lowest stack pointer seen
};

// Side effects of the most recent instruction (for tracing / debugging).
struct Effects {
    struct RegWrite { uint8_t reg; uint32_t value; };
    std::array<RegWrite, 3> regs{};
    int num_regs = 0;
    bool mem_write = false;
    uint32_t mem_addr = 0;
    uint32_t mem_value = 0;
    AccessSize mem_size = AccessSize::Word;
};

class CPU {
public:
    CPU(Memory& memory, std::istream& in, std::ostream& out);

    // Clears registers, sets PC = entry, sp = top of stack, and pushes the
    // EXIT_TRAP return address so that `RET` from main halts the machine.
    void reset(uint32_t entry);

    // ---- the three stages of one instruction cycle ----
    uint32_t fetch();                        // read word at PC, compute next PC
    Instruction decode(uint32_t raw) const;  // split into opcode/registers/immediate
    void execute(const Instruction& ins);    // ALU / memory / control flow / write-back

    bool step();                                     // one full cycle; false once stopped
    RunResult run(uint64_t max_instructions = 0);    // 0 = no limit

    // ---- state inspection ----
    CpuState state() const { return state_; }
    uint32_t pc() const { return pc_; }
    uint32_t reg(int i) const { return regs_[static_cast<size_t>(i)]; }
    void set_reg(int i, uint32_t value);  // debugger / tests (r0 stays 0)
    int32_t exit_code() const { return exit_code_; }
    const std::optional<FaultInfo>& fault() const { return fault_; }
    const CpuStats& stats() const { return stats_; }
    const Effects& last_effects() const { return effects_; }
    const Instruction& last_instruction() const { return last_ins_; }
    uint32_t last_pc() const { return last_pc_; }

    // When `trace` is non-null every executed instruction is logged to it.
    void set_trace(std::ostream* trace, const SymbolTable* symbols = nullptr);

private:
    void write_reg(uint8_t r, uint32_t value);
    void push(uint32_t value);
    uint32_t pop();
    void syscall(uint32_t code);
    void trace_instruction(uint32_t pc, const Instruction& ins);

    Memory& mem_;
    std::istream& in_;
    std::ostream& out_;

    std::array<uint32_t, NUM_REGS> regs_{};
    uint32_t pc_ = TEXT_BASE;
    uint32_t next_pc_ = TEXT_BASE;
    CpuState state_ = CpuState::Halted;
    int32_t exit_code_ = 0;
    std::optional<FaultInfo> fault_;
    CpuStats stats_;
    Effects effects_;
    Instruction last_ins_;
    uint32_t last_pc_ = 0;
    uint32_t extra_cycles_ = 0;

    std::ostream* trace_ = nullptr;
    const SymbolTable* trace_symbols_ = nullptr;
};

}  // namespace c32
