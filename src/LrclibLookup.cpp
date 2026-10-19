#include "LrclibLookup.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

// Portable on purpose (Windows and macOS): no locale-dependent <cwctype> classification,
// because the "C" locale on macOS treats every non-ASCII letter as punctuation.
namespace {
bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }
bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// Letters and digits for matching purposes: ASCII alnum plus every non-ASCII character
// except the common punctuation/symbol blocks.
bool IsWordChar(wchar_t c) {
    if (c < 0x80) return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || IsDigit(c) || c == L'_';
    if (c >= 0x80 && c <= 0xBF) return c == 0xAA || c == 0xB5 || c == 0xBA;  // Latin-1 symbols
    if (c == 0xD7 || c == 0xF7) return false;                               // × ÷
    if (c >= 0x2000 && c <= 0x206F) return false;                           // general punctuation
    if (c >= 0x3000 && c <= 0x303F) return false;                           // CJK punctuation
    if (c >= 0xFF00 && c <= 0xFF0F) return false;                           // fullwidth punctuation
    return true;
}

wchar_t ToLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + 32);
    if (c < 0xC0) return c;
    if ((c >= 0xC0 && c <= 0xDE) && c != 0xD7) return static_cast<wchar_t>(c + 32);  // Latin-1
    if (c >= 0x100 && c <= 0x137 && (c % 2) == 0) return static_cast<wchar_t>(c + 1);  // Latin Ext-A
    if (c >= 0x139 && c <= 0x148 && (c % 2) == 1) return static_cast<wchar_t>(c + 1);
    if (c >= 0x14A && c <= 0x177 && (c % 2) == 0) return static_cast<wchar_t>(c + 1);
    if (c == 0x178) return 0xFF;
    if (c >= 0x179 && c <= 0x17E && (c % 2) == 1) return static_cast<wchar_t>(c + 1);
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return static_cast<wchar_t>(c + 32);  // Greek
    if (c >= 0x410 && c <= 0x42F) return static_cast<wchar_t>(c + 32);                 // Cyrillic
    if (c >= 0x400 && c <= 0x40F) return static_cast<wchar_t>(c + 80);
    return c;
}

std::wstring Lower(std::wstring text) {
    for (auto& c : text) c = ToLower(c);
    return text;
}

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
    void Skip() { while (p_ < s_.size() && IsSpace(s_[p_])) ++p_; }
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
        while (p_ < s_.size() && (IsDigit(s_[p_]) || std::wcschr(L"+-.eE", s_[p_]))) ++p_;
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
            if (h >= L'0' && h <= L'9') value |= static_cast<unsigned>(h - L'0');
            else if (h >= L'a' && h <= L'f') value |= static_cast<unsigned>(h - L'a' + 10);
            else if (h >= L'A' && h <= L'F') value |= static_cast<unsigned>(h - L'A' + 10);
            else return false;
        }
        return true;
    }
    void Append(std::wstring& out, unsigned code) {
        if constexpr (sizeof(wchar_t) == 2) {
            out += static_cast<wchar_t>(code);  // surrogate halves pass through unchanged
        } else {
            // UTF-32 wchar_t (macOS): join 🎵 style surrogate pairs.
            if (code >= 0xDC00 && code <= 0xDFFF && !out.empty()) {
                const auto high = static_cast<unsigned>(out.back());
                if (high >= 0xD800 && high <= 0xDBFF) {
                    out.back() = static_cast<wchar_t>(0x10000 + ((high - 0xD800) << 10) + (code - 0xDC00));
                    return;
                }
            }
            out += static_cast<wchar_t>(code);
        }
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
                Append(out, code);
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
std::string UrlEncode(const std::wstring& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : LrclibUtf8(value)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

// One retry on 5xx, like the Python client: LRCLIB returns 502/503 when briefly overloaded.
bool GetJson(const std::string& pathAndQuery, Json& out, std::wstring& error) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto response = LrclibHttpGet(pathAndQuery);
        if (!response.error.empty()) { error = response.error; return false; }
        if (response.status == 404) return false;
        if (response.status >= 500 && attempt == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            continue;
        }
        if (response.status != 200) { error = L"HTTP " + std::to_wstring(response.status); return false; }
        if (!JsonParser(LrclibWiden(response.body)).Parse(out)) { error = L"unreadable LRCLIB answer"; return false; }
        return true;
    }
    return false;
}

