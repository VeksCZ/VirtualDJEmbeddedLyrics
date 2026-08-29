#include "Lyrics.hpp"
#include "LyricsTiming.hpp"
#include "MasterDeckSelector.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    assert(EstimateLyricHighlightMs(L"Short line") >= 1200);
    assert(EstimateLyricHighlightMs(std::wstring(200, L'a')) == 4500);
    assert(UnitProgress(2000, 1000, 2000) == 0.5f);
    assert(UnitProgress(10000, 1000, 2000) == 1.0f);
    assert(LyricCountdown(1) == 1);
    assert(LyricCountdown(5000) == 5);
    assert(LyricCountdown(5001) == 6);
    assert(LyricCountdown(10000) == 10);
    assert(LyricCountdown(63001) == 64);
    assert(LyricCountdown(0) == -1);
    assert(LyricPauseDisplayText(L"12", 1, 12000) == L"> 12 <");
    assert(LyricPauseDisplayText(L"12", 0, 4500) == L"> 5 <");
    assert(LyricPauseDisplayText(L"12", 0, 0).empty());
    assert(LyricPauseDisplayText(L"12", -1, 0).empty());

    MasterDeckSelector selector;
    assert(selector.Select(0.2, 1, 2) == 1);
    assert(selector.Select(0.59, 1, 2) == 1);
    assert(selector.Select(0.60, 1, 2) == 2);
    assert(selector.Select(0.41, 1, 2) == 2);
    assert(selector.Select(0.40, 1, 2) == 1);
    assert(selector.Select(0.8, 3, 4) == 4);

    const auto path = std::filesystem::temp_directory_path() / "vdj_embedded_lyrics_test.lrc";
    {
        std::ofstream out(path, std::ios::binary);
        out << "[00:01.20]First line\n"
               "[00:02.500]<00:02.500>Hello <00:02.900>world\n"
               "[00:04.00][00:05.00]Repeated\n";
    }
    const auto result = LoadLrc(path);
    assert(result.error.empty());
    assert(result.document.lines.size() == 4);
    assert(result.document.lines[0].timeMs == 1200);
    assert(result.document.lines[1].tokens.size() == 2);
    assert(result.document.lines[3].timeMs == 5000);
    std::filesystem::remove(path);

    const auto timedTxtPath = std::filesystem::temp_directory_path() / "vdj_embedded_lyrics_timed_test.txt";
    {
        std::ofstream out(timedTxtPath, std::ios::binary);
        out << "[00:01]First from TXT\n[00:02.500]Second from TXT\n";
    }
    const auto timedTxt = LoadPlainTextLyrics(timedTxtPath);
    assert(timedTxt.error.empty());
    assert(timedTxt.document.synchronized);
    assert(timedTxt.document.source == L"sidecar timed TXT");
    assert(timedTxt.document.lines.size() == 2);
    assert(timedTxt.document.lines[0].timeMs == 1000);
    assert(timedTxt.document.lines[1].timeMs == 2500);
    std::filesystem::remove(timedTxtPath);

    if (argc > 1) {
        const auto embedded = LoadEmbeddedTimedLyrics(std::filesystem::path(argv[1]));
        std::wcerr << L"Embedded source: " << embedded.document.source
                   << L", lines: " << embedded.document.lines.size()
                   << L", error: " << embedded.error << L'\n';
        assert(embedded.error.empty());
        assert(embedded.document.source == L"embedded TXXX:USLT");
        assert(embedded.document.lines.size() == 47);
        assert(embedded.document.lines.front().timeMs == 12320);
        assert(embedded.document.lines.front().text == L"Po lávce tvého nártu");
        assert(embedded.document.lines.back().timeMs == 191880);
        assert(embedded.document.lines.back().text == L"A mám");
        const auto untimed = LoadEmbeddedUntimedLyrics(std::filesystem::path(argv[1]));
        assert(untimed.error.empty());
        assert(!untimed.document.synchronized);
        assert(untimed.document.source == L"embedded ID3 USLT");
        assert(untimed.document.lines.size() > 40);
        assert(untimed.document.lines.front().text == L"Po lávce tvého nártu");
    }
    std::cout << "All lyrics parser tests passed\n";
}
