#pragma once
#ifdef _WIN32
#include <filesystem>
#include <string>
#include <windows.h>

struct EmbeddedLyricsEdit {
    std::wstring timedText;
    std::wstring untimedText;
    bool synchronized{};
    std::wstring songLabel;
    // Online lookup. lookupExecutable is the LrcHelper (TIDAL + LRCLIB); when it is empty
    // or cannot start, the dialog falls back to its built-in LRCLIB-only search.
    std::filesystem::path audioPath;
    std::wstring lookupExecutable;
    std::wstring lookupScript;
    double durationSeconds{};  // 0 when unknown; narrows the built-in LRCLIB search
};

bool ShowEmbeddedLyricsEditor(HWND owner, EmbeddedLyricsEdit& edit);
#endif