// ---- Matching (same rules as tools/lrc_tool.py) ------------------------------------
std::wstring Words(const std::wstring& text) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : text) {
        if (IsWordChar(c)) {
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
    return end >= text.size() || !IsWordChar(text[end]);
}

std::wstring NormalizeArtist(std::wstring text) {
    text = Lower(std::move(text));
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((i == 0 || !IsWordChar(text[i - 1])) &&
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
        const bool boundary = i == 0 || !IsWordChar(lower[i - 1]);
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
    const long total = std::lround(seconds);
    wchar_t buffer[16]{};
    std::swprintf(buffer, 16, L"%ld:%02ld", total / 60, total % 60);
    return buffer;
}

bool HasText(const std::wstring& text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t c) { return !IsSpace(c); });
}

bool Lyrics(const Json& record, LrclibMatch& match, const wchar_t* suffix) {
    if (record.type != Json::Type::Object || record.Flag(L"instrumental")) return false;
    auto synced = record.Text(L"syncedLyrics");
    auto plain = record.Text(L"plainLyrics");
    const bool timed = HasText(synced);
    if (!timed && !HasText(plain)) return false;
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

std::string LrclibUtf8(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto code = static_cast<std::uint32_t>(value[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (code >= 0xD800 && code <= 0xDBFF && i + 1 < value.size()) {
                const auto low = static_cast<std::uint32_t>(value[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

std::wstring LrclibWiden(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        std::uint32_t code = 0xFFFD;
        std::size_t length = 1;
        if (lead < 0x80) { code = lead; }
        else if ((lead >> 5) == 0x6) { length = 2; code = lead & 0x1F; }
        else if ((lead >> 4) == 0xE) { length = 3; code = lead & 0x0F; }
        else if ((lead >> 3) == 0x1E) { length = 4; code = lead & 0x07; }
        if (length > 1) {
            if (i + length > utf8.size()) { code = 0xFFFD; length = 1; }
            else {
                for (std::size_t k = 1; k < length; ++k) {
                    const auto next = static_cast<unsigned char>(utf8[i + k]);
                    if ((next >> 6) != 0x2) { code = 0xFFFD; length = 1; break; }
                    code = (code << 6) | (next & 0x3F);
                }
            }
        }
        i += length;
        if constexpr (sizeof(wchar_t) == 2) {
            if (code >= 0x10000) {
                code -= 0x10000;
                out += static_cast<wchar_t>(0xD800 + (code >> 10));
                out += static_cast<wchar_t>(0xDC00 + (code & 0x3FF));
                continue;
            }
        }
        out += static_cast<wchar_t>(code);
    }
    return out;
}

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
    const auto base = "artist_name=" + UrlEncode(artist) + "&track_name=" + UrlEncode(title);
    std::string query = "/api/get?" + base;
    if (durationSeconds >= 1 && durationSeconds <= 3600)
        query += "&duration=" + std::to_string(std::lround(durationSeconds));
    Json exact;
    if (GetJson(query, exact, match.error) && Lyrics(exact, match, L"")) return match;

    // LRCLIB's search matches the artist field literally, so a tag listing several artists
    // ("Charlie Xcx, Billie Eilish") finds nothing; retry with the first artist alone and
    // then as free text. Every candidate is still checked against the full tags.
    std::vector<std::string> searches{base};
    const auto artists = SplitArtists(artist);
    if (artists.size() > 1)
        searches.push_back("artist_name=" + UrlEncode(artists.front()) + "&track_name=" + UrlEncode(title));
    searches.push_back("q=" + UrlEncode(title + L" " + (artists.empty() ? artist : artists.front())));
    const auto targetTitle = NormalizeLrclibTitle(title);
    std::wstring searchError;
    bool answered = false;
    for (const auto& search : searches) {
        Json results;
        if (!GetJson("/api/search?" + search, results, searchError)) continue;
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
