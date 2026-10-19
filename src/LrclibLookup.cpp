#include "LrclibLookup.hpp"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

namespace {
// ---- Minimal JSON reader (objects, arrays, strings, numbers, literals) -------------
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type{Type::Null};
    bool boolean{};
    double number{};
    std::wstring string;
    std::vector<Json> items;           // array items or object values
    std::vector<std::wstring> keys;    // object keys, parallel to items

    const Json* Get(const wchar_t* key) const {
        if (type != Type::Object) return nullptr;
        for (std::size_t i = 0; i < keys.size(); ++i) if (keys[i] == key) return &items[i];
        return nullptr;
    }
    std::wstring Text(const wchar_t* key) const {
        const auto* v = Get(key);
        return v && v->type == Type::String ? v->string : std::wstring{};
    }
    double Number(const wchar_t* key) const {
        const auto* v = Get(key);
        return v && v->type == Type::Number ? v->number : 0.0;
    }
    bool Flag(const wchar_t* key) const {
        const auto* v = Get(key);
        return v && v->type == Type::Bool && v->boolean;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::wstring& text) : s_(text) {}
    bool Parse(Json& out) { return Value(out, 0) && (Skip(), p_ == s_.size()); }

private:
    void Skip() { while (p_ < s_.size() && std::iswspace(s_[p_])) ++p_; }
    bool Literal(const wchar_t* word) {
        const std::wstring w(word);
        if (s_.compare(p_, w.size(), w) != 0) return false;
        p_ += w.size();
        return true;
    }
    bool Value(Json& out, int depth) {
        if (depth > 32) return false;
        Skip();
        if (p_ >= s_.size()) return false;
        const wchar_t c = s_[p_];
        if (c == L'{') return Object(out, depth);
        if (c == L'[') return Array(out, depth);
        if (c == L'"') { out.type = Json::Type::String; return String(out.string); }
        if (Literal(L"true")) { out.type = Json::Type::Bool; out.boolean = true; return true; }
        if (Literal(L"false")) { out.type = Json::Type::Bool; return true; }
        if (Literal(L"null")) { out.type = Json::Type::Null; return true; }
        const std::size_t start = p_;
        while (p_ < s_.size() && (std::iswdigit(s_[p_]) || std::wcschr(L"+-.eE", s_[p_]))) ++p_;
        if (start == p_) return false;
        out.type = Json::Type::Number;
        out.number = std::wcstod(s_.substr(start, p_ - start).c_str(), nullptr);
        return true;
    }
    bool Object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++p_;
        Skip();
        if (p_ < s_.size() && s_[p_] == L'}') { ++p_; return true; }
        while (true) {
            Skip();
            std::wstring key;
            if (p_ >= s_.size() || s_[p_] != L'"' || !String(key)) return false;
            Skip();
            if (p_ >= s_.size() || s_[p_++] != L':') return false;
            Json value;
            if (!Value(value, depth + 1)) return false;
            out.keys.push_back(std::move(key));
            out.items.push_back(std::move(value));
            Skip();
            if (p_ < s_.size() && s_[p_] == L',') { ++p_; continue; }
            if (p_ < s_.size() && s_[p_] == L'}') { ++p_; return true; }
            return false;
        }
    }
    bool Array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++p_;
        Skip();
        if (p_ < s_.size() && s_[p_] == L']') { ++p_; return true; }
        while (true) {
            Json value;
            if (!Value(value, depth + 1)) return false;
            out.items.push_back(std::move(value));
            Skip();
            if (p_ < s_.size() && s_[p_] == L',') { ++p_; continue; }
            if (p_ < s_.size() && s_[p_] == L']') { ++p_; return true; }
            return false;
        }
    }
    bool Hex4(unsigned& value) {
        if (p_ + 4 > s_.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const wchar_t h = s_[p_++];
            value <<= 4;
            if (h >= L'0' && h <= L'9') value |= h - L'0';
            else if (h >= L'a' && h <= L'f') value |= h - L'a' + 10;
            else if (h >= L'A' && h <= L'F') value |= h - L'A' + 10;
            else return false;
        }
        return true;
    }
    bool String(std::wstring& out) {
        ++p_;  // opening quote
        while (p_ < s_.size()) {
            const wchar_t c = s_[p_++];
            if (c == L'"') return true;
            if (c != L'\\') { out += c; continue; }
            if (p_ >= s_.size()) return false;
            const wchar_t e = s_[p_++];
            switch (e) {
            case L'n': out += L'\n'; break;
            case L'r': out += L'\r'; break;
            case L't': out += L'\t'; break;
            case L'b': out += L'\b'; break;
            case L'f': out += L'\f'; break;
            case L'u': {
                unsigned code = 0;
                if (!Hex4(code)) return false;
                out += static_cast<wchar_t>(code);  // UTF-16 surrogates pass through as-is
                break;
            }
            default: out += e; break;  // \" \\ \/
            }
        }
        return false;
    }
    const std::wstring& s_;
    std::size_t p_{};
};

