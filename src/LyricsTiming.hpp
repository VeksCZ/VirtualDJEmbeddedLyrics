#pragma once

#include <algorithm>
#include <cstdint>
#include <climits>
#include <cwctype>
#include <string>

inline std::int64_t EstimateLyricHighlightMs(const std::wstring& text) noexcept {
    const auto characters = std::count_if(text.begin(), text.end(),
        [](wchar_t value) { return !std::iswspace(value); });
    return std::clamp<std::int64_t>(700 + static_cast<std::int64_t>(characters) * 65,
                                    1200, 4500);
}

inline float UnitProgress(std::int64_t now, std::int64_t start, std::int64_t duration) noexcept {
    if (duration <= 0) return 1.0f;
    return std::clamp(static_cast<float>(now - start) / static_cast<float>(duration), 0.0f, 1.0f);
}

inline int LyricCountdown(std::int64_t remainingMs) noexcept {
    if (remainingMs <= 0) return -1;
    const auto seconds = (remainingMs + 999) / 1000;
    return static_cast<int>(std::min<std::int64_t>(seconds, INT_MAX));
}
