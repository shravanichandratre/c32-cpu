// layout.hpp - C32 physical memory map.
//
// The address space is 32 bits wide, but only four 1 MiB segments are
// mapped. Everything else (including address 0) is unmapped, so a stray or
// NULL pointer traps instead of silently corrupting memory.
//
//   0x0000_0000 - 0x000F_FFFF   unmapped (NULL guard)
//   0x0010_0000 - 0x001F_FFFF   TEXT   (read + execute)
//   0x0020_0000 - 0x002F_FFFF   DATA   (read + write)
//   0x0030_0000 - 0x003F_FFFF   HEAP   (read + write, below program break)
//   0x0040_0000 - 0x004F_FFFF   STACK  (read + write, grows downward)
//   0x0050_0000 - 0xFFFF_FFFF   unmapped
#pragma once

#include <cstdint>

namespace c32 {

constexpr uint32_t SEGMENT_SIZE = 1u << 20;  // 1 MiB per segment

constexpr uint32_t TEXT_BASE = 0x00100000;
constexpr uint32_t DATA_BASE = 0x00200000;
constexpr uint32_t HEAP_BASE = 0x00300000;
constexpr uint32_t STACK_BASE = 0x00400000;
constexpr uint32_t STACK_TOP = STACK_BASE + SEGMENT_SIZE;  // 0x00500000 (exclusive)

// On reset this value is pushed as main's return address. When the CPU
// fetches from it (i.e. main executed RET), the machine halts cleanly with
// exit code a0. It lies in unmapped space so it can never be real code.
constexpr uint32_t EXIT_TRAP = 0xFFFFFFF0;

}  // namespace c32
