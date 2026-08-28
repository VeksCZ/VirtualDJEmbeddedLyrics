#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct TimedToken {
    std::int64_t timeMs{};
    std::wstring text;
};

struct LyricLine {
    std::int64_t timeMs{};
    std::wstring text;
    std::vector<TimedToken> tokens;
};

struct LyricsDocument {
    std::vector<LyricLine> lines;
    std::wstring source;
    bool synchronized{true};

    [[nodiscard]] bool empty() const noexcept { return lines.empty(); }
};

struct LyricsLoadResult {
    LyricsDocument document;
    std::wstring error;
};

LyricsLoadResult LoadEmbeddedTimedLyrics(const std::filesystem::path& audioPath);
LyricsLoadResult LoadEmbeddedUntimedLyrics(const std::filesystem::path& audioPath);
LyricsLoadResult LoadLrc(const std::filesystem::path& lrcPath);
LyricsLoadResult LoadPlainTextLyrics(const std::filesystem::path& textPath);
LyricsLoadResult LoadLyricsForTrack(const std::filesystem::path& audioPath);
