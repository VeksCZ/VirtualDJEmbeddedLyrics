#pragma once
#include <string>

// Built-in LRCLIB (https://lrclib.net) client used when the LrcHelper is not installed.
// Mirrors LrclibClient in tools/lrc_tool.py: exact /api/get first, then /api/search
// ranked by title/artist similarity within the duration tolerance. Portable C++; the
// HTTPS request itself is per platform (LrclibHttpWin.cpp / LrclibHttpMac.mm).
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

// One HTTPS GET against https://lrclib.net; pathAndQuery is ASCII ("/api/get?...").
struct LrclibHttpResponse {
    long status{};
    std::string body;    // UTF-8
    std::wstring error;  // set when no HTTP answer arrived at all
};
LrclibHttpResponse LrclibHttpGet(const std::string& pathAndQuery);

// Exposed for tests.
std::wstring NormalizeLrclibTitle(std::wstring text);
double LrclibArtistSimilarity(const std::wstring& tagged, const std::wstring& candidate);
double LrclibSimilarity(const std::wstring& left, const std::wstring& right);
std::string LrclibUtf8(const std::wstring& value);
std::wstring LrclibWiden(const std::string& utf8);
