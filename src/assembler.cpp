// assembler.cpp - two-pass assembler for C32.
//
// Pass 1 parses every line, records labels and computes the size and address
// of every statement (pseudo-instructions may expand to several machine
// instructions; their size is decided here). Pass 2 evaluates operands with
// the complete symbol table and emits machine code and data bytes.
#include "c32/assembler.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "c32/isa.hpp"
#include "c32/layout.hpp"
#include "c32/util.hpp"

namespace c32 {

AssemblyError::AssemblyError(const std::string& file, int line, const std::string& message)
    : std::runtime_error(file + ":" + std::to_string(line) + ": error: " + message), line_(line) {}

namespace {

constexpr int64_t S18_MIN = -(1 << 17);
constexpr int64_t S18_MAX = (1 << 17) - 1;
constexpr int64_t U18_MAX = (1 << 18) - 1;
constexpr int64_t U22_MAX = (1 << 22) - 1;
constexpr int64_t WORD_MIN = -2147483648LL;
constexpr int64_t WORD_MAX = 4294967295LL;

enum class Section { Text = 0, Data = 1 };

struct Statement {
    int line = 0;
    Section section = Section::Text;
    std::string op;  // upper-case mnemonic, or directive with leading '.'
    std::vector<std::string> args;
    uint32_t addr = 0;
    uint32_t size = 0;  // bytes
};

struct MemOperand {
    std::string offset;  // expression text (may be empty)
    uint8_t base;
};

// ------------------------------------------------------------ text helpers

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}
bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$';
}
bool is_identifier(const std::string& s) {
    if (s.empty() || !is_ident_start(s[0])) return false;
    for (char c : s)
        if (!is_ident_char(c)) return false;
    return true;
}

// Removes ';', '#' and '//' comments, ignoring them inside string/char literals.
std::string strip_comment(const std::string& line) {
    bool in_str = false, in_chr = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_str || in_chr) {
            if (c == '\\') { ++i; continue; }
            if (in_str && c == '"') in_str = false;
            else if (in_chr && c == '\'') in_chr = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '\'') in_chr = true;
        else if (c == ';' || c == '#') return line.substr(0, i);
        else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return line.substr(0, i);
    }
    return line;
}

uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// ------------------------------------------------------------ the assembler

class Assembler {
public:
    Assembler(std::string name, const std::string& source) : name_(std::move(name)) {
        std::istringstream in(source);
        std::string line;
        while (std::getline(in, line)) lines_.push_back(line);
        // Predefined constants: system call numbers and the memory map.
        for (uint32_t i = 0; i < SYS_COUNT; ++i) consts_[std::string("SYS_") + syscall_name(i)] = i;
        consts_["TEXT_BASE"] = TEXT_BASE;
        consts_["DATA_BASE"] = DATA_BASE;
        consts_["HEAP_BASE"] = HEAP_BASE;
        consts_["STACK_BASE"] = STACK_BASE;
        consts_["STACK_TOP"] = STACK_TOP;
        consts_["SEGMENT_SIZE"] = SEGMENT_SIZE;
    }

    Program run() {
        pass1();
        pass2();
        Program p;
        p.text = std::move(text_);
        p.data = std::move(data_);
        p.symbols = labels_;
        p.line_map = std::move(line_map_);
        p.source_lines = lines_;
        p.source_name = name_;
        p.entry = resolve_entry();
        return p;
    }

private:
    [[noreturn]] void error(int line, const std::string& msg) const { throw AssemblyError(name_, line, msg); }

    // ---------------------------------------------------- operand parsing

