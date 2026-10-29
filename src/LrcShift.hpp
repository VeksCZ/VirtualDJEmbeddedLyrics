#pragma once
// Moves every [mm:ss.xx] timestamp in LRC text by the same amount so that the first one lands
// on `firstMs` (e.g. where the vocals really start). Used by "Start here" in the plugin panel and
// by "Shift" in the lyrics window. Returns false (text unchanged) when there is no timestamp.
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <regex>
#include <string>

inline bool ShiftLrcTimestamps(std::wstring& text, std::int64_t firstMs, std::int64_t* deltaMs = nullptr) {
    static const std::wregex stamp(LR"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])");
    std::wsmatch first;
    if (!std::regex_search(text, first, stamp)) return false;
    const auto toMs = [](const std::wsmatch& m) {
        std::int64_t ms = std::stoll(m[1].str()) * 60000 + std::stoll(m[2].str()) * 1000;
        if (m[3].matched) {
            auto frac = m[3].str();
            frac.resize(3, L'0');                      // .5 -> 500, .45 -> 450, .456 -> 456
            ms += std::stoll(frac);
        }
        return ms;
    };
    const std::int64_t delta = std::max<std::int64_t>(0, firstMs) - toMs(first);
    if (deltaMs) *deltaMs = delta;
    std::wstring out;
    out.reserve(text.size());
    auto begin = text.cbegin();
    for (std::wsregex_iterator it(text.begin(), text.end(), stamp), end; it != end; ++it) {
        const auto& m = *it;
        out.append(begin, m[0].first);
        const auto ms = std::max<std::int64_t>(0, toMs(m) + delta);
        wchar_t buffer[32]{};
        std::swprintf(buffer, std::size(buffer), L"[%02lld:%02lld.%02lld]",
                      static_cast<long long>(ms / 60000), static_cast<long long>((ms / 1000) % 60),
                      static_cast<long long>((ms % 1000) / 10));
        out += buffer;
        begin = m[0].second;
    }
    out.append(begin, text.cend());
    text = std::move(out);
    return true;
}

// "1:23.45", "83.45", "01:23" -> milliseconds; -1 when it can't be read.
inline std::int64_t ParseLyricsTime(const std::wstring& value) {
    static const std::wregex form(LR"(\s*(?:(\d{1,3}):)?(\d{1,4})(?:[.,](\d{1,3}))?\s*)");
    std::wsmatch m;
    if (!std::regex_match(value, m, form)) return -1;
    std::int64_t ms = (m[1].matched ? std::stoll(m[1].str()) * 60000 : 0) + std::stoll(m[2].str()) * 1000;
    if (m[3].matched) {
        auto frac = m[3].str();
        frac.resize(3, L'0');
        ms += std::stoll(frac);
    }
    return ms;
}

inline std::wstring FormatLyricsTime(std::int64_t ms) {
    ms = std::max<std::int64_t>(0, ms);
    wchar_t buffer[32]{};
    std::swprintf(buffer, std::size(buffer), L"%lld:%02lld.%02lld", static_cast<long long>(ms / 60000),
                  static_cast<long long>((ms / 1000) % 60), static_cast<long long>((ms % 1000) / 10));
    return buffer;
}