// ---- HTTP -------------------------------------------------------------------------
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(0, size)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::wstring Widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(0, size)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring UrlEncode(const std::wstring& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::wstring out;
    for (unsigned char c : Utf8(value)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<wchar_t>(c);
        } else {
            out += L'%';
            out += static_cast<wchar_t>(kHex[c >> 4]);
            out += static_cast<wchar_t>(kHex[c & 15]);
        }
    }
    return out;
}

struct HttpResult { DWORD status{}; std::wstring body; std::wstring error; };

HttpResult Get(const std::wstring& pathAndQuery) {
    HttpResult result;
    HINTERNET session = WinHttpOpen(
        L"VirtualDJEmbeddedLyrics (https://github.com/VeksCZ/VirtualDJEmbeddedLyrics)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { result.error = L"WinHTTP unavailable"; return result; }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);
    HINTERNET connection = WinHttpConnect(session, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"GET", pathAndQuery.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;
    std::string body;
    if (request &&
        WinHttpSendRequest(request, L"Accept: application/json\r\n", static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD size = sizeof(result.status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &size, WINHTTP_NO_HEADER_INDEX);
        for (DWORD available = 0; WinHttpQueryDataAvailable(request, &available) && available;) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || !read) break;
            body.append(chunk.data(), read);
            if (body.size() > 8 * 1024 * 1024) break;
        }
        result.body = Widen(body);
    } else {
        result.error = L"network error " + std::to_wstring(GetLastError());
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

// One retry on 5xx, like the Python client: LRCLIB returns 502/503 when briefly overloaded.
bool GetJson(const std::wstring& pathAndQuery, Json& out, std::wstring& error) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto response = Get(pathAndQuery);
        if (GetEnvironmentVariableW(L"LRCLIB_DEBUG", nullptr, 0)) {
            Json probe;
            const bool parsed = JsonParser(response.body).Parse(probe);
            fwprintf(stderr, L"[lrclib] %ls -> %lu, %zu chars, parsed=%d items=%zu %ls\n", pathAndQuery.c_str(),
                     response.status, response.body.size(), parsed, probe.items.size(), response.error.c_str());
        }
        if (!response.error.empty()) { error = response.error; return false; }
        if (response.status == 404) return false;
        if (response.status >= 500 && attempt == 0) { Sleep(1500); continue; }
        if (response.status != 200) { error = L"HTTP " + std::to_wstring(response.status); return false; }
        if (!JsonParser(response.body).Parse(out)) { error = L"unreadable LRCLIB answer"; return false; }
        return true;
    }
    return false;
}

// ---- Matching (same rules as tools/lrc_tool.py) ------------------------------------
std::wstring Lower(std::wstring text) {
    if (!text.empty())
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring Words(const std::wstring& text) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : text) {
        if (std::iswalnum(c) || c == L'_') {
            if (space && !out.empty()) out += L' ';
            out += c;
            space = false;
        } else {
            space = true;
        }
    }
    return out;
}