    std::vector<std::string> split_operands(const std::string& s, int line) const {
        std::vector<std::string> out;
        if (trim(s).empty()) return out;
        std::string cur;
        int depth = 0;
        bool in_str = false, in_chr = false;
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (in_str || in_chr) {
                cur += c;
                if (c == '\\' && i + 1 < s.size()) { cur += s[++i]; continue; }
                if (in_str && c == '"') in_str = false;
                else if (in_chr && c == '\'') in_chr = false;
                continue;
            }
            if (c == '"') in_str = true;
            else if (c == '\'') in_chr = true;
            else if (c == '(') ++depth;
            else if (c == ')') --depth;
            else if (c == ',' && depth == 0) {
                out.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (in_str) error(line, "unterminated string literal");
        if (in_chr) error(line, "unterminated character literal");
        out.push_back(trim(cur));
        for (const auto& o : out)
            if (o.empty()) error(line, "empty operand");
        return out;
    }

    // Decodes one (possibly escaped) character starting at s[i]; advances i.
    char parse_escaped_char(const std::string& s, size_t& i, int line) const {
        char c = s[i++];
        if (c != '\\') return c;
        if (i >= s.size()) error(line, "bad escape sequence");
        c = s[i++];
        switch (c) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case '0': return '\0';
            case '\\': return '\\';
            case '"': return '"';
            case '\'': return '\'';
            case 'x': {
                int v = 0, digits = 0;
                while (i < s.size() && digits < 2 && std::isxdigit(static_cast<unsigned char>(s[i]))) {
                    v = v * 16 + std::stoi(std::string(1, s[i]), nullptr, 16);
                    ++i;
                    ++digits;
                }
                if (digits == 0) error(line, "\\x needs hex digits");
                return static_cast<char>(v);
            }
            default: error(line, std::string("unknown escape sequence '\\") + c + "'");
        }
    }

    std::string parse_string(const std::string& arg, int line) const {
        if (arg.size() < 2 || arg.front() != '"' || arg.back() != '"')
            error(line, "expected a string literal in double quotes, got '" + arg + "'");
        std::string out;
        size_t i = 1;
        while (i < arg.size() - 1) out += parse_escaped_char(arg, i, line);
        return out;
    }

    // Expression grammar:  expr := [+|-] term { (+|-) term }
    //                      term := number | 'c' | symbol | '(' expr ')'
    struct ExprState {
        const std::string& s;
        size_t i;
        std::string unknown;  // first undefined symbol encountered
    };

    void skip_ws(ExprState& st) const {
        while (st.i < st.s.size() && std::isspace(static_cast<unsigned char>(st.s[st.i]))) ++st.i;
    }

    int64_t parse_term(ExprState& st, int line) const {
        skip_ws(st);
        if (st.i >= st.s.size()) error(line, "expected a value in expression '" + st.s + "'");
        const char c = st.s[st.i];
        if (c == '(') {
            ++st.i;
            const int64_t v = parse_expr(st, line);
            skip_ws(st);
            if (st.i >= st.s.size() || st.s[st.i] != ')') error(line, "missing ')' in '" + st.s + "'");
            ++st.i;
            return v;
        }
        if (c == '\'') {
            ++st.i;
            if (st.i >= st.s.size()) error(line, "bad character literal");
            const char ch = parse_escaped_char(st.s, st.i, line);
            if (st.i >= st.s.size() || st.s[st.i] != '\'') error(line, "unterminated character literal");
            ++st.i;
            return static_cast<unsigned char>(ch);
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const size_t start = st.i;
            while (st.i < st.s.size() && (std::isalnum(static_cast<unsigned char>(st.s[st.i])) || st.s[st.i] == '_'))
                ++st.i;
            return parse_number(st.s.substr(start, st.i - start), line);
        }
        if (is_ident_start(c)) {
            const size_t start = st.i;
            while (st.i < st.s.size() && is_ident_char(st.s[st.i])) ++st.i;
            const std::string name = st.s.substr(start, st.i - start);
            auto l = labels_.find(name);
            if (l != labels_.end()) return l->second;
            auto k = consts_.find(name);
            if (k != consts_.end()) return k->second;
            if (st.unknown.empty()) st.unknown = name;
            return 0;
        }
        error(line, std::string("unexpected character '") + c + "' in expression '" + st.s + "'");
    }

    int64_t parse_expr(ExprState& st, int line) const {
        int64_t value = 0;
        bool first = true;
        while (true) {
            skip_ws(st);
            char op = '+';
            if (st.i < st.s.size() && (st.s[st.i] == '+' || st.s[st.i] == '-')) {
                op = st.s[st.i++];
            } else if (!first) {
                break;
            }
            const int64_t t = parse_term(st, line);
            value = (op == '+') ? value + t : value - t;
            first = false;
            skip_ws(st);
            if (st.i >= st.s.size() || st.s[st.i] == ')') break;
            if (st.s[st.i] != '+' && st.s[st.i] != '-')
                error(line, "unexpected '" + st.s.substr(st.i) + "' in expression '" + st.s + "'");
        }
        return value;
    }

