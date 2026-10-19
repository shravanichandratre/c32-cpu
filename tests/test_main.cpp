// test_main.cpp - unit tests for the C32 ISA, memory, CPU and assembler.
// Uses a tiny self-contained test framework (no external dependencies).
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "c32/assembler.hpp"
#include "c32/cpu.hpp"
#include "c32/disassembler.hpp"
#include "c32/memory.hpp"
#include "c32/program.hpp"

using namespace c32;

// ------------------------------------------------------------------ framework

namespace {

struct TestFailure {
    std::string where;
    std::string what;
};

std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};

std::string loc(const char* file, int line) { return std::string(file) + ":" + std::to_string(line); }

}  // namespace

#define TEST(name)                                     \
    static void name();                                \
    static Registrar registrar_##name(#name, name);    \
    static void name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) throw TestFailure{loc(__FILE__, __LINE__), #cond};  \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto va_ = (a);                                                                       \
        const auto vb_ = (b);                                                                       \
        if (!(va_ == vb_)) {                                                                        \
            std::ostringstream os_;                                                                 \
            os_ << #a << " == " << #b << "  (got " << +va_ << " vs " << +vb_ << ")";               \
            throw TestFailure{loc(__FILE__, __LINE__), os_.str()};                                  \
        }                                                                                           \
    } while (0)

