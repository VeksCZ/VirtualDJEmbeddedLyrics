// Matching rules of the built-in LRCLIB fallback (must agree with tools/lrc_tool.py).
// Uses explicit checks instead of assert() so the Release CTest run really verifies them.
#include "LrclibLookup.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void Check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << "\n"; ++failures; }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 4) {  // manual live check: lrclib_tests "Artist" "Title" seconds (UTF-8 arguments)
        const auto match = LookupLrclib(LrclibWiden(argv[1]), LrclibWiden(argv[2]), std::atof(argv[3]));
        std::cout << "found=" << match.found << " synced=" << match.synchronized << " source="
                  << LrclibUtf8(match.source) << " error=" << LrclibUtf8(match.error)
                  << " chars=" << match.text.size() << "\n";
        return match.found ? 0 : 1;
    }
    Check(NormalizeLrclibTitle(L"Guess featuring billie eilish") == L"guess", "unbracketed featuring");
    Check(NormalizeLrclibTitle(L"Guess (feat. Billie Eilish)") == L"guess", "bracketed feat");
    Check(NormalizeLrclibTitle(L"Tohle je RÁJ (Remastered 2011)") == L"tohle je ráj", "remaster + diacritics");
    Check(NormalizeLrclibTitle(L"Žlutý kůň") == L"žlutý kůň", "Czech letters stay letters");
    Check(NormalizeLrclibTitle(L"Feathers") == L"feathers", "word starting with feat kept");
    Check(LrclibArtistSimilarity(L"Charlie Xcx, Billie Eilish", L"Charli xcx") > 0.9, "multi-artist misspelling");
    Check(LrclibArtistSimilarity(L"Charlie Xcx, Billie Eilish", L"Vox Freaks") < 0.5, "unrelated artist");
    Check(LrclibSimilarity(L"abc", L"abc") == 1.0, "identical");
    Check(LrclibSimilarity(L"", L"abc") == 0.0, "empty");
    Check(LrclibWiden(LrclibUtf8(L"Příliš \U0001F3B5")) == L"Příliš \U0001F3B5", "UTF-8 round trip");
    if (failures == 0) std::cout << "lrclib_tests passed\n";
    return failures == 0 ? 0 : 1;
}
