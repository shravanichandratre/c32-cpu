// memory.cpp - segmented memory with permission, alignment and heap checks.
#include "c32/memory.hpp"

#include <algorithm>
#include <stdexcept>

#include "c32/util.hpp"

namespace c32 {

Memory::Memory() {
    auto init = [](Segment& s, const char* name, uint32_t base, bool r, bool w, bool x) {
        s.name = name;
        s.base = base;
        s.size = SEGMENT_SIZE;
        s.readable = r;
        s.writable = w;
        s.executable = x;
        s.bytes.assign(SEGMENT_SIZE, 0);
    };
    init(segs_[0], "text", TEXT_BASE, true, false, true);
    init(segs_[1], "data", DATA_BASE, true, true, false);
    init(segs_[2], "heap", HEAP_BASE, true, true, false);
    init(segs_[3], "stack", STACK_BASE, true, true, false);
}

void Memory::reset() {
    for (auto& s : segs_) {
        std::fill(s.bytes.begin(), s.bytes.end(), 0);
        s.reads = s.writes = s.fetches = 0;
    }
    brk_ = HEAP_BASE;
    text_size_ = data_size_ = 0;
}

Segment* Memory::find_segment_mut(uint32_t addr) {
    for (auto& s : segs_)
        if (s.contains(addr)) return &s;
    return nullptr;
}

const Segment* Memory::find_segment(uint32_t addr) const {
    for (const auto& s : segs_)
        if (s.contains(addr)) return &s;
    return nullptr;
}

Segment& Memory::check(uint32_t addr, AccessSize size, AccessKind kind) {
    const uint32_t n = static_cast<uint32_t>(size);
    const char* what = kind == AccessKind::Fetch ? "instruction fetch"
                       : kind == AccessKind::Write ? (n == 4 ? "word write" : "byte write")
                                                   : (n == 4 ? "word read" : "byte read");

    if (n > 1 && (addr % n) != 0) {
        throw CpuFault(FaultType::MisalignedAccess, addr,
                       strfmt("misaligned %s at 0x%08X (words must be 4-byte aligned)", what, addr));
    }
    Segment* seg = find_segment_mut(addr);
    if (seg == nullptr) {
        throw CpuFault(FaultType::SegmentationFault, addr,
                       strfmt("%s at unmapped address 0x%08X", what, addr));
    }
    // Segments are 4-byte multiples and the access is aligned, so the whole
    // access lies inside this one segment.
    if (kind == AccessKind::Fetch && !seg->executable) {
        throw CpuFault(FaultType::ExecuteViolation, addr,
                       strfmt("instruction fetch from non-executable %s segment at 0x%08X",
                              seg->name, addr));
    }
    if (kind == AccessKind::Write && !seg->writable) {
        throw CpuFault(FaultType::WriteViolation, addr,
                       strfmt("%s to read-only %s segment at 0x%08X", what, seg->name, addr));
    }
    if (kind == AccessKind::Read && !seg->readable) {
        throw CpuFault(FaultType::SegmentationFault, addr,
                       strfmt("%s from unreadable %s segment at 0x%08X", what, seg->name, addr));
    }
    if (seg == &segs_[static_cast<size_t>(SegmentId::Heap)] && addr + n > brk_) {
        throw CpuFault(FaultType::HeapViolation, addr,
                       strfmt("%s at 0x%08X is beyond the program break 0x%08X "
                              "(allocate heap memory with SYS_SBRK first)",
                              what, addr, brk_));
    }
    return *seg;
}

uint8_t Memory::read8(uint32_t addr) {
    Segment& s = check(addr, AccessSize::Byte, AccessKind::Read);
    ++s.reads;
    return s.bytes[addr - s.base];
}

uint32_t Memory::read32(uint32_t addr) {
    Segment& s = check(addr, AccessSize::Word, AccessKind::Read);
    ++s.reads;
    const uint8_t* p = &s.bytes[addr - s.base];
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void Memory::write8(uint32_t addr, uint8_t value) {
    Segment& s = check(addr, AccessSize::Byte, AccessKind::Write);
    ++s.writes;
    s.bytes[addr - s.base] = value;
}

void Memory::write32(uint32_t addr, uint32_t value) {
    Segment& s = check(addr, AccessSize::Word, AccessKind::Write);
    ++s.writes;
    uint8_t* p = &s.bytes[addr - s.base];
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t Memory::fetch32(uint32_t addr) {
    Segment& s = check(addr, AccessSize::Word, AccessKind::Fetch);
    ++s.fetches;
    const uint8_t* p = &s.bytes[addr - s.base];
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void Memory::load(SegmentId id, const std::vector<uint8_t>& image) {
    Segment& s = segs_[static_cast<size_t>(id)];
    if (image.size() > s.size) {
        throw std::runtime_error(strfmt("image of %zu bytes does not fit in the %s segment (%u bytes)",
                                        image.size(), s.name, s.size));
    }
    std::copy(image.begin(), image.end(), s.bytes.begin());
    if (id == SegmentId::Text) text_size_ = static_cast<uint32_t>(image.size());
    if (id == SegmentId::Data) data_size_ = static_cast<uint32_t>(image.size());
}

bool Memory::peek8(uint32_t addr, uint8_t& out) const {
    const Segment* s = find_segment(addr);
    if (s == nullptr) return false;
    out = s->bytes[addr - s->base];
    return true;
}

bool Memory::peek32(uint32_t addr, uint32_t& out) const {
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t b = 0;
        if (!peek8(addr + i, b)) return false;
        v |= static_cast<uint32_t>(b) << (8 * i);
    }
    out = v;
    return true;
}

std::optional<uint32_t> Memory::sbrk(int32_t increment) {
    const int64_t old_brk = brk_;
    const int64_t new_brk = old_brk + increment;
    if (new_brk < HEAP_BASE || new_brk > static_cast<int64_t>(HEAP_BASE) + SEGMENT_SIZE) {
        return std::nullopt;
    }
    Segment& heap = segs_[static_cast<size_t>(SegmentId::Heap)];
    if (new_brk < old_brk) {  // released memory is zeroed so it never leaks back
        std::fill(heap.bytes.begin() + (new_brk - HEAP_BASE), heap.bytes.begin() + (old_brk - HEAP_BASE), 0);
    }
    brk_ = static_cast<uint32_t>(new_brk);
    return static_cast<uint32_t>(old_brk);
}

}  // namespace c32
