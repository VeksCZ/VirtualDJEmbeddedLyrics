// Matching rules of the built-in LRCLIB fallback (must agree with tools/lrc_tool.py).
// Uses explicit checks instead of assert() so the Release CTest run really verifies them.
#include "LrclibLookup.hpp"

#include <iostream>

namespace {
int failures = 0;
void Check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << "\n"; ++failures; }
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4) {  // manual live check: lrclib_tests "Artist" "Title" seconds
        const auto match = LookupLrclib(argv[1], argv[2], _wtof(argv[3]));
        std::wcout << L"found=" << match.found << L" synced=" << match.synchronized << L" source="
                   << match.source << L" error=" << match.error << L" chars=" << match.text.size() << L"\n";
        return match.found ? 0 : 1;
    }
    Check(NormalizeLrclibTitle(L"Guess featuring billie eilish") == L"guess", "unbracketed featuring");
    Check(NormalizeLrclibTitle(L"Guess (feat. Billie Eilish)") == L"guess", "bracketed feat");
    Check(NormalizeLrclibTitle(L"Tohle je ráj (Remastered 2011)") == L"tohle je ráj", "remaster + diacritics");
    Check(NormalizeLrclibTitle(L"Feathers") == L"feathers", "word starting with feat kept");
    Check(LrclibArtistSimilarity(L"Charlie Xcx, Billie Eilish", L"Charli xcx") > 0.9, "multi-artist misspelling");
    Check(LrclibArtistSimilarity(L"Charlie Xcx, Billie Eilish", L"Vox Freaks") < 0.5, "unrelated artist");
    Check(LrclibSimilarity(L"abc", L"abc") == 1.0, "identical");
    Check(LrclibSimilarity(L"", L"abc") == 0.0, "empty");
    if (failures == 0) std::cout << "lrclib_tests passed\n";
    return failures == 0 ? 0 : 1;
}
