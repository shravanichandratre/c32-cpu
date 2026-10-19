// cpu.cpp - fetch / decode / execute implementation of the C32 core.
#include "c32/cpu.hpp"

#include <climits>
#include <cstdio>
#include <istream>
#include <ostream>

#include "c32/disassembler.hpp"
#include "c32/util.hpp"

namespace c32 {
namespace {

uint32_t sra(uint32_t x, uint32_t n) {
    n &= 31u;
    // Portable arithmetic shift right.
    return (x & 0x80000000u) ? ~(~x >> n) : (x >> n);
}

}  // namespace

CPU::CPU(Memory& memory, std::istream& in, std::ostream& out)
    : mem_(memory), in_(in), out_(out) {}

void CPU::reset(uint32_t entry) {
    regs_.fill(0);
    pc_ = next_pc_ = entry;
    state_ = CpuState::Running;
    exit_code_ = 0;
    fault_.reset();
    stats_ = CpuStats{};
    effects_ = Effects{};
    last_ins_ = Instruction{};
    last_pc_ = entry;

    regs_[REG_SP] = STACK_TOP;
    regs_[REG_FP] = STACK_TOP;
    // Plant the exit trap as main's return address: `RET` from main halts.
    push(EXIT_TRAP);
    effects_ = Effects{};
    stats_.min_sp = regs_[REG_SP];
}

void CPU::set_reg(int i, uint32_t value) {
    if (i > 0 && i < NUM_REGS) regs_[static_cast<size_t>(i)] = value;
}

void CPU::set_trace(std::ostream* trace, const SymbolTable* symbols) {
    trace_ = trace;
    trace_symbols_ = symbols;
}

// ------------------------------------------------------------------ helpers

void CPU::write_reg(uint8_t r, uint32_t value) {
    if (r == REG_ZERO) return;  // r0 is hard-wired to zero
    if (r == REG_SP) {
        // The stack pointer is bounds-checked like a hardware stack-limit
        // register, so runaway recursion is caught the moment sp leaves the
        // stack segment instead of silently trashing the heap.
        if (value < STACK_BASE) {
            throw CpuFault(FaultType::StackOverflow, value,
                           strfmt("stack overflow: sp would become 0x%08X, below the stack "
                                  "segment base 0x%08X (recursion too deep?)",
                                  value, STACK_BASE));
        }
        if (value > STACK_TOP) {
            throw CpuFault(FaultType::StackUnderflow, value,
                           strfmt("stack underflow: sp would become 0x%08X, above the stack top 0x%08X",
                                  value, STACK_TOP));
        }
    }
    regs_[r] = value;
    if (effects_.num_regs < static_cast<int>(effects_.regs.size())) {
        effects_.regs[static_cast<size_t>(effects_.num_regs++)] = {r, value};
    }
}

void CPU::push(uint32_t value) {
    const uint32_t sp = regs_[REG_SP];
    if (sp < STACK_BASE + 4) {
        throw CpuFault(FaultType::StackOverflow, sp - 4,
                       strfmt("stack overflow: push would move sp to 0x%08X, below the stack "
                              "segment base 0x%08X (recursion too deep?)",
                              sp - 4, STACK_BASE));
    }
    const uint32_t new_sp = sp - 4;
    mem_.write32(new_sp, value);
    effects_.mem_write = true;
    effects_.mem_addr = new_sp;
    effects_.mem_value = value;
    effects_.mem_size = AccessSize::Word;
    write_reg(REG_SP, new_sp);
}

uint32_t CPU::pop() {
    const uint32_t sp = regs_[REG_SP];
    if (sp >= STACK_TOP) {
        throw CpuFault(FaultType::StackUnderflow, sp,
                       strfmt("stack underflow: pop with empty stack (sp = 0x%08X)", sp));
    }
    const uint32_t value = mem_.read32(sp);
    write_reg(REG_SP, sp + 4);
    return value;
}

// ------------------------------------------------------------------ stage 1: fetch

uint32_t CPU::fetch() {
    // Memory::fetch32 enforces 4-byte alignment and execute permission, so
    // jumping into data, the heap, the stack or unmapped space traps here.
    const uint32_t raw = mem_.fetch32(pc_);
    next_pc_ = pc_ + 4;
    return raw;
}

// ------------------------------------------------------------------ stage 2: decode

Instruction CPU::decode(uint32_t raw) const {
    try {
        return c32::decode(raw);
    } catch (const CpuFault& f) {
        throw CpuFault(f.type(), pc_, f.what());
    }
}

// ------------------------------------------------------------------ stage 3: execute

void CPU::execute(const Instruction& in) {
    const uint32_t a = regs_[in.rs1];
    const uint32_t b = regs_[in.rs2];
    const uint32_t imm = static_cast<uint32_t>(in.imm);
    const int32_t sa = static_cast<int32_t>(a);
    const int32_t sb = static_cast<int32_t>(b);

    auto branch = [&](bool taken) {
        if (taken) {
            next_pc_ = pc_ + 4 + imm * 4u;  // imm is a signed instruction offset
            ++stats_.branches_taken;
            extra_cycles_ = 1;
        }
    };
    auto require_nonzero = [&](uint32_t divisor) {
        if (divisor == 0)
            throw CpuFault(FaultType::DivideByZero, pc_, "integer division by zero");
    };

    switch (in.op) {
        // ---- control ----
        case Opcode::HALT:
            state_ = CpuState::Halted;
            exit_code_ = 0;
            break;
        case Opcode::NOP: break;

        // ---- register-register ALU ----
        case Opcode::ADD: write_reg(in.rd, a + b); break;
        case Opcode::SUB: write_reg(in.rd, a - b); break;
        case Opcode::MUL: write_reg(in.rd, a * b); break;
        case Opcode::DIV:
            require_nonzero(b);
            // INT_MIN / -1 overflows in C++; the hardware defines it as INT_MIN.
            write_reg(in.rd, (sa == INT32_MIN && sb == -1) ? a : static_cast<uint32_t>(sa / sb));
            break;
        case Opcode::DIVU: require_nonzero(b); write_reg(in.rd, a / b); break;
        case Opcode::REM:
            require_nonzero(b);
            write_reg(in.rd, (sa == INT32_MIN && sb == -1) ? 0u : static_cast<uint32_t>(sa % sb));
            break;
        case Opcode::REMU: require_nonzero(b); write_reg(in.rd, a % b); break;
        case Opcode::AND: write_reg(in.rd, a & b); break;
        case Opcode::OR: write_reg(in.rd, a | b); break;
        case Opcode::XOR: write_reg(in.rd, a ^ b); break;
        case Opcode::SHL: write_reg(in.rd, a << (b & 31u)); break;
        case Opcode::SHR: write_reg(in.rd, a >> (b & 31u)); break;
        case Opcode::SRA: write_reg(in.rd, sra(a, b)); break;
        case Opcode::SLT: write_reg(in.rd, sa < sb ? 1u : 0u); break;
        case Opcode::SLTU: write_reg(in.rd, a < b ? 1u : 0u); break;
        case Opcode::NOT: write_reg(in.rd, ~a); break;

        // ---- register-immediate ALU ----
        case Opcode::ADDI: write_reg(in.rd, a + imm); break;
        case Opcode::ANDI: write_reg(in.rd, a & imm); break;
        case Opcode::ORI: write_reg(in.rd, a | imm); break;
        case Opcode::XORI: write_reg(in.rd, a ^ imm); break;
        case Opcode::SHLI: write_reg(in.rd, a << (imm & 31u)); break;
        case Opcode::SHRI: write_reg(in.rd, a >> (imm & 31u)); break;
        case Opcode::SRAI: write_reg(in.rd, sra(a, imm)); break;
        case Opcode::SLTI: write_reg(in.rd, sa < in.imm ? 1u : 0u); break;
        case Opcode::LUI: write_reg(in.rd, imm << 10); break;

        // ---- memory: word and byte granularity ----
        case Opcode::LW: write_reg(in.rd, mem_.read32(a + imm)); break;
        case Opcode::LB:
            write_reg(in.rd, static_cast<uint32_t>(sign_extend(mem_.read8(a + imm), 8)));
            break;
        case Opcode::LBU: write_reg(in.rd, mem_.read8(a + imm)); break;
        case Opcode::SW:
            mem_.write32(a + imm, b);
            effects_.mem_write = true;
            effects_.mem_addr = a + imm;
            effects_.mem_value = b;
            effects_.mem_size = AccessSize::Word;
            break;
        case Opcode::SB:
            mem_.write8(a + imm, static_cast<uint8_t>(b));
            effects_.mem_write = true;
            effects_.mem_addr = a + imm;
            effects_.mem_value = b & 0xFFu;
            effects_.mem_size = AccessSize::Byte;
            break;

        // ---- conditional branches ----
        case Opcode::BEQ: branch(a == b); break;
        case Opcode::BNE: branch(a != b); break;
        case Opcode::BLT: branch(sa < sb); break;
        case Opcode::BGE: branch(sa >= sb); break;
        case Opcode::BLTU: branch(a < b); break;
        case Opcode::BGEU: branch(a >= b); break;

        // ---- jumps and stack-based procedure calls ----
        case Opcode::JMP: next_pc_ = imm; break;
        case Opcode::JR: next_pc_ = a; break;
        case Opcode::CALL:
        case Opcode::CALLR:
            push(next_pc_);  // return address goes on the stack
            next_pc_ = (in.op == Opcode::CALL) ? imm : a;
            ++stats_.calls;
            if (++stats_.call_depth > stats_.max_call_depth) stats_.max_call_depth = stats_.call_depth;
            break;
        case Opcode::RET:
            next_pc_ = pop();
            if (next_pc_ != EXIT_TRAP) {  // main's own return is not a matched call
                ++stats_.returns;
                if (stats_.call_depth > 0) --stats_.call_depth;
            }
            break;
        case Opcode::PUSH: push(a); break;
        case Opcode::POP: write_reg(in.rd, pop()); break;
        case Opcode::ENTER: {
            // Build a stack frame:  [ret addr][saved fp] <- fp, then `imm` bytes of locals.
            push(regs_[REG_FP]);
            const uint32_t frame = regs_[REG_SP];
            write_reg(REG_FP, frame);
            write_reg(REG_SP, frame - imm);
            break;
        }
        case Opcode::LEAVE:
            write_reg(REG_SP, regs_[REG_FP]);
            write_reg(REG_FP, pop());
            break;

        // ---- operating system ----
        case Opcode::SYS:
            ++stats_.syscalls;
            syscall(imm);
            break;

        case Opcode::COUNT:
            throw CpuFault(FaultType::IllegalInstruction, pc_, "illegal opcode");
    }

    regs_[REG_ZERO] = 0;
    if (state_ == CpuState::Running) pc_ = next_pc_;
}

// ------------------------------------------------------------------ system calls

void CPU::syscall(uint32_t code) {
    const uint32_t a0 = regs_[REG_A0];
    switch (code) {
        case SYS_EXIT:
            exit_code_ = static_cast<int32_t>(a0);
            state_ = CpuState::Halted;
            break;
        case SYS_PRINT_INT: out_ << static_cast<int32_t>(a0); break;
        case SYS_PRINT_CHAR: out_.put(static_cast<char>(a0 & 0xFFu)); break;
        case SYS_PRINT_STR:
            for (uint32_t i = 0; i < SEGMENT_SIZE; ++i) {
                const uint8_t ch = mem_.read8(a0 + i);
                if (ch == 0) break;
                out_.put(static_cast<char>(ch));
            }
            break;
        case SYS_READ_INT: {
            long long v = 0;
            if (in_ >> v) {
                write_reg(REG_A0, static_cast<uint32_t>(v));
                write_reg(REG_A1, 0);
            } else {
                in_.clear();
                in_.ignore(1);  // skip one bad character so the program can retry
                write_reg(REG_A0, 0);
                write_reg(REG_A1, 1);
            }
            break;
        }
        case SYS_SBRK: {
            auto old = mem_.sbrk(static_cast<int32_t>(a0));
            write_reg(REG_A0, old ? *old : 0xFFFFFFFFu);
            break;
        }
        case SYS_PRINT_HEX: out_ << hex32(a0); break;
        case SYS_PRINT_UINT: out_ << a0; break;
        case SYS_READ_CHAR: {
            const int ch = in_.get();
            write_reg(REG_A0, ch == std::char_traits<char>::eof() ? 0xFFFFFFFFu
                                                                  : static_cast<uint32_t>(ch & 0xFF));
            if (ch == std::char_traits<char>::eof()) in_.clear();
            break;
        }
        case SYS_CYCLES: write_reg(REG_A0, static_cast<uint32_t>(stats_.cycles)); break;
        default:
            throw CpuFault(FaultType::InvalidSyscall, pc_, strfmt("unknown system call #%u", code));
    }
}

// ------------------------------------------------------------------ the cycle

bool CPU::step() {
    if (state_ != CpuState::Running) return false;
    const uint32_t pc = pc_;
    try {
        if (pc == EXIT_TRAP) {  // main returned
            exit_code_ = static_cast<int32_t>(regs_[REG_A0]);
            state_ = CpuState::Halted;
            return false;
        }
        effects_ = Effects{};
        extra_cycles_ = 0;

        const uint32_t raw = fetch();             // 1. fetch
        const Instruction ins = decode(raw);      // 2. decode
        execute(ins);                             // 3. execute + write-back

        last_ins_ = ins;
        last_pc_ = pc;
        ++stats_.instructions;
        stats_.cycles += op_info(ins.op).cycles + extra_cycles_;
        if (regs_[REG_SP] < stats_.min_sp) stats_.min_sp = regs_[REG_SP];
        if (trace_ != nullptr) trace_instruction(pc, ins);
    } catch (const CpuFault& f) {
        state_ = CpuState::Faulted;
        fault_ = FaultInfo{f.type(), pc, f.addr(), f.what()};
        return false;
    }
    return state_ == CpuState::Running;
}

RunResult CPU::run(uint64_t max_instructions) {
    uint64_t executed = 0;
    while (state_ == CpuState::Running) {
        if (max_instructions != 0 && executed >= max_instructions) return RunResult::LimitReached;
        step();
        ++executed;
    }
    return state_ == CpuState::Halted ? RunResult::Halted : RunResult::Faulted;
}

void CPU::trace_instruction(uint32_t pc, const Instruction& ins) {
    out_.flush();  // keep program output and trace in order on a terminal
    std::string effects;
    for (int i = 0; i < effects_.num_regs; ++i) {
        const auto& w = effects_.regs[static_cast<size_t>(i)];
        if (!effects.empty()) effects += "  ";
        effects += strfmt("%s=0x%08X", reg_name(w.reg), w.value);
    }
    if (effects_.mem_write) {
        if (!effects.empty()) effects += "  ";
        if (effects_.mem_size == AccessSize::Word)
            effects += strfmt("mem32[0x%08X]=0x%08X", effects_.mem_addr, effects_.mem_value);
        else
            effects += strfmt("mem8[0x%08X]=0x%02X", effects_.mem_addr, effects_.mem_value);
    }
    const std::string text = disassemble(ins, pc, trace_symbols_);
    *trace_ << strfmt("%8llu  %08X:  %08X  %-40s %s\n",
                      static_cast<unsigned long long>(stats_.instructions), pc, ins.raw,
                      text.c_str(), effects.c_str());
}

}  // namespace c32
