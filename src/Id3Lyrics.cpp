#include "Lyrics.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <regex>
#include <span>

namespace {

std::uint32_t ReadSynchsafe(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0] & 0x7f) << 21) |
           (static_cast<std::uint32_t>(p[1] & 0x7f) << 14) |
           (static_cast<std::uint32_t>(p[2] & 0x7f) << 7) |
           static_cast<std::uint32_t>(p[3] & 0x7f);
}

std::uint32_t ReadBigEndian(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::wstring DecodeLatin1(std::span<const unsigned char> bytes) {
    std::wstring result;
    result.reserve(bytes.size());
    for (const auto ch : bytes) result.push_back(static_cast<wchar_t>(ch));
    return result;
}

std::wstring DecodeUtf8(std::span<const unsigned char> bytes) {
    std::wstring result;
    for (std::size_t i = 0; i < bytes.size();) {
        std::uint32_t cp = 0;
        std::size_t count = 0;
        const auto c = bytes[i];
        if (c < 0x80) { cp = c; count = 1; }
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; count = 2; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; count = 3; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; count = 4; }
        else { ++i; continue; }
        if (i + count > bytes.size()) break;
        bool valid = true;
        for (std::size_t j = 1; j < count; ++j) {
            if ((bytes[i + j] & 0xc0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (bytes[i + j] & 0x3f);
        }
        if (!valid) { ++i; continue; }
#if WCHAR_MAX <= 0xffff
        if (cp > 0xffff) {
            cp -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xd800 + (cp >> 10)));
            result.push_back(static_cast<wchar_t>(0xdc00 + (cp & 0x3ff)));
        } else
#endif
        result.push_back(static_cast<wchar_t>(cp));
        i += count;
    }
    return result;
}

std::wstring DecodeUtf16(std::span<const unsigned char> bytes, bool bigEndian) {
    std::wstring result;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const auto value = bigEndian
            ? static_cast<std::uint16_t>((bytes[i] << 8) | bytes[i + 1])
            : static_cast<std::uint16_t>(bytes[i] | (bytes[i + 1] << 8));
        if (value == 0xfeff || value == 0xfffe) continue;
        result.push_back(static_cast<wchar_t>(value));
    }
    return result;
}

std::size_t FindTerminator(std::span<const unsigned char> data, std::size_t offset, unsigned char encoding) {
    if (encoding == 1 || encoding == 2) {
        for (std::size_t i = offset; i + 1 < data.size(); i += 2)
            if (data[i] == 0 && data[i + 1] == 0) return i;
        return data.size();
    }
    for (std::size_t i = offset; i < data.size(); ++i)
        if (data[i] == 0) return i;
    return data.size();
}

std::wstring DecodeText(std::span<const unsigned char> bytes, unsigned char encoding) {
    if (encoding == 0) return DecodeLatin1(bytes);
    if (encoding == 3) return DecodeUtf8(bytes);
    bool bigEndian = encoding == 2;
    if (encoding == 1 && bytes.size() >= 2) bigEndian = bytes[0] == 0xfe && bytes[1] == 0xff;
    return DecodeUtf16(bytes, bigEndian);
}

LyricsLoadResult ParseSylt(std::span<const unsigned char> frame) {
    LyricsLoadResult result;
    if (frame.size() < 7) { result.error = L"SYLT frame is truncated"; return result; }
    const auto encoding = frame[0];
    const auto timestampFormat = frame[4];
    const auto contentType = frame[5];
    if (timestampFormat != 2) { result.error = L"SYLT uses MPEG frames instead of milliseconds"; return result; }
    if (contentType != 1 && contentType != 0) { result.error = L"SYLT is not lyrics content"; return result; }

    std::size_t pos = 6;
    const auto descriptorEnd = FindTerminator(frame, pos, encoding);
    pos = std::min(frame.size(), descriptorEnd + ((encoding == 1 || encoding == 2) ? 2u : 1u));

    std::vector<TimedToken> entries;
    while (pos < frame.size()) {
        const auto textEnd = FindTerminator(frame, pos, encoding);
        if (textEnd >= frame.size()) break;
        auto text = DecodeText(frame.subspan(pos, textEnd - pos), encoding);
        pos = textEnd + ((encoding == 1 || encoding == 2) ? 2u : 1u);
        if (pos + 4 > frame.size()) break;
        const auto time = ReadBigEndian(frame.data() + pos);
        pos += 4;
        if (!text.empty()) entries.push_back({time, std::move(text)});
    }

    // SYLT producers commonly store either complete lines or word-level tokens.
    // Newline characters always create a new display line; otherwise each timed
    // entry remains independently highlightable and is grouped conservatively.
    for (const auto& entry : entries) {
        std::size_t start = 0;
        while (start <= entry.text.size()) {
            const auto end = entry.text.find_first_of(L"\r\n", start);
            const auto part = entry.text.substr(start, end == std::wstring::npos ? end : end - start);
            if (!part.empty()) result.document.lines.push_back({entry.timeMs, part, {{entry.timeMs, part}}});
            if (end == std::wstring::npos) break;
            start = entry.text.find_first_not_of(L"\r\n", end);
            if (start == std::wstring::npos) break;
        }
    }
    result.document.source = L"embedded ID3 SYLT";
    return result;
}

