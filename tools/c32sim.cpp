// c32sim - run a C32 program (assembly source or .c32 binary) on the emulator.
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "c32/assembler.hpp"
#include "c32/cpu.hpp"
#include "c32/disassembler.hpp"
#include "c32/memory.hpp"
#include "c32/program.hpp"
#include "c32/util.hpp"

using namespace c32;

namespace {

constexpr int EXIT_FAULT = 3;
constexpr int EXIT_LIMIT = 4;
constexpr uint64_t DEFAULT_LIMIT = 100000000ULL;  // 100 million instructions

void usage() {
    std::cerr <<
        "usage: c32sim [options] <program.asm | program.c32>\n"
        "\n"
        "  -t, --trace        log every executed instruction to stderr\n"
        "  -d, --debug        start the interactive debugger\n"
        "  -s, --stats        print execution statistics to stderr when the program stops\n"
        "  -r, --regs         print the register file to stderr when the program stops\n"
        "  -m, --max N        stop after N instructions (default 100000000, 0 = unlimited)\n"
        "      --dump A:N     hex-dump N bytes of memory starting at address A when stopped\n"
        "  -h, --help         show this help\n"
        "\n"
        "exit status: the program's exit code, 1 = load/assembly error,\n"
        "             3 = CPU fault, 4 = instruction limit reached\n";
}

bool parse_u32(const std::string& s, uint32_t& out) {
    try {
        size_t used = 0;
        unsigned long v = std::stoul(s, &used, 0);
        if (used != s.size() || v > 0xFFFFFFFFul) return false;
        out = static_cast<uint32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

void print_regs(const CPU& cpu, std::ostream& os) {
    os << "registers:\n";
    for (int i = 0; i < NUM_REGS; ++i) {
        os << strfmt("  r%-2d %-4s 0x%08X %11d", i, reg_name(static_cast<uint8_t>(i)), cpu.reg(i),
                     static_cast<int32_t>(cpu.reg(i)));
        os << ((i % 2 == 1) ? "\n" : "   ");
    }
    os << strfmt("  pc       0x%08X\n", cpu.pc());
}

void print_stats(const CPU& cpu, const Memory& mem, std::ostream& os) {
    const CpuStats& s = cpu.stats();
    const double cpi = s.instructions ? static_cast<double>(s.cycles) / static_cast<double>(s.instructions) : 0.0;
    os << "---- execution statistics ----\n";
    os << strfmt("  instructions      : %llu\n", static_cast<unsigned long long>(s.instructions));
    os << strfmt("  cycles (model)    : %llu  (CPI %.2f)\n", static_cast<unsigned long long>(s.cycles), cpi);
    os << strfmt("  branches taken    : %llu\n", static_cast<unsigned long long>(s.branches_taken));
    os << strfmt("  calls / returns   : %llu / %llu\n", static_cast<unsigned long long>(s.calls),
                 static_cast<unsigned long long>(s.returns));
    os << strfmt("  max call depth    : %u\n", s.max_call_depth);
    os << strfmt("  peak stack usage  : %u bytes (lowest sp 0x%08X)\n", STACK_TOP - s.min_sp, s.min_sp);
    os << strfmt("  heap in use       : %u bytes (break 0x%08X)\n", mem.brk() - HEAP_BASE, mem.brk());
    os << strfmt("  syscalls          : %llu\n", static_cast<unsigned long long>(s.syscalls));
    os << "  segment  range                     perms       reads      writes     fetches\n";
    for (size_t i = 0; i < kNumSegments; ++i) {
        const Segment& seg = mem.segment(static_cast<SegmentId>(i));
        os << strfmt("  %-6s   0x%08X-0x%08X   %c%c%c   %10llu  %10llu  %10llu\n", seg.name, seg.base,
                     seg.end() - 1, seg.readable ? 'r' : '-', seg.writable ? 'w' : '-',
                     seg.executable ? 'x' : '-', static_cast<unsigned long long>(seg.reads),
                     static_cast<unsigned long long>(seg.writes), static_cast<unsigned long long>(seg.fetches));
    }
}

void hexdump(const Memory& mem, uint32_t addr, uint32_t len, std::ostream& os) {
    for (uint32_t off = 0; off < len; off += 16) {
        os << strfmt("  %08X: ", addr + off);
        std::string ascii;
        for (uint32_t i = 0; i < 16 && off + i < len; ++i) {
            uint8_t b = 0;
            if (mem.peek8(addr + off + i, b)) {
                os << strfmt("%02X ", b);
                ascii += (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
            } else {
                os << "-- ";
                ascii += ' ';
            }
        }
        os << " |" << ascii << "|\n";
    }
}

void report_fault(const CPU& cpu, const Memory& mem, const SymbolTable& syms, std::ostream& os) {
    const FaultInfo& f = *cpu.fault();
    os << "*** CPU FAULT: " << fault_name(f.type) << " at pc=" << hex32(f.pc) << symbolize(f.pc, &syms)
       << " (address " << hex32(f.addr) << ")\n";
    os << "    " << f.message << "\n";
    uint32_t raw = 0;
    if (mem.peek32(f.pc, raw)) {
        const bool not_code = f.type == FaultType::ExecuteViolation;
        os << (not_code ? "    word at pc (would decode as): " : "    instruction: ")
           << disassemble_word(raw, f.pc, &syms) << "\n";
    }
    const uint64_t n = cpu.stats().instructions;
    os << "    after " << n << (n == 1 ? " instruction" : " instructions") << ", call depth "
       << cpu.stats().call_depth << "\n";
}

void print_location(const CPU& cpu, const Memory& mem, const SymbolTable& syms) {
    uint32_t raw = 0;
    if (cpu.state() != CpuState::Running) {
        std::cout << "program is " << (cpu.state() == CpuState::Halted ? "halted" : "faulted") << "\n";
        return;
    }
    if (mem.peek32(cpu.pc(), raw))
        std::cout << strfmt("=> %08X:  %08X  ", cpu.pc(), raw) << disassemble_word(raw, cpu.pc(), &syms) << "\n";
    else
        std::cout << strfmt("=> %08X:  (unmapped)\n", cpu.pc());
}

bool resolve_address(const std::string& tok, const Program& prog, const CPU& cpu, uint32_t& out) {
    auto it = prog.symbols.find(tok);
    if (it != prog.symbols.end()) { out = it->second; return true; }
    if (auto r = parse_register(tok)) { out = cpu.reg(*r); return true; }
    if (tok == "pc") { out = cpu.pc(); return true; }
    return parse_u32(tok, out);
}

void debugger(CPU& cpu, Memory& mem, const Program& prog, const SymbolTable& syms) {
    std::set<uint32_t> breakpoints;
    const char* help =
        "commands:\n"
        "  s [n]            step n instructions (default 1)\n"
        "  c                continue until breakpoint / halt / fault\n"
        "  b <addr|label>   set breakpoint          d [addr|label]  delete breakpoint(s)\n"
        "  bl               list breakpoints\n"
        "  r                show registers\n"
        "  m <addr> [n]     show n words of memory (addr may be a label or register)\n"
        "  st [n]           show n words at the top of the stack\n"
        "  x [addr] [n]     disassemble n instructions (default: at pc)\n"
        "  seg              show segments        stats   show statistics\n"
        "  q                quit\n";
    std::cout << "C32 debugger - type 'h' for help\n";
    print_location(cpu, mem, syms);
    std::string line;
    while (std::cout << "(c32db) " << std::flush, std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::vector<std::string> t;
        for (std::string w; ss >> w;) t.push_back(w);
        if (t.empty()) continue;
        const std::string& cmd = t[0];
        auto report_stop = [&]() {
            std::cout.flush();
            if (cpu.state() == CpuState::Halted) std::cout << "\n[program halted, exit code " << cpu.exit_code() << "]\n";
            if (cpu.state() == CpuState::Faulted) report_fault(cpu, mem, syms, std::cout);
        };
        if (cmd == "q" || cmd == "quit") break;
        if (cmd == "h" || cmd == "help") { std::cout << help; continue; }
        if (cmd == "s" || cmd == "step") {
            uint32_t n = 1;
            if (t.size() > 1 && !parse_u32(t[1], n)) { std::cout << "bad count\n"; continue; }
            for (uint32_t i = 0; i < n && cpu.state() == CpuState::Running; ++i) {
                const uint64_t before = cpu.stats().instructions;
                cpu.step();
                if (cpu.stats().instructions != before) {  // an instruction actually completed
                    const auto& e = cpu.last_effects();
                    std::string fx;
                    for (int k = 0; k < e.num_regs; ++k)
                        fx += strfmt("  %s=0x%08X", reg_name(e.regs[static_cast<size_t>(k)].reg),
                                     e.regs[static_cast<size_t>(k)].value);
                    if (e.mem_write) fx += strfmt("  mem[0x%08X]=0x%X", e.mem_addr, e.mem_value);
                    std::cout << strfmt("   %08X:  ", cpu.last_pc())
                              << disassemble(cpu.last_instruction(), cpu.last_pc(), &syms) << fx << "\n";
                }
            }
            report_stop();
            print_location(cpu, mem, syms);
            continue;
        }
        if (cmd == "c" || cmd == "continue") {
            bool first = true;
            while (cpu.state() == CpuState::Running) {
                if (!first && breakpoints.count(cpu.pc())) {
                    std::cout << "breakpoint at " << hex32(cpu.pc()) << symbolize(cpu.pc(), &syms) << "\n";
                    break;
                }
                first = false;
                cpu.step();
            }
            report_stop();
            print_location(cpu, mem, syms);
            continue;
        }
        if (cmd == "b" || cmd == "break") {
            uint32_t a = 0;
            if (t.size() < 2 || !resolve_address(t[1], prog, cpu, a)) { std::cout << "usage: b <addr|label>\n"; continue; }
            breakpoints.insert(a);
            std::cout << "breakpoint set at " << hex32(a) << symbolize(a, &syms) << "\n";
            continue;
        }
        if (cmd == "d" || cmd == "delete") {
            uint32_t a = 0;
            if (t.size() < 2) { breakpoints.clear(); std::cout << "all breakpoints deleted\n"; continue; }
            if (!resolve_address(t[1], prog, cpu, a)) { std::cout << "bad address\n"; continue; }
            breakpoints.erase(a);
            continue;
        }
        if (cmd == "bl") {
            for (uint32_t a : breakpoints) std::cout << "  " << hex32(a) << symbolize(a, &syms) << "\n";
            continue;
        }
        if (cmd == "r" || cmd == "regs") { print_regs(cpu, std::cout); continue; }
        if (cmd == "m" || cmd == "mem" || cmd == "st" || cmd == "stack") {
            const bool stack = cmd == "st" || cmd == "stack";
            uint32_t a = cpu.reg(REG_SP), n = 8;
            size_t ni = 1;
            if (!stack) {
                if (t.size() < 2 || !resolve_address(t[1], prog, cpu, a)) { std::cout << "usage: m <addr> [n]\n"; continue; }
                ni = 2;
            }
            if (t.size() > ni && !parse_u32(t[ni], n)) { std::cout << "bad count\n"; continue; }
            a &= ~3u;
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t w = 0;
                const uint32_t addr = a + 4 * i;
                if (stack && addr >= STACK_TOP) break;
                if (mem.peek32(addr, w))
                    std::cout << strfmt("  %08X:  0x%08X  %11d", addr, w, static_cast<int32_t>(w))
                              << (stack && addr == cpu.reg(REG_FP) ? "   <- fp" : "")
                              << (stack && addr == cpu.reg(REG_SP) ? "   <- sp" : "") << "\n";
                else
                    std::cout << strfmt("  %08X:  (unmapped)\n", addr);
            }
            continue;
        }
        if (cmd == "x" || cmd == "dis") {
            uint32_t a = cpu.pc(), n = 8;
            if (t.size() > 1 && !resolve_address(t[1], prog, cpu, a)) { std::cout << "bad address\n"; continue; }
            if (t.size() > 2 && !parse_u32(t[2], n)) { std::cout << "bad count\n"; continue; }
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t w = 0;
                const uint32_t addr = (a & ~3u) + 4 * i;
                if (!mem.peek32(addr, w)) break;
                std::cout << (addr == cpu.pc() ? "=> " : "   ") << strfmt("%08X:  %08X  ", addr, w)
                          << disassemble_word(w, addr, &syms) << (breakpoints.count(addr) ? "   [bp]" : "") << "\n";
            }
            continue;
        }
        if (cmd == "seg") {
            for (size_t i = 0; i < kNumSegments; ++i) {
                const Segment& s = mem.segment(static_cast<SegmentId>(i));
                std::cout << strfmt("  %-6s 0x%08X-0x%08X %c%c%c\n", s.name, s.base, s.end() - 1,
                                    s.readable ? 'r' : '-', s.writable ? 'w' : '-', s.executable ? 'x' : '-');
            }
            std::cout << strfmt("  program break: 0x%08X  sp: 0x%08X  fp: 0x%08X\n", mem.brk(), cpu.reg(REG_SP),
                                cpu.reg(REG_FP));
            continue;
        }
        if (cmd == "stats") { print_stats(cpu, mem, std::cout); continue; }
        std::cout << "unknown command '" << cmd << "' (type 'h' for help)\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    bool trace = false, debug = false, stats = false, regs = false;
    uint64_t limit = DEFAULT_LIMIT;
    std::vector<std::pair<uint32_t, uint32_t>> dumps;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { usage(); return 0; }
        else if (arg == "-t" || arg == "--trace") trace = true;
        else if (arg == "-d" || arg == "--debug") debug = true;
        else if (arg == "-s" || arg == "--stats") stats = true;
        else if (arg == "-r" || arg == "--regs") regs = true;
        else if ((arg == "-m" || arg == "--max") && i + 1 < argc) {
            try { limit = std::stoull(argv[++i], nullptr, 0); }
            catch (...) { std::cerr << "c32sim: bad instruction limit\n"; return 1; }
        } else if (arg == "--dump" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const size_t colon = spec.find(':');
            uint32_t a = 0, n = 0;
            if (colon == std::string::npos || !parse_u32(spec.substr(0, colon), a) || !parse_u32(spec.substr(colon + 1), n)) {
                std::cerr << "c32sim: --dump expects ADDR:LEN, e.g. 0x00200000:64\n";
                return 1;
            }
            dumps.emplace_back(a, n);
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "c32sim: unknown option '" << arg << "'\n";
            usage();
            return 1;
        } else {
            path = arg;
        }
    }
    if (path.empty()) { usage(); return 1; }

    Program prog;
    try {
        prog = load_program_file(path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    Memory mem;
    CPU cpu(mem, std::cin, std::cout);
    load_program(prog, mem, cpu);
    const SymbolTable syms = prog.address_symbols();
    if (trace) cpu.set_trace(&std::cerr, &syms);

    int status = 0;
    if (debug) {
        debugger(cpu, mem, prog, syms);
        status = cpu.state() == CpuState::Faulted ? EXIT_FAULT : (cpu.exit_code() & 0xFF);
    } else {
        const RunResult r = cpu.run(limit);
        std::cout.flush();
        if (r == RunResult::Faulted) {
            report_fault(cpu, mem, syms, std::cerr);
            status = EXIT_FAULT;
        } else if (r == RunResult::LimitReached) {
            std::cerr << "*** instruction limit of " << limit << " reached at pc=" << hex32(cpu.pc())
                      << " (use --max to raise it, 0 = unlimited)\n";
            status = EXIT_LIMIT;
        } else {
            status = cpu.exit_code() & 0xFF;
        }
    }
    if (regs) print_regs(cpu, std::cerr);
    if (stats) print_stats(cpu, mem, std::cerr);
    for (const auto& d : dumps) {
        std::cerr << "memory " << hex32(d.first) << " (" << d.second << " bytes):\n";
        hexdump(mem, d.first, d.second, std::cerr);
    }
    return status;
}
