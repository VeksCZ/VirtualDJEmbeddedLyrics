#pragma once

#ifdef _WIN32
#include <string>
#include <windows.h>

struct EmbeddedLyricsEdit {
    std::wstring text;
    bool synchronized{};
};

bool ShowEmbeddedLyricsEditor(HWND owner, EmbeddedLyricsEdit& edit);
#endif