LyricsLoadResult ParseTimestampedText(const std::wstring& text, const std::wstring& source) {
    LyricsLoadResult result;
    const std::wregex timestamp(LR"(^\s*[\(\[]([0-9]{1,3}):([0-9]{2})(?:[\.:]([0-9]{1,3}))?[\)\]]\s?(.*)$)");
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const auto end = text.find_first_of(L"\r\n", offset);
        const auto rawLine = text.substr(offset, end == std::wstring::npos ? end : end - offset);
        std::wsmatch match;
        if (std::regex_match(rawLine, match, timestamp)) {
            auto fraction = match[3].str();
            if (fraction.empty()) fraction = L"000";
            else if (fraction.size() == 1) fraction += L"00";
            else if (fraction.size() == 2) fraction += L"0";
            const auto timeMs = (std::stoll(match[1].str()) * 60 + std::stoll(match[2].str())) * 1000 +
                                std::stoll(fraction.substr(0, 3));
            auto lineText = match[4].str();
            if (!lineText.empty()) result.document.lines.push_back({timeMs, std::move(lineText), {}});
        }
        if (end == std::wstring::npos) break;
        offset = text.find_first_not_of(L"\r\n", end);
        if (offset == std::wstring::npos) break;
    }
    std::ranges::sort(result.document.lines, {}, &LyricLine::timeMs);
    result.document.source = source;
    if (result.document.empty()) result.error = L"Embedded text contains no recognized timestamps";
    return result;
}

std::wstring SanitizeUntimedLine(std::wstring line) {
    static const std::wregex leadingTimestamps(
        LR"(^\s*(?:[\(\[]\d{1,3}:\d{2}(?:[\.:]\d{1,3})?[\)\]]\s*)+)");
    static const std::wregex wordTimestamps(LR"(<\d{1,3}:\d{2}[\.:]\d{1,3}>)");
    static const std::wregex metadata(LR"(^\s*\[[A-Za-z]{1,8}:.*\]\s*$)");
    if (std::regex_match(line, metadata)) return {};
    line = std::regex_replace(line, leadingTimestamps, L"");
    line = std::regex_replace(line, wordTimestamps, L"");
    return line;
}

LyricsLoadResult ParseTxxxUntimedLyrics(std::span<const unsigned char> frame) {
    LyricsLoadResult result;
    if (frame.size() < 4) return result;
    const auto encoding = frame[0];
    const auto descriptionEnd = FindTerminator(frame, 1, encoding);
    if (descriptionEnd >= frame.size()) return result;
    const auto description = DecodeText(frame.subspan(1, descriptionEnd - 1), encoding);
    if (description != L"UNSYNCEDLYRICS") return result;
    const auto valueStart = descriptionEnd + ((encoding == 1 || encoding == 2) ? 2u : 1u);
    if (valueStart >= frame.size()) return result;
    auto value = DecodeText(frame.subspan(valueStart), encoding);
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find_first_of(L"\r\n", start);
        auto line = SanitizeUntimedLine(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (!line.empty()) result.document.lines.push_back({0, std::move(line), {}});
        if (end == std::wstring::npos) break;
        start = value.find_first_not_of(L"\r\n", end);
        if (start == std::wstring::npos) break;
    }
    result.document.source = L"embedded TXXX:UNSYNCEDLYRICS";
    result.document.synchronized = false;
    return result;
}

std::wstring DecodeUsltValue(std::span<const unsigned char> frame) {
    if (frame.size() < 5) return {};
    const auto encoding = frame[0];
    const auto descriptorEnd = FindTerminator(frame, 4, encoding);
    const auto valueStart = descriptorEnd + ((encoding == 1 || encoding == 2) ? 2u : 1u);
    if (valueStart >= frame.size()) return {};
    auto value = DecodeText(frame.subspan(valueStart), encoding);
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

LyricsLoadResult ParseTxxxTimedLyrics(std::span<const unsigned char> frame) {
    LyricsLoadResult result;
    if (frame.size() < 4) { result.error = L"TXXX frame is truncated"; return result; }
    const auto encoding = frame[0];
    const auto descriptionEnd = FindTerminator(frame, 1, encoding);
    if (descriptionEnd >= frame.size()) { result.error = L"TXXX description is truncated"; return result; }
    const auto description = DecodeText(frame.subspan(1, descriptionEnd - 1), encoding);
    const auto valueStart = descriptionEnd + ((encoding == 1 || encoding == 2) ? 2u : 1u);
    if (valueStart >= frame.size()) { result.error = L"TXXX value is empty"; return result; }
    auto value = DecodeText(frame.subspan(valueStart), encoding);
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    if (description != L"USLT" && description != L"LYRICS" && description != L"SYNCEDLYRICS" &&
        description != L"UNSYNCEDLYRICS") {
        result.error = L"TXXX is not a timed lyrics field";
        return result;
    }
    return ParseTimestampedText(value, L"embedded TXXX:" + description);
}

} // namespace