bool StartsWord(const std::wstring& text, std::size_t at, const wchar_t* word) {
    const std::wstring w(word);
    if (text.compare(at, w.size(), w) != 0) return false;
    const std::size_t end = at + w.size();
    return end >= text.size() || !std::iswalnum(text[end]);
}

std::wstring NormalizeArtist(std::wstring text) {
    text = Lower(std::move(text));
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((i == 0 || !std::iswalnum(text[i - 1])) &&
            (StartsWord(text, i, L"feat") || StartsWord(text, i, L"featuring") || StartsWord(text, i, L"ft"))) {
            text.resize(i);
            break;
        }
    }
    std::wstring out;
    for (wchar_t c : text) out += c == L'&' ? std::wstring(L" and ") : std::wstring(1, c);
    return Words(out);
}

std::vector<std::wstring> SplitArtists(const std::wstring& text) {
    // Cut at separators and joining words: , ; / & + x and with vs feat featuring ft
    const auto lower = Lower(text);
    std::vector<std::wstring> parts;
    std::wstring current;
    auto flush = [&] {
        auto normalized = NormalizeArtist(current);
        if (!normalized.empty()) parts.push_back(std::move(normalized));
        current.clear();
    };
    for (std::size_t i = 0; i < lower.size(); ++i) {
        const wchar_t c = lower[i];
        if (std::wcschr(L",;/&+", c)) { flush(); continue; }
        const bool boundary = i == 0 || !std::iswalnum(lower[i - 1]);
        bool joined = false;
        if (boundary) {
            for (const wchar_t* word : {L"x", L"and", L"with", L"vs", L"feat", L"featuring", L"ft"}) {
                if (StartsWord(lower, i, word)) {
                    flush();
                    i += std::wcslen(word) - 1;
                    joined = true;
                    break;
                }
            }
        }
        if (!joined) current += text[i];
    }
    flush();
    return parts;
}

std::wstring FormatDuration(double seconds) {
    if (seconds <= 0) return {};
    const int total = static_cast<int>(std::lround(seconds));
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%d:%02d", total / 60, total % 60);
    return buffer;
}

bool Lyrics(const Json& record, LrclibMatch& match, const wchar_t* suffix) {
    if (record.type != Json::Type::Object || record.Flag(L"instrumental")) return false;
    auto synced = record.Text(L"syncedLyrics");
    auto plain = record.Text(L"plainLyrics");
    const bool timed = synced.find_first_not_of(L" \r\n\t") != std::wstring::npos;
    if (!timed && plain.find_first_not_of(L" \r\n\t") == std::wstring::npos) return false;
    match.found = true;
    match.synchronized = timed;
    match.text = timed ? synced : plain;
    match.source = std::wstring(timed ? L"LRCLIB synced" : L"LRCLIB plain") + suffix;
    match.matched = record.Text(L"artistName") + L" - " + record.Text(L"trackName");
    if (const auto length = record.Number(L"duration"); length > 0)
        match.matched += L" (" + FormatDuration(length) + L")";
    return true;
}
}  // namespace

double LrclibSimilarity(const std::wstring& a, const std::wstring& b) {
    // 2*LCS/(|a|+|b|): close to difflib's ratio for short strings like titles.
    if (a.empty() || b.empty()) return 0.0;
    std::vector<std::size_t> previous(b.size() + 1), current(b.size() + 1);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j)
            current[j] = a[i - 1] == b[j - 1] ? previous[j - 1] + 1 : std::max(previous[j], current[j - 1]);
        std::swap(previous, current);
    }
    return 2.0 * static_cast<double>(previous[b.size()]) / static_cast<double>(a.size() + b.size());
}