#define CHECK_STR(a, b)                                                                             \
    do {                                                                                            \
        const std::string va_ = (a);                                                                \
        const std::string vb_ = (b);                                                                \
        if (va_ != vb_)                                                                             \
            throw TestFailure{loc(__FILE__, __LINE__), #a " == " #b "  (got \"" + va_ + "\" vs \"" + vb_ + "\")"}; \
    } while (0)

#define CHECK_THROWS(expr, Type)                                                                    \
    do {                                                                                            \
        bool thrown_ = false;                                                                       \
        try { (void)(expr); } catch (const Type&) { thrown_ = true; }                              \
        if (!thrown_) throw TestFailure{loc(__FILE__, __LINE__), #expr " did not throw " #Type};    \
    } while (0)

// ------------------------------------------------------------------ helpers

namespace {

// A complete machine running one program with captured I/O.
struct Machine {
    Program program;
    Memory memory;
    std::istringstream in;
    std::ostringstream out;
    CPU cpu;
    RunResult result;

    Machine(const std::string& source, const std::string& input)
        : program(assemble(source, "<test>")), in(input), cpu(memory, in, out) {
        load_program(program, memory, cpu);
        result = cpu.run(10000000);
    }
    uint32_t reg(const char* name) const { return cpu.reg(*parse_register(name)); }
    FaultType fault() const { return cpu.fault()->type; }
};

std::unique_ptr<Machine> run(const std::string& source, const std::string& input = "") {
    return std::make_unique<Machine>(source, input);
}

}  // namespace

// ------------------------------------------------------------------ ISA

TEST(encode_decode_roundtrip_all_opcodes) {
    for (size_t i = 0; i < kNumOpcodes; ++i) {
        Instruction ins;
        ins.op = static_cast<Opcode>(i);
        const OpInfo& info = op_info(ins.op);
        switch (info.syntax) {
            case Syntax::RRR: ins.rd = 3; ins.rs1 = 7; ins.rs2 = 15; break;
            case Syntax::RR: ins.rd = 9; ins.rs1 = 2; break;
            case Syntax::RRI: ins.rd = 1; ins.rs1 = 14;
                ins.imm = info.imm == ImmKind::Signed18 ? -1234 : (info.imm == ImmKind::Shamt ? 31 : 262143); break;
            case Syntax::RI: ins.rd = 12; ins.imm = 0x3FFFFF; break;
            case Syntax::Load: ins.rd = 4; ins.rs1 = 13; ins.imm = -131072; break;
            case Syntax::Store: ins.rs2 = 4; ins.rs1 = 13; ins.imm = 131071; break;
            case Syntax::Branch: ins.rs1 = 5; ins.rs2 = 6; ins.imm = -7; break;
            case Syntax::Jump: ins.imm = 0x00100abc; break;
            case Syntax::RegS: ins.rs1 = 11; break;
            case Syntax::RegD: ins.rd = 10; break;
            case Syntax::Imm: ins.imm = 4096; break;
            case Syntax::None: break;
        }
        const uint32_t word = encode(ins);
        CHECK_EQ(word >> 26, static_cast<uint32_t>(i));
        const Instruction d = decode(word);
        CHECK(d.op == ins.op);
        CHECK_EQ(d.rd, ins.rd);
        CHECK_EQ(d.rs1, ins.rs1);
        CHECK_EQ(d.rs2, ins.rs2);
        CHECK_EQ(d.imm, ins.imm);
        CHECK_EQ(encode(d), word);
    }
}

TEST(decode_rejects_undefined_opcode) {
    CHECK_THROWS(decode(0xFC000000u), CpuFault);
    CHECK_THROWS(decode(static_cast<uint32_t>(kNumOpcodes) << 26), CpuFault);
}

TEST(sign_extension) {
    CHECK_EQ(sign_extend(0x3FFFF, 18), -1);
    CHECK_EQ(sign_extend(0x20000, 18), -131072);
    CHECK_EQ(sign_extend(0x1FFFF, 18), 131071);
    CHECK_EQ(sign_extend(0x80, 8), -128);
    CHECK_EQ(sign_extend(0x7F, 8), 127);
}

TEST(register_names) {
    CHECK_EQ(*parse_register("r0"), 0);
    CHECK_EQ(*parse_register("R15"), 15);
    CHECK_EQ(*parse_register("zero"), 0);
    CHECK_EQ(*parse_register("a0"), 1);
    CHECK_EQ(*parse_register("t4"), 9);
    CHECK_EQ(*parse_register("s2"), 12);
    CHECK_EQ(*parse_register("fp"), 13);
    CHECK_EQ(*parse_register("SP"), 14);
    CHECK_EQ(*parse_register("at"), 15);
    CHECK(!parse_register("r16"));
    CHECK(!parse_register("x1"));
}

// ------------------------------------------------------------------ memory

TEST(memory_little_endian_bytes_and_words) {
    Memory m;
    m.write32(DATA_BASE, 0x11223344u);
    CHECK_EQ(m.read8(DATA_BASE + 0), 0x44);
    CHECK_EQ(m.read8(DATA_BASE + 1), 0x33);
    CHECK_EQ(m.read8(DATA_BASE + 2), 0x22);
    CHECK_EQ(m.read8(DATA_BASE + 3), 0x11);
    m.write8(DATA_BASE + 1, 0xAB);
    CHECK_EQ(m.read32(DATA_BASE), 0x1122AB44u);
}

TEST(memory_word_access_must_be_aligned) {
    Memory m;
    try { m.read32(DATA_BASE + 2); CHECK(false); }
    catch (const CpuFault& f) { CHECK(f.type() == FaultType::MisalignedAccess); CHECK_EQ(f.addr(), DATA_BASE + 2); }
    try { m.write32(STACK_BASE + 1, 0); CHECK(false); }
    catch (const CpuFault& f) { CHECK(f.type() == FaultType::MisalignedAccess); }
    m.write8(DATA_BASE + 3, 7);  // bytes may be anywhere
    CHECK_EQ(m.read8(DATA_BASE + 3), 7);
}

TEST(memory_unmapped_addresses_fault) {
    Memory m;
    for (uint32_t addr : {0u, 0x000FFFFCu, STACK_TOP, 0xFFFFFFF0u}) {
        try { m.read32(addr); CHECK(false); }
        catch (const CpuFault& f) { CHECK(f.type() == FaultType::SegmentationFault); }
    }
}

TEST(memory_text_is_read_only_and_only_text_executes) {
    Memory m;
    m.load(SegmentId::Text, {0x01, 0x00, 0x00, 0x04});
    CHECK_EQ(m.read32(TEXT_BASE), 0x04000001u);  // reading code is fine
    CHECK_EQ(m.fetch32(TEXT_BASE), 0x04000001u);
    try { m.write8(TEXT_BASE, 0); CHECK(false); }
    catch (const CpuFault& f) { CHECK(f.type() == FaultType::WriteViolation); }
    for (uint32_t addr : {DATA_BASE, STACK_BASE}) {
        try { m.fetch32(addr); CHECK(false); }
        catch (const CpuFault& f) { CHECK(f.type() == FaultType::ExecuteViolation); }
    }
}

TEST(memory_heap_limited_by_program_break) {
    Memory m;
    try { m.read8(HEAP_BASE); CHECK(false); }
    catch (const CpuFault& f) { CHECK(f.type() == FaultType::HeapViolation); }
    CHECK_EQ(*m.sbrk(8), HEAP_BASE);
    CHECK_EQ(m.brk(), HEAP_BASE + 8);
    m.write32(HEAP_BASE + 4, 99);
    CHECK_EQ(m.read32(HEAP_BASE + 4), 99u);
    try { m.write32(HEAP_BASE + 8, 1); CHECK(false); }
    catch (const CpuFault& f) { CHECK(f.type() == FaultType::HeapViolation); }
    CHECK(!m.sbrk(-16));                          // below heap base
    CHECK(!m.sbrk(static_cast<int32_t>(SEGMENT_SIZE)));  // beyond 1 MiB
    CHECK_EQ(*m.sbrk(-8), HEAP_BASE + 8);
    CHECK_EQ(m.brk(), HEAP_BASE);
}

// ------------------------------------------------------------------ CPU

TEST(alu_operations) {
    auto m = run(R"(
        main:
            li   t0, 100
            li   t1, -7
            add  a0, t0, t1      ; 93
            sub  a1, t0, t1      ; 107
            mul  a2, t0, t1      ; -700
            div  a3, t0, t1      ; -14 (truncates toward zero)
            rem  s0, t0, t1      ; 2
            and  s1, t0, t1      ; 100 & -7 = 96
            xor  s2, t0, t1
            halt
    )");
    CHECK(m->result == RunResult::Halted);
    CHECK_EQ(static_cast<int32_t>(m->reg("a0")), 93);
    CHECK_EQ(static_cast<int32_t>(m->reg("a1")), 107);
    CHECK_EQ(static_cast<int32_t>(m->reg("a2")), -700);
    CHECK_EQ(static_cast<int32_t>(m->reg("a3")), -14);
    CHECK_EQ(static_cast<int32_t>(m->reg("s0")), 2);
    CHECK_EQ(static_cast<int32_t>(m->reg("s1")), 96);
    CHECK_EQ(m->reg("s2"), 100u ^ static_cast<uint32_t>(-7));
}

TEST(shifts_and_comparisons) {
    auto m = run(R"(
        main:
            li   t0, -16
            srai a0, t0, 2       ; -4
            shri a1, t0, 28      ; 0xF
            shli a2, t0, 1       ; -32
            slt  a3, t0, zero    ; 1  (signed)
            sltu s0, t0, zero    ; 0  (unsigned: 0xFFFFFFF0 > 0)
            slti s1, t0, -15     ; 1
            not  s2, zero        ; 0xFFFFFFFF
            halt
    )");
    CHECK_EQ(static_cast<int32_t>(m->reg("a0")), -4);
    CHECK_EQ(m->reg("a1"), 0xFu);
    CHECK_EQ(static_cast<int32_t>(m->reg("a2")), -32);
    CHECK_EQ(m->reg("a3"), 1u);
    CHECK_EQ(m->reg("s0"), 0u);
    CHECK_EQ(m->reg("s1"), 1u);
    CHECK_EQ(m->reg("s2"), 0xFFFFFFFFu);
}

TEST(division_edge_cases) {
    auto m = run(R"(
        main:
            li   t0, 0x80000000
            li   t1, -1
            div  a0, t0, t1      ; INT_MIN / -1 = INT_MIN (defined, no trap)
            rem  a1, t0, t1      ; 0
            li   t2, 7
            divu a2, t1, t2      ; 0xFFFFFFFF / 7
            halt
    )");
    CHECK_EQ(m->reg("a0"), 0x80000000u);
    CHECK_EQ(m->reg("a1"), 0u);
    CHECK_EQ(m->reg("a2"), 0xFFFFFFFFu / 7);
    auto z = run("main: li t0, 1\n div a0, t0, zero\n halt\n");
    CHECK(z->result == RunResult::Faulted);
    CHECK(z->fault() == FaultType::DivideByZero);
}

TEST(load_immediate_any_32bit_value) {
    auto m = run(R"(
        main:
            li a0, 0xDEADBEEF
            li a1, -1
            li a2, -131072
            li a3, 131072
            li s0, 0
            halt
    )");
    CHECK_EQ(m->reg("a0"), 0xDEADBEEFu);
    CHECK_EQ(m->reg("a1"), 0xFFFFFFFFu);
    CHECK_EQ(static_cast<int32_t>(m->reg("a2")), -131072);
    CHECK_EQ(m->reg("a3"), 131072u);
    CHECK_EQ(m->reg("s0"), 0u);
    // small constants use one instruction, large ones two (LUI + ORI)
    CHECK_EQ(m->program.text.size(), static_cast<size_t>((2 + 1 + 1 + 2 + 1 + 1) * 4));
}

TEST(zero_register_is_hardwired) {
    auto m = run("main: li t0, 5\n add zero, t0, t0\n addi r0, r0, 9\n mov a0, zero\n halt\n");
    CHECK_EQ(m->cpu.reg(0), 0u);
    CHECK_EQ(m->reg("a0"), 0u);
}

TEST(byte_loads_sign_and_zero_extend) {
    auto m = run(R"(
        .data
        b:  .byte 0x80, 0x7F
        .text
        main:
            la  t0, b
            lb  a0, 0(t0)
            lbu a1, 0(t0)
            lb  a2, 1(t0)
            li  t1, 0x1FF
            sb  t1, 1(t0)        ; stores only the low byte (0xFF)
            lbu a3, 1(t0)
            halt
    )");
    CHECK_EQ(static_cast<int32_t>(m->reg("a0")), -128);
    CHECK_EQ(m->reg("a1"), 128u);
    CHECK_EQ(m->reg("a2"), 127u);
    CHECK_EQ(m->reg("a3"), 0xFFu);
}

TEST(branches_and_loops) {
    auto m = run(R"(
        main:
            li   t0, 1
            li   a0, 0
        loop:
            add  a0, a0, t0
            inc  t0
            li   t1, 100
            ble  t0, t1, loop    ; sum 1..100
            li   t2, -1
            li   t3, 1
            li   a1, 0
            bltu t2, t3, skip    ; unsigned: 0xFFFFFFFF < 1 is false
            li   a1, 1
        skip:
            halt
    )");
    CHECK_EQ(m->reg("a0"), 5050u);
    CHECK_EQ(m->reg("a1"), 1u);
}

TEST(recursive_factorial) {
    auto m = run(R"(
        main:
            li   a0, 10
            call fact
            halt
        fact:
            enter 4
            sw   a0, -4(fp)
            li   t0, 1
            bgt  a0, t0, rec
            li   a0, 1
            leave
            ret
        rec:
            addi a0, a0, -1
            call fact
            lw   t0, -4(fp)
            mul  a0, a0, t0
            leave
            ret
    )");
    CHECK(m->result == RunResult::Halted);
    CHECK_EQ(m->reg("a0"), 3628800u);
    CHECK_EQ(m->cpu.stats().max_call_depth, 10u);
    CHECK_EQ(m->cpu.stats().calls, m->cpu.stats().returns);
    CHECK_EQ(m->reg("sp"), STACK_TOP - 4);  // stack fully unwound (exit trap remains)
    CHECK_EQ(STACK_TOP - m->cpu.stats().min_sp, 4u + 10u * 12u);
}

TEST(recursive_fibonacci_preserves_saved_registers) {
    auto m = run(R"(
        main:
            li   s0, 1234
            li   a0, 15
            call fib
            halt
        fib:
            li   t0, 2
            blt  a0, t0, base
            push s0
            push s1
            mov  s0, a0
            addi a0, s0, -1
            call fib
            mov  s1, a0
            addi a0, s0, -2
            call fib
            add  a0, a0, s1
            pop  s1
            pop  s0
        base:
            ret
    )");
    CHECK_EQ(m->reg("a0"), 610u);
    CHECK_EQ(m->reg("s0"), 1234u);
}

TEST(indirect_calls) {
    auto m = run(R"(
        main:
            la    t0, triple
            li    a0, 14
            callr t0
            halt
        triple:
            add   t1, a0, a0
            add   a0, t1, a0
            ret
    )");
    CHECK_EQ(m->reg("a0"), 42u);
}

TEST(stack_overflow_is_detected) {
    auto m = run("main: call main\n");
    CHECK(m->result == RunResult::Faulted);
    CHECK(m->fault() == FaultType::StackOverflow);
    // 1 MiB of 4-byte return addresses, minus the exit-trap slot
    CHECK_EQ(m->cpu.stats().max_call_depth, SEGMENT_SIZE / 4 - 1);
}

TEST(stack_underflow_is_detected) {
    auto m = run("main: pop t0\n pop t1\n halt\n");  // first pop takes the exit trap
    CHECK(m->result == RunResult::Faulted);
    CHECK(m->fault() == FaultType::StackUnderflow);
    CHECK_EQ(m->reg("t0"), EXIT_TRAP);
}

TEST(sp_is_bounds_checked) {
    auto m = run("main: li t0, 0x003FFFF0\n mov sp, t0\n halt\n");
    CHECK(m->fault() == FaultType::StackOverflow);
}

TEST(return_from_main_halts_with_a0) {
    auto m = run("main: li a0, 42\n ret\n");
    CHECK(m->result == RunResult::Halted);
    CHECK_EQ(m->cpu.exit_code(), 42);
}

TEST(running_off_the_end_halts) {
    auto m = run("main: li a0, 3\n");  // zero-filled text decodes as HALT
    CHECK(m->result == RunResult::Halted);
    CHECK_EQ(m->cpu.exit_code(), 0);
}

TEST(execute_data_faults) {
    auto m = run(".data\nx: .word 0\n.text\nmain: la t0, x\n jr t0\n");
    CHECK(m->fault() == FaultType::ExecuteViolation);
    CHECK_EQ(m->cpu.fault()->pc, DATA_BASE);
}

TEST(syscalls_io) {
    auto m = run(R"(
        .data
        s: .asciiz "hi "
        .text
        main:
            la   a0, s
            sys  SYS_PRINT_STR
            li   a0, -5
            sys  SYS_PRINT_INT
            li   a0, ' '
            sys  SYS_PRINT_CHAR
            li   a0, -1
            sys  SYS_PRINT_UINT
            li   a0, ' '
            sys  SYS_PRINT_CHAR
            li   a0, 255
            sys  SYS_PRINT_HEX
            sys  SYS_READ_INT
            mov  s0, a0
            mov  s1, a1
            sys  SYS_READ_INT
            mov  s2, a1
            li   a0, 7
            sys  SYS_EXIT
    )", "  123 ");
    CHECK_STR(m->out.str(), "hi -5 4294967295 0x000000FF");
    CHECK_EQ(m->reg("s0"), 123u);
    CHECK_EQ(m->reg("s1"), 0u);
    CHECK_EQ(m->reg("s2"), 1u);  // EOF
    CHECK_EQ(m->cpu.exit_code(), 7);
}

TEST(sbrk_allocates_heap) {
    auto m = run(R"(
        main:
            li   a0, 12
            sys  SYS_SBRK
            mov  s0, a0          ; old break = HEAP_BASE
            li   t0, 77
            sw   t0, 8(s0)
            lw   s1, 8(s0)
            li   a0, 0
            sys  SYS_SBRK
            mov  s2, a0          ; HEAP_BASE + 12
            li   a0, 0x200000    ; more than the heap segment can hold
            sys  SYS_SBRK
            halt
    )");
    CHECK_EQ(m->reg("s0"), HEAP_BASE);
    CHECK_EQ(m->reg("s1"), 77u);
    CHECK_EQ(m->reg("s2"), HEAP_BASE + 12);
    CHECK_EQ(m->reg("a0"), 0xFFFFFFFFu);
}

TEST(invalid_syscall_faults) {
    auto m = run("main: sys 999\n");
    CHECK(m->fault() == FaultType::InvalidSyscall);
}

// ------------------------------------------------------------------ assembler

TEST(assembler_data_directives_and_labels) {
    const Program p = assemble(R"(
        .data
        a:  .byte 1, 2, 3
        w:  .word 0x01020304, a     ; auto-aligned to 4; labels allowed
        s:  .asciiz "A\n"
            .align 8
        e:  .space 2
        .text
        main: halt
    )");
    CHECK_EQ(p.symbols.at("a"), DATA_BASE);
    CHECK_EQ(p.symbols.at("w"), DATA_BASE + 4);
    CHECK_EQ(p.symbols.at("s"), DATA_BASE + 12);
    CHECK_EQ(p.symbols.at("e"), DATA_BASE + 16);
    CHECK_EQ(p.data.size(), 18u);
    CHECK_EQ(p.data[3], 0);  // padding
    CHECK_EQ(p.data[4], 0x04);
    CHECK_EQ(p.data[8], 0x00);  // low byte of &a (0x00200000)
    CHECK_EQ(p.data[10], 0x20);
    CHECK_EQ(p.data[12], 'A');
    CHECK_EQ(p.data[13], '\n');
    CHECK_EQ(p.data[14], 0);
    CHECK_EQ(p.entry, TEXT_BASE);
}

TEST(assembler_equ_comments_and_char_literals) {
    auto m = run(R"(
        .equ  COUNT, 3 + 4      # comment styles: ; # //
        main:                   // label on its own line
            li  a0, COUNT - 1   ; 6
            li  a1, ';'         ; char literal containing a comment char
            halt
    )");
    CHECK_EQ(m->reg("a0"), 6u);
    CHECK_EQ(m->reg("a1"), static_cast<uint32_t>(';'));
}

TEST(assembler_symbolic_memory_operands) {
    auto m = run(R"(
        .data
        counter: .word 41
        .text
        main:
            lw  t0, counter
            addi t0, t0, 1
            sw  t0, counter
            lw  a0, counter
            halt
    )");
    CHECK_EQ(m->reg("a0"), 42u);
}

TEST(assembler_reports_errors) {
    CHECK_THROWS(assemble("main: frob a0, a1\n"), AssemblyError);           // unknown mnemonic
    CHECK_THROWS(assemble("main: jmp nowhere\n"), AssemblyError);          // undefined label
    CHECK_THROWS(assemble("main: addi a0, a0, 200000\n"), AssemblyError);  // immediate range
    CHECK_THROWS(assemble("main: andi a0, a0, -1\n"), AssemblyError);      // zero-extended imm
    CHECK_THROWS(assemble("main: add a0, a1\n"), AssemblyError);           // operand count
    CHECK_THROWS(assemble("main: add a0, a1, r16\n"), AssemblyError);      // bad register
    CHECK_THROWS(assemble(".data\n add a0, a0, a0\n"), AssemblyError);     // instr in .data
    CHECK_THROWS(assemble(".word 5\n"), AssemblyError);                    // data in .text
    CHECK_THROWS(assemble("x: nop\nx: nop\n"), AssemblyError);             // duplicate label
    CHECK_THROWS(assemble("sp: nop\n"), AssemblyError);                    // register as label
    CHECK_THROWS(assemble("main: enter 6\n"), AssemblyError);              // misaligned frame
    CHECK_THROWS(assemble(".data\ns: .asciiz \"oops\n"), AssemblyError);   // unterminated string
    try {
        assemble("nop\nnop\n  bogus\n", "file.asm");
        CHECK(false);
    } catch (const AssemblyError& e) {
        CHECK_EQ(e.line(), 3);
        CHECK(std::string(e.what()).find("file.asm:3") == 0);
    }
}

TEST(assembler_entry_point) {
    CHECK_EQ(assemble("nop\nmain: halt\n").entry, TEXT_BASE + 4);
    CHECK_EQ(assemble(".entry go\nnop\nnop\ngo: halt\n").entry, TEXT_BASE + 8);
}

TEST(binary_file_roundtrip) {
    const Program p = assemble(".data\nmsg: .asciiz \"x\"\n.text\nnop\nmain: la a0, msg\n halt\n");
    const std::string path = (std::filesystem::temp_directory_path() / "c32_roundtrip_test.c32").string();
    save_binary(p, path);
    CHECK(is_binary_file(path));
    const Program q = load_binary(path);
    std::remove(path.c_str());
    CHECK(q.text == p.text);
    CHECK(q.data == p.data);
    CHECK_EQ(q.entry, p.entry);
    CHECK(q.symbols == p.symbols);
}

TEST(disassembler_output) {
    const Program p = assemble("main: addi a0, sp, -8\n lw t0, 4(fp)\nloop: bne a0, zero, loop\n call main\n sys SYS_PRINT_INT\n");
    const SymbolTable syms = p.address_symbols();
    auto word = [&](size_t i) {
        return static_cast<uint32_t>(p.text[i * 4]) | (static_cast<uint32_t>(p.text[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(p.text[i * 4 + 2]) << 16) | (static_cast<uint32_t>(p.text[i * 4 + 3]) << 24);
    };
    CHECK_STR(disassemble_word(word(0), TEXT_BASE, &syms), "addi   a0, sp, -8");
    CHECK_STR(disassemble_word(word(1), TEXT_BASE + 4, &syms), "lw     t0, 4(fp)");
    CHECK_STR(disassemble_word(word(2), TEXT_BASE + 8, &syms), "bne    a0, zero, 0x00100008 <loop>");
    CHECK_STR(disassemble_word(word(3), TEXT_BASE + 12, &syms), "call   0x00100000 <main>");
    CHECK_STR(disassemble_word(word(4), TEXT_BASE + 16, &syms), "sys    1  (PRINT_INT)");
    CHECK_STR(disassemble_word(0xFC000000u, TEXT_BASE, &syms), ".word   0xFC000000   (illegal opcode)");
}

// ------------------------------------------------------------------ main

int main() {
    int passed = 0, failed = 0;
    for (const auto& [name, fn] : registry()) {
        try {
            fn();
            ++passed;
            std::cout << "  PASS  " << name << "\n";
        } catch (const TestFailure& f) {
            ++failed;
            std::cout << "  FAIL  " << name << "\n        " << f.where << ": " << f.what << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  FAIL  " << name << "\n        unexpected exception: " << e.what() << "\n";
        }
    }
    std::cout << "\nunit tests: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