LyricsLoadResult LoadEmbeddedTimedLyrics(const std::filesystem::path& audioPath) {
    LyricsLoadResult result;
    std::ifstream input(audioPath, std::ios::binary);
    if (!input) { result.error = L"Cannot open audio file"; return result; }

    unsigned char header[10]{};
    if (!input.read(reinterpret_cast<char*>(header), sizeof(header)) ||
        header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        result.error = L"No ID3v2 tag";
        return result;
    }
    const auto version = header[3];
    if (version < 3 || version > 4) { result.error = L"Unsupported ID3v2 version"; return result; }
    const auto tagSize = ReadSynchsafe(header + 6);
    std::vector<unsigned char> tag(tagSize);
    if (!input.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size()))) {
        result.error = L"ID3v2 tag is truncated";
        return result;
    }

    LyricsLoadResult textLyrics;
    LyricsLoadResult syltFallback;
    LyricsLoadResult timestampedUntimedFallback;
    std::size_t pos = 0;
    while (pos + 10 <= tag.size()) {
        const auto* h = tag.data() + pos;
        if (h[0] == 0) break;
        const std::string id(reinterpret_cast<const char*>(h), 4);
        const auto size = version == 4 ? ReadSynchsafe(h + 4) : ReadBigEndian(h + 4);
        pos += 10;
        if (size > tag.size() - pos) break;
        if (id == "SYLT") {
            auto sylt = ParseSylt(std::span(tag).subspan(pos, size));
            if (!sylt.document.empty()) syltFallback = std::move(sylt);
        } else if (id == "TXXX") {
            auto candidate = ParseTxxxTimedLyrics(std::span(tag).subspan(pos, size));
            if (!candidate.document.empty()) {
                if (candidate.document.source == L"embedded TXXX:UNSYNCEDLYRICS")
                    timestampedUntimedFallback = std::move(candidate);
                else
                    textLyrics = std::move(candidate);
            }
        } else if (id == "USLT") {
            auto candidate = ParseTimestampedText(
                DecodeUsltValue(std::span(tag).subspan(pos, size)), L"embedded ID3 USLT with timestamps");
            if (!candidate.document.empty()) timestampedUntimedFallback = std::move(candidate);
        }
        pos += size;
    }
    if (!textLyrics.document.empty()) return textLyrics;
    if (!syltFallback.document.empty()) return syltFallback;
    if (!timestampedUntimedFallback.document.empty()) return timestampedUntimedFallback;
    result.error = L"No synchronized embedded lyrics";
    return result;
}

LyricsLoadResult LoadEmbeddedUntimedLyrics(const std::filesystem::path& audioPath) {
    LyricsLoadResult result;
    std::ifstream input(audioPath, std::ios::binary);
    if (!input) { result.error = L"Cannot open audio file"; return result; }
    unsigned char header[10]{};
    if (!input.read(reinterpret_cast<char*>(header), sizeof(header)) ||
        header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        result.error = L"No ID3v2 tag"; return result;
    }
    const auto version = header[3];
    if (version < 3 || version > 4) { result.error = L"Unsupported ID3v2 version"; return result; }
    std::vector<unsigned char> tag(ReadSynchsafe(header + 6));
    if (!input.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size()))) {
        result.error = L"ID3v2 tag is truncated"; return result;
    }
    LyricsLoadResult txxxLyrics, usltFallback;
    std::size_t pos = 0;
    while (pos + 10 <= tag.size()) {
        const auto* h = tag.data() + pos;
        if (h[0] == 0) break;
        const std::string id(reinterpret_cast<const char*>(h), 4);
        const auto size = version == 4 ? ReadSynchsafe(h + 4) : ReadBigEndian(h + 4);
        pos += 10;
        if (size > tag.size() - pos) break;
        if (id == "TXXX") {
            auto candidate = ParseTxxxUntimedLyrics(std::span(tag).subspan(pos, size));
            if (!candidate.document.empty()) txxxLyrics = std::move(candidate);
        } else if (id == "USLT" && size >= 5) {
            auto value = DecodeUsltValue(std::span(tag).subspan(pos, size));
            if (!value.empty()) {
                LyricsLoadResult candidate;
                std::size_t start = 0;
                while (start <= value.size()) {
                    const auto end = value.find_first_of(L"\r\n", start);
                    auto line = SanitizeUntimedLine(value.substr(start, end == std::wstring::npos ? end : end - start));
                    if (!line.empty()) candidate.document.lines.push_back({0, std::move(line), {}});
                    if (end == std::wstring::npos) break;
                    start = value.find_first_not_of(L"\r\n", end);
                    if (start == std::wstring::npos) break;
                }
                candidate.document.source = L"embedded ID3 USLT";
                candidate.document.synchronized = false;
                if (!candidate.document.empty()) usltFallback = std::move(candidate);
            }
        }
        pos += size;
    }
    if (!txxxLyrics.document.empty()) return txxxLyrics;
    if (!usltFallback.document.empty()) return usltFallback;
    result.error = L"No unsynchronized embedded lyrics";
    return result;
}