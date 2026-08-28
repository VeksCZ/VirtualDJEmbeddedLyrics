#include "Lyrics.hpp"
#include "MasterDeckSelector.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
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
