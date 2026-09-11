#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

struct LyricsWindow { std::size_t first, end; };

// Count includes the active line and its surrounding logical lyric lines.
inline LyricsWindow VisibleLyricsWindow(std::size_t size, std::size_t active,
                                        std::size_t count) {
    if (!size) return {0, 0};
    count = std::clamp<std::size_t>(count, 1, size);
    active = std::min(active, size - 1);
    const auto previous = (count - 1) / 2;
    const auto first = std::min(active > previous ? active - previous : 0, size - count);
    return {first, first + count};
}

inline std::int64_t AdjustLyricsTime(std::int64_t elapsed, int delayMs) {
    return elapsed - std::clamp(delayMs, -2000, 2000);
}
