#pragma once
#ifdef _WIN32
#include <string>
#include <windows.h>
struct EmbeddedLyricsEdit { std::wstring timedText; std::wstring untimedText; bool synchronized{}; };
bool ShowEmbeddedLyricsEditor(HWND owner, EmbeddedLyricsEdit& edit);
#endif
