// memory.hpp - segmented, permission-checked memory for the C32 CPU.
//
// Memory is byte-addressable and little-endian. Accesses are either a byte
// (8 bits) or a word (32 bits). Word accesses must be 4-byte aligned. Every
// access is checked against the segment table: it must fall inside a mapped
// segment and respect that segment's read/write/execute permissions. Heap
// accesses must additionally lie below the current program break (sbrk).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "c32/fault.hpp"
#include "c32/layout.hpp"

namespace c32 {

enum class SegmentId : uint8_t { Text = 0, Data = 1, Heap = 2, Stack = 3 };
constexpr size_t kNumSegments = 4;

enum class AccessSize : uint8_t { Byte = 1, Word = 4 };
enum class AccessKind : uint8_t { Read, Write, Fetch };

struct Segment {
    const char* name = "";
    uint32_t base = 0;
    uint32_t size = 0;
    bool readable = false;
    bool writable = false;
    bool executable = false;
    std::vector<uint8_t> bytes;
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t fetches = 0;

    uint32_t end() const { return base + size; }
    bool contains(uint32_t addr) const { return addr >= base && addr - base < size; }
};

class Memory {
public:
    Memory();

    // Zeroes all segments, clears statistics and resets the program break.
    void reset();

    // Checked accesses used by the CPU. Throw CpuFault on violation.
    uint8_t read8(uint32_t addr);
    uint32_t read32(uint32_t addr);
    void write8(uint32_t addr, uint8_t value);
    void write32(uint32_t addr, uint32_t value);
    uint32_t fetch32(uint32_t addr);  // instruction fetch: needs execute permission

    // Loader access: copies an image into a segment, bypassing permissions.
    void load(SegmentId id, const std::vector<uint8_t>& image);

    // Debugger access: no permission checks or statistics. False if unmapped.
    bool peek8(uint32_t addr, uint8_t& out) const;
    bool peek32(uint32_t addr, uint32_t& out) const;

    // Heap management (program break).
    uint32_t brk() const { return brk_; }
    std::optional<uint32_t> sbrk(int32_t increment);  // returns the old break

    const Segment& segment(SegmentId id) const { return segs_[static_cast<size_t>(id)]; }
    const Segment* find_segment(uint32_t addr) const;
    uint32_t text_size() const { return text_size_; }
    uint32_t data_size() const { return data_size_; }

private:
    Segment& check(uint32_t addr, AccessSize size, AccessKind kind);
    Segment* find_segment_mut(uint32_t addr);

    std::array<Segment, kNumSegments> segs_;
    uint32_t brk_ = HEAP_BASE;
    uint32_t text_size_ = 0;
    uint32_t data_size_ = 0;
};

}  // namespace c32
