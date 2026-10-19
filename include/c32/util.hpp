// util.hpp - small formatting helpers shared by all modules.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace c32 {

inline std::string hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", v);
    return buf;
}

// printf-style formatting into a std::string. Always call with at least one
// argument after the format string.
template <typename... Args>
std::string strfmt(const char* fmt, Args... args) {
    int n = std::snprintf(nullptr, 0, fmt, args...);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    std::snprintf(&s[0], static_cast<size_t>(n) + 1, fmt, args...);
    return s;
}

}  // namespace c32