    int64_t parse_number(const std::string& tok, int line) const {
        std::string t;
        for (char c : tok)
            if (c != '_') t += c;
        int base = 10;
        size_t start = 0;
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { base = 16; start = 2; }
        else if (t.size() > 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B')) { base = 2; start = 2; }
        else if (t.size() > 2 && t[0] == '0' && (t[1] == 'o' || t[1] == 'O')) { base = 8; start = 2; }
        const std::string digits = t.substr(start);
        if (digits.empty()) error(line, "malformed number '" + tok + "'");
        for (char c : digits) {
            int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                    : std::isalpha(static_cast<unsigned char>(c)) ? std::tolower(c) - 'a' + 10 : 99;
            if (d >= base) error(line, "malformed number '" + tok + "'");
        }
        if (digits.size() > 20) error(line, "number '" + tok + "' is too large");
        unsigned long long v = std::stoull(digits, nullptr, base);
        if (v > 0xFFFFFFFFFFULL) error(line, "number '" + tok + "' is too large");
        return static_cast<int64_t>(v);
    }

    // Evaluates an expression. If it references an undefined symbol, returns
    // nullopt (when !required) or reports an error (when required).
    std::optional<int64_t> eval(const std::string& expr, int line, bool required) const {
        ExprState st{expr, 0, {}};
        const int64_t v = parse_expr(st, line);
        skip_ws(st);
        if (st.i != expr.size()) error(line, "unexpected '" + expr.substr(st.i) + "' in expression '" + expr + "'");
        if (!st.unknown.empty()) {
            if (!required) return std::nullopt;
            std::string hint;
            if (parse_register(st.unknown)) hint = " (registers are not allowed here)";
            error(line, "undefined symbol '" + st.unknown + "'" + hint);
        }
        return v;
    }

    int64_t value(const std::string& expr, int line) const { return *eval(expr, line, true); }

    // "imm(reg)" or "(reg)"  ->  {offset, base}; anything else -> nullopt.
    static std::optional<MemOperand> split_mem_operand(const std::string& s) {
        if (s.empty() || s.back() != ')') return std::nullopt;
        int depth = 0;
        size_t open = std::string::npos;
        for (size_t i = s.size(); i-- > 0;) {
            if (s[i] == ')') ++depth;
            else if (s[i] == '(' && --depth == 0) { open = i; break; }
        }
        if (open == std::string::npos) return std::nullopt;
        auto reg = parse_register(trim(s.substr(open + 1, s.size() - open - 2)));
        if (!reg) return std::nullopt;
        return MemOperand{trim(s.substr(0, open)), *reg};
    }

    // ---------------------------------------------------- symbols

    void define_label(const std::string& name, uint32_t addr, int line) {
        labels_[name] = addr;
        (void)line;
    }

    void check_new_symbol(const std::string& name, int line) const {
        if (!is_identifier(name)) error(line, "invalid symbol name '" + name + "'");
        if (parse_register(name)) error(line, "'" + name + "' is a register name and cannot be a symbol");
        if (labels_.count(name) || consts_.count(name) || pending_names_.count(name))
            error(line, "symbol '" + name + "' is already defined");
    }

    // ---------------------------------------------------- pass 1

    // Number of machine instructions a text statement expands to.
    int instruction_count(const Statement& st) const {
        const std::string& op = st.op;
        if (op == "LI") {
            if (st.args.size() == 2) {
                auto v = eval(st.args[1], st.line, false);
                if (v && *v >= S18_MIN && *v <= S18_MAX) return 1;  // ADDI rd, zero, imm
            }
            return 2;  // LUI + ORI
        }
        if (op == "LA") return 2;
        static const std::set<std::string> pseudo1 = {"MOV",  "NEG",  "INC", "DEC", "B",
                                                      "BEQZ", "BNEZ", "BGT", "BLE", "BGTU", "BLEU"};
        if (pseudo1.count(op)) return 1;
        auto opc = lookup_mnemonic(op);
        if (!opc) error(st.line, "unknown instruction '" + to_lower(op) + "'");
        const Syntax syn = op_info(*opc).syntax;
        if ((syn == Syntax::Load || syn == Syntax::Store) && st.args.size() == 2 &&
            !split_mem_operand(st.args[1]))
            return 3;  // LUI at / ORI at / access 0(at)
        return 1;
    }

    void pass1() {
        Section sec = Section::Text;
        uint32_t pc[2] = {TEXT_BASE, DATA_BASE};
        struct Pending { std::string name; Section sec; int line; };
        std::vector<Pending> pending;

        auto flush = [&](Section s, uint32_t addr) {
            std::vector<Pending> keep;
            for (auto& p : pending) {
                if (p.sec == s) {
                    pending_names_.erase(p.name);
                    define_label(p.name, addr, p.line);
                } else {
                    keep.push_back(p);
                }
            }
            pending.swap(keep);
        };
        auto check_fits = [&](Section s, int line) {
            const uint32_t base = s == Section::Text ? TEXT_BASE : DATA_BASE;
            if (pc[static_cast<int>(s)] - base > SEGMENT_SIZE)
                error(line, std::string(s == Section::Text ? ".text" : ".data") + " exceeds the 1 MiB segment");
        };

        for (size_t ln = 0; ln < lines_.size(); ++ln) {
            const int line = static_cast<int>(ln) + 1;
            std::string text = trim(strip_comment(lines_[ln]));

            // Labels: "name:" (several may precede one statement).
            while (!text.empty() && is_ident_start(text[0])) {
                size_t j = 0;
                while (j < text.size() && is_ident_char(text[j])) ++j;
                if (j >= text.size() || text[j] != ':') break;
                const std::string name = text.substr(0, j);
                check_new_symbol(name, line);
                pending.push_back({name, sec, line});
                pending_names_.insert(name);
                text = trim(text.substr(j + 1));
            }
            if (text.empty()) continue;

            const size_t ws = text.find_first_of(" \t");
            Statement st;
            st.line = line;
            st.section = sec;
            st.op = to_upper(text.substr(0, ws));
            st.args = split_operands(ws == std::string::npos ? "" : text.substr(ws), line);
            auto need = [&](size_t n) {
                if (st.args.size() != n)
                    error(line, strfmt("'%s' expects %zu operand(s), got %zu", to_lower(st.op).c_str(), n,
                                       st.args.size()));
            };

            if (st.op[0] == '.') {
                // ---------------- directives
                if (st.op == ".TEXT" || st.op == ".DATA") {
                    need(0);
                    flush(sec, pc[static_cast<int>(sec)]);
                    sec = st.op == ".TEXT" ? Section::Text : Section::Data;
                    continue;
                }
                if (st.op == ".GLOBL" || st.op == ".GLOBAL") continue;  // accepted for compatibility
                if (st.op == ".ENTRY") {
                    need(1);
                    entry_name_ = st.args[0];
                    entry_line_ = line;
                    continue;
                }
                if (st.op == ".EQU" || st.op == ".SET") {
                    need(2);
                    check_new_symbol(st.args[0], line);
                    consts_[st.args[0]] = value(st.args[1], line);
                    continue;
                }
                if (sec != Section::Data)
                    error(line, "data directive '" + to_lower(st.op) + "' must be placed in the .data section");

                uint32_t align = 1, size = 0;
                if (st.op == ".WORD") {
                    if (st.args.empty()) error(line, ".word needs at least one value");
                    align = 4;
                    size = 4 * static_cast<uint32_t>(st.args.size());
                } else if (st.op == ".BYTE") {
                    if (st.args.empty()) error(line, ".byte needs at least one value");
                    size = static_cast<uint32_t>(st.args.size());
                } else if (st.op == ".ASCII" || st.op == ".ASCIIZ") {
                    if (st.args.empty()) error(line, to_lower(st.op) + " needs a string");
                    for (const auto& a : st.args)
                        size += static_cast<uint32_t>(parse_string(a, line).size()) + (st.op == ".ASCIIZ" ? 1 : 0);
                } else if (st.op == ".SPACE" || st.op == ".ZERO") {
                    need(1);
                    const int64_t n = value(st.args[0], line);
                    if (n < 0 || n > SEGMENT_SIZE) error(line, ".space size out of range");
                    size = static_cast<uint32_t>(n);
                } else if (st.op == ".ALIGN") {
                    need(1);
                    const int64_t n = value(st.args[0], line);
                    if (n < 1 || n > 4096 || (n & (n - 1)) != 0)
                        error(line, ".align needs a power of two between 1 and 4096 (bytes)");
                    align = static_cast<uint32_t>(n);
                } else {
                    error(line, "unknown directive '" + to_lower(st.op) + "'");
                }
                uint32_t& p = pc[static_cast<int>(Section::Data)];
                p = align_up(p, align);
                flush(Section::Data, p);
                st.addr = p;
                st.size = size;
                p += size;
                check_fits(Section::Data, line);
                stmts_.push_back(st);
                continue;
            }

            // ---------------- instructions
            if (sec != Section::Text)
                error(line, "instruction '" + to_lower(st.op) + "' must be placed in the .text section");
            const int count = instruction_count(st);
            uint32_t& p = pc[static_cast<int>(Section::Text)];
            flush(Section::Text, p);
            st.addr = p;
            st.size = 4u * static_cast<uint32_t>(count);
            p += st.size;
            check_fits(Section::Text, line);
            stmts_.push_back(st);
        }
        flush(Section::Text, pc[0]);
        flush(Section::Data, pc[1]);
        text_end_ = pc[0];
        data_end_ = pc[1];
    }

    // ---------------------------------------------------- pass 2

    static Instruction mk(Opcode op, uint8_t rd = 0, uint8_t rs1 = 0, uint8_t rs2 = 0, int32_t imm = 0) {
        Instruction i;
        i.op = op;
        i.rd = rd;
        i.rs1 = rs1;
        i.rs2 = rs2;
        i.imm = imm;
        return i;
    }

    // Any 32-bit constant in two instructions: LUI rd, hi22 ; ORI rd, rd, lo10
    static std::vector<Instruction> load_const(uint8_t rd, uint32_t v) {
        return {mk(Opcode::LUI, rd, 0, 0, static_cast<int32_t>((v >> 10) & 0x3FFFFFu)),
                mk(Opcode::ORI, rd, rd, 0, static_cast<int32_t>(v & 0x3FFu))};
    }

    int32_t check_imm(int64_t v, ImmKind kind, const std::string& mnemonic, int line) const {
        switch (kind) {
            case ImmKind::Signed18:
                if (v < S18_MIN || v > S18_MAX)
                    error(line, strfmt("immediate %lld out of range for %s (-131072..131071); "
                                       "use LI into a register",
                                       static_cast<long long>(v), mnemonic.c_str()));
                break;
            case ImmKind::Unsigned18:
                if (v < 0 || v > U18_MAX)
                    error(line, strfmt("immediate %lld out of range for %s (0..262143; logical immediates "
                                       "are zero-extended)",
                                       static_cast<long long>(v), mnemonic.c_str()));
                break;
            case ImmKind::Shamt:
                if (v < 0 || v > 31) error(line, "shift amount must be 0..31");
                break;
            case ImmKind::Unsigned22:
                if (v < 0 || v > U22_MAX) error(line, "LUI immediate must be 0..4194303");
                break;
            case ImmKind::Addr26:
            case ImmKind::None: break;
        }
        return static_cast<int32_t>(v);
    }

    uint32_t code_target(const std::string& expr, int line) const {
        const int64_t t = value(expr, line);
        if (t < TEXT_BASE || t >= text_end_ || (t & 3) != 0)
            error(line, "jump/branch target '" + expr + "' is not an instruction address in .text");
        return static_cast<uint32_t>(t);
    }

    Instruction branch(Opcode op, uint8_t rs1, uint8_t rs2, const std::string& target, uint32_t pc,
                       int line) const {
        const int64_t t = code_target(target, line);
        const int64_t offset = (t - (static_cast<int64_t>(pc) + 4)) / 4;
        if (offset < S18_MIN || offset > S18_MAX) error(line, "branch target out of range; use JMP");
        return mk(op, 0, rs1, rs2, static_cast<int32_t>(offset));
    }

    std::vector<Instruction> mem_access(Opcode op, bool store, uint8_t reg, const std::string& operand,
                                        int line) const {
        if (auto m = split_mem_operand(operand)) {
            const int64_t off = m->offset.empty() ? 0 : value(m->offset, line);
            check_imm(off, ImmKind::Signed18, "memory offset", line);
            return {store ? mk(op, 0, m->base, reg, static_cast<int32_t>(off))
                          : mk(op, reg, m->base, 0, static_cast<int32_t>(off))};
        }
        // Absolute/symbolic address: materialise it in `at` first.
        if (store && reg == REG_AT)
            error(line, "cannot store 'at' to a symbolic address (the expansion uses at); use imm(reg)");
        const int64_t addr = value(operand, line);
        if (addr < 0 || addr > WORD_MAX) error(line, "address out of range");
        auto out = load_const(REG_AT, static_cast<uint32_t>(addr));
        out.push_back(store ? mk(op, 0, REG_AT, reg, 0) : mk(op, reg, REG_AT, 0, 0));
        return out;
    }

    std::vector<Instruction> expand(const Statement& st) const {
        const auto& a = st.args;
        const std::string& op = st.op;
        const int line = st.line;
        auto need = [&](size_t n) {
            if (a.size() != n)
                error(line, strfmt("'%s' expects %zu operand(s), got %zu", to_lower(op).c_str(), n, a.size()));
        };
        auto R = [&](size_t i) -> uint8_t {
            auto r = parse_register(a[i]);
            if (!r) error(line, "expected a register, got '" + a[i] + "'");
            return *r;
        };

        // ---------------- pseudo-instructions
        if (op == "LI" || op == "LA") {
            need(2);
            const uint8_t rd = R(0);
            const int64_t v = value(a[1], line);
            if (v < WORD_MIN || v > WORD_MAX) error(line, "constant does not fit in 32 bits");
            if (st.size == 4) return {mk(Opcode::ADDI, rd, REG_ZERO, 0, static_cast<int32_t>(v))};
            return load_const(rd, static_cast<uint32_t>(v));
        }
        if (op == "MOV") { need(2); return {mk(Opcode::ADD, R(0), R(1), REG_ZERO)}; }
        if (op == "NEG") { need(2); return {mk(Opcode::SUB, R(0), REG_ZERO, R(1))}; }
        if (op == "INC") { need(1); return {mk(Opcode::ADDI, R(0), R(0), 0, 1)}; }
        if (op == "DEC") { need(1); return {mk(Opcode::ADDI, R(0), R(0), 0, -1)}; }
        if (op == "B") { need(1); return {mk(Opcode::JMP, 0, 0, 0, static_cast<int32_t>(code_target(a[0], line)))}; }
        if (op == "BEQZ") { need(2); return {branch(Opcode::BEQ, R(0), REG_ZERO, a[1], st.addr, line)}; }
        if (op == "BNEZ") { need(2); return {branch(Opcode::BNE, R(0), REG_ZERO, a[1], st.addr, line)}; }
        if (op == "BGT") { need(3); return {branch(Opcode::BLT, R(1), R(0), a[2], st.addr, line)}; }
        if (op == "BLE") { need(3); return {branch(Opcode::BGE, R(1), R(0), a[2], st.addr, line)}; }
        if (op == "BGTU") { need(3); return {branch(Opcode::BLTU, R(1), R(0), a[2], st.addr, line)}; }
        if (op == "BLEU") { need(3); return {branch(Opcode::BGEU, R(1), R(0), a[2], st.addr, line)}; }

        // ---------------- real instructions
        const Opcode opc = *lookup_mnemonic(op);
        const OpInfo& info = op_info(opc);
        const std::string mn = to_lower(op);
        switch (info.syntax) {
            case Syntax::None: need(0); return {mk(opc)};
            case Syntax::RRR: need(3); return {mk(opc, R(0), R(1), R(2))};
            case Syntax::RR: need(2); return {mk(opc, R(0), R(1))};
            case Syntax::RRI:
                need(3);
                return {mk(opc, R(0), R(1), 0, check_imm(value(a[2], line), info.imm, mn, line))};
            case Syntax::RI:
                need(2);
                return {mk(opc, R(0), 0, 0, check_imm(value(a[1], line), info.imm, mn, line))};
            case Syntax::Load: need(2); return mem_access(opc, false, R(0), a[1], line);
            case Syntax::Store: need(2); return mem_access(opc, true, R(0), a[1], line);
            case Syntax::Branch: need(3); return {branch(opc, R(0), R(1), a[2], st.addr, line)};
            case Syntax::Jump: need(1); return {mk(opc, 0, 0, 0, static_cast<int32_t>(code_target(a[0], line)))};
            case Syntax::RegS: need(1); return {mk(opc, 0, R(0))};
            case Syntax::RegD: need(1); return {mk(opc, R(0))};
            case Syntax::Imm: {
                need(1);
                const int32_t v = check_imm(value(a[0], line), info.imm, mn, line);
                if (opc == Opcode::ENTER && v % 4 != 0)
                    error(line, "ENTER frame size must be a multiple of 4 to keep sp word-aligned");
                return {mk(opc, 0, 0, 0, v)};
            }
        }
        error(line, "internal error: unhandled syntax");
    }

    static void put32(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 24));
    }

    void emit_data(const Statement& st) {
        data_.resize(st.addr - DATA_BASE, 0);  // alignment padding
        const int line = st.line;
        if (st.op == ".WORD") {
            for (const auto& arg : st.args) {
                const int64_t v = value(arg, line);
                if (v < WORD_MIN || v > WORD_MAX) error(line, ".word value does not fit in 32 bits");
                put32(data_, static_cast<uint32_t>(v));
            }
        } else if (st.op == ".BYTE") {
            for (const auto& arg : st.args) {
                const int64_t v = value(arg, line);
                if (v < -128 || v > 255) error(line, ".byte value must be -128..255");
                data_.push_back(static_cast<uint8_t>(v));
            }
        } else if (st.op == ".ASCII" || st.op == ".ASCIIZ") {
            for (const auto& arg : st.args) {
                for (char c : parse_string(arg, line)) data_.push_back(static_cast<uint8_t>(c));
                if (st.op == ".ASCIIZ") data_.push_back(0);
            }
        } else if (st.op == ".SPACE" || st.op == ".ZERO") {
            data_.resize(data_.size() + st.size, 0);
        }
        // .ALIGN emits nothing; its padding was applied through the address.
    }

    void pass2() {
        for (const auto& st : stmts_) {
            if (st.section == Section::Data) {
                emit_data(st);
                continue;
            }
            const auto instrs = expand(st);
            if (instrs.size() * 4 != st.size) error(st.line, "internal error: instruction size changed between passes");
            uint32_t addr = st.addr;
            for (const auto& ins : instrs) {
                put32(text_, encode(ins));
                line_map_[addr] = st.line;
                addr += 4;
            }
        }
        data_.resize(data_end_ - DATA_BASE, 0);  // trailing .align / labels
    }

    uint32_t resolve_entry() const {
        if (!entry_name_.empty()) {
            auto it = labels_.find(entry_name_);
            if (it == labels_.end()) error(entry_line_, "entry label '" + entry_name_ + "' is not defined");
            if (it->second < TEXT_BASE || it->second >= TEXT_BASE + SEGMENT_SIZE)
                error(entry_line_, "entry label must be in .text");
            return it->second;
        }
        for (const char* name : {"main", "_start"}) {
            auto it = labels_.find(name);
            if (it != labels_.end() && it->second >= TEXT_BASE && it->second < text_end_) return it->second;
        }
        return TEXT_BASE;
    }

    std::string name_;
    std::vector<std::string> lines_;
    std::vector<Statement> stmts_;
    std::map<std::string, uint32_t> labels_;
    std::map<std::string, int64_t> consts_;
    std::set<std::string> pending_names_;
    std::string entry_name_;
    int entry_line_ = 0;
    uint32_t text_end_ = TEXT_BASE;
    uint32_t data_end_ = DATA_BASE;
    std::vector<uint8_t> text_;
    std::vector<uint8_t> data_;
    std::map<uint32_t, int> line_map_;
};

}  // namespace

Program assemble(const std::string& source, const std::string& name) {
    return Assembler(name, source).run();
}

Program assemble_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return assemble(ss.str(), path);
}

}  // namespace c32
