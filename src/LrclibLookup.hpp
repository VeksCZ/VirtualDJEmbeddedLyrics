#pragma once
#ifdef _WIN32
#include <string>

// Built-in LRCLIB (https://lrclib.net) client used when the LrcHelper is not installed.
// Mirrors LrclibClient in tools/lrc_tool.py: exact /api/get first, then /api/search
// ranked by title/artist similarity within the duration tolerance.
struct LrclibMatch {
    bool found{};
    bool synchronized{};
    std::wstring text;     // LRC with [mm:ss.xx] rows, or plain lyrics
    std::wstring source;   // "LRCLIB synced", "LRCLIB plain search", ...
    std::wstring matched;  // "Artist - Title (m:ss)" of the chosen record
    std::wstring error;    // network/HTTP problem, empty on a clean miss
};

LrclibMatch LookupLrclib(const std::wstring& artist, const std::wstring& title,
                         double durationSeconds, double toleranceSeconds = 3.0);

// Exposed for tests.
std::wstring NormalizeLrclibTitle(std::wstring text);
double LrclibArtistSimilarity(const std::wstring& tagged, const std::wstring& candidate);
double LrclibSimilarity(const std::wstring& left, const std::wstring& right);
#endif