std::wstring NormalizeLrclibTitle(std::wstring text) {
    text = Lower(std::move(text));
    // Drop bracketed credits/versions and an unbracketed "feat./featuring/ft." tail.
    static const wchar_t* kBracketed[] = {L"feat", L"with", L"remaster", L"live", L"radio edit",
                                          L"explicit", L"clean", L"deluxe", L"bonus track"};
    std::wstring out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'(' || c == L'[') {
            const wchar_t close = c == L'(' ? L')' : L']';
            const auto end = text.find(close, i);
            const auto inner = text.substr(i + 1, end == std::wstring::npos ? std::wstring::npos : end - i - 1);
            const bool noise = std::any_of(std::begin(kBracketed), std::end(kBracketed),
                [&](const wchar_t* word) { return inner.rfind(word, 0) == 0; });
            if (noise && end != std::wstring::npos) { i = end; continue; }
        }
        if (i > 0 && text[i - 1] == L' ' &&
            (StartsWord(text, i, L"feat") || StartsWord(text, i, L"featuring") || StartsWord(text, i, L"ft")))
            break;
        out += c;
    }
    return Words(out);
}

double LrclibArtistSimilarity(const std::wstring& tagged, const std::wstring& candidate) {
    double best = LrclibSimilarity(NormalizeArtist(tagged), NormalizeArtist(candidate));
    for (const auto& left : SplitArtists(tagged))
        for (const auto& right : SplitArtists(candidate))
            best = std::max(best, LrclibSimilarity(left, right));
    return best;
}

LrclibMatch LookupLrclib(const std::wstring& artist, const std::wstring& title,
                         double durationSeconds, double toleranceSeconds) {
    LrclibMatch match;
    if (artist.empty() || title.empty()) {
        match.error = L"the MP3 has no artist or title tag";
        return match;
    }
    const auto base = L"artist_name=" + UrlEncode(artist) + L"&track_name=" + UrlEncode(title);
    std::wstring query = L"/api/get?" + base;
    if (durationSeconds >= 1 && durationSeconds <= 3600)
        query += L"&duration=" + std::to_wstring(std::lround(durationSeconds));
    Json exact;
    if (GetJson(query, exact, match.error) && Lyrics(exact, match, L"")) return match;

    // LRCLIB's search matches the artist field literally, so a tag listing several artists
    // ("Charlie Xcx, Billie Eilish") finds nothing; retry with the first artist alone and
    // then as free text. Every candidate is still checked against the full tags.
    std::vector<std::wstring> searches{base};
    const auto artists = SplitArtists(artist);
    if (artists.size() > 1)
        searches.push_back(L"artist_name=" + UrlEncode(artists.front()) + L"&track_name=" + UrlEncode(title));
    searches.push_back(L"q=" + UrlEncode(title + L" " + (artists.empty() ? artist : artists.front())));
    const auto targetTitle = NormalizeLrclibTitle(title);
    std::wstring searchError;
    bool answered = false;
    for (const auto& search : searches) {
        Json results;
        if (!GetJson(L"/api/search?" + search, results, searchError)) continue;
        if (results.type != Json::Type::Array) continue;
        answered = true;
        std::vector<std::pair<double, const Json*>> ranked;
        for (const auto& record : results.items) {
            if (record.type != Json::Type::Object) continue;
            const double length = record.Number(L"duration");
            if (durationSeconds > 0 && length > 0 && std::abs(length - durationSeconds) > toleranceSeconds) continue;
            const double titleScore = LrclibSimilarity(targetTitle, NormalizeLrclibTitle(record.Text(L"trackName")));
            const double artistScore = LrclibArtistSimilarity(artist, record.Text(L"artistName"));
            if (titleScore >= 0.65 && artistScore >= 0.5)
                ranked.emplace_back(titleScore * 0.7 + artistScore * 0.3, &record);
        }
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
        for (const auto& [score, record] : ranked)
            if (Lyrics(*record, match, L" search")) { match.error.clear(); return match; }
    }
    // A clean answer without a match is a miss, not an outage.
    if (answered) match.error.clear();
    else if (!searchError.empty()) match.error = searchError;
    return match;
}
#endif
