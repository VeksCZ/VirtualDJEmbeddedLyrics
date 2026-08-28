#include "Lyrics.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace {

std::wstring Utf8ToWide(const std::string& input) {
    std::wstring output;
    for (std::size_t i = 0; i < input.size();) {
        const auto c = static_cast<unsigned char>(input[i]);
        std::uint32_t cp = 0;
        std::size_t count = 1;
        if (c < 0x80) cp = c;
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; count = 2; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; count = 3; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; count = 4; }
        else { ++i; continue; }
        if (i + count > input.size()) break;
        for (std::size_t j = 1; j < count; ++j) cp = (cp << 6) | (static_cast<unsigned char>(input[i + j]) & 0x3f);
        output.push_back(static_cast<wchar_t>(cp));
        i += count;
    }
    return output;
}

std::int64_t TimestampMs(const std::wsmatch& match) {
    const auto minutes = std::stoll(match[1].str());
    const auto seconds = std::stoll(match[2].str());
    auto fraction = match[3].str();
    if (fraction.size() == 1) fraction += L"00";
    else if (fraction.size() == 2) fraction += L"0";
    return (minutes * 60 + seconds) * 1000 + std::stoll(fraction.substr(0, 3));
}

} // namespace

LyricsLoadResult LoadLrc(const std::filesystem::path& lrcPath) {
    LyricsLoadResult result;
    std::ifstream input(lrcPath, std::ios::binary);
    if (!input) { result.error = L"Matching LRC file was not found"; return result; }
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::wstringstream stream(Utf8ToWide(bytes.substr(bytes.starts_with("\xef\xbb\xbf") ? 3 : 0)));
    const std::wregex lineTime(LR"(\[(\d{1,3}):(\d{2})[\.:](\d{1,3})\])");
    const std::wregex wordTime(LR"(<(\d{1,3}):(\d{2})[\.:](\d{1,3})>)");
    std::wstring raw;
    while (std::getline(stream, raw)) {
        std::vector<std::int64_t> lineTimes;
        for (auto it = std::wsregex_iterator(raw.begin(), raw.end(), lineTime); it != std::wsregex_iterator(); ++it)
            lineTimes.push_back(TimestampMs(*it));
        if (lineTimes.empty()) continue;
        const auto textStart = raw.find_last_of(L']') + 1;
        auto markedText = raw.substr(textStart);
        std::vector<TimedToken> tokens;
        std::wstring cleanText;
        std::size_t cursor = 0;
        for (auto it = std::wsregex_iterator(markedText.begin(), markedText.end(), wordTime); it != std::wsregex_iterator(); ++it) {
            const auto matchPos = static_cast<std::size_t>(it->position());
            cleanText += markedText.substr(cursor, matchPos - cursor);
            cursor = matchPos + static_cast<std::size_t>(it->length());
            const auto next = std::next(it);
            const auto tokenEnd = next == std::wsregex_iterator() ? markedText.size() : static_cast<std::size_t>(next->position());
            const auto tokenText = markedText.substr(cursor, tokenEnd - cursor);
            if (!tokenText.empty()) tokens.push_back({TimestampMs(*it), tokenText});
        }
        cleanText += markedText.substr(cursor);
        if (tokens.empty()) cleanText = markedText;
        for (const auto time : lineTimes) result.document.lines.push_back({time, cleanText, tokens});
    }
    std::ranges::sort(result.document.lines, {}, &LyricLine::timeMs);
    result.document.source = L"sidecar LRC";
    if (result.document.empty()) result.error = L"LRC contains no timed lines";
    return result;
}

LyricsLoadResult LoadPlainTextLyrics(const std::filesystem::path& textPath) {
    LyricsLoadResult result;
    std::ifstream input(textPath, std::ios::binary);
    if (!input) { result.error = L"Matching TXT file was not found"; return result; }
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::wstringstream stream(Utf8ToWide(bytes.substr(bytes.starts_with("\xef\xbb\xbf") ? 3 : 0)));
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        result.document.lines.push_back({0, line, {}});
    }
    while (!result.document.lines.empty() && result.document.lines.back().text.empty())
        result.document.lines.pop_back();
    result.document.source = L"sidecar TXT";
    result.document.synchronized = false;
    if (result.document.empty()) result.error = L"TXT contains no lyrics";
    return result;
}
