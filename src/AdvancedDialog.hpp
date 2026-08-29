#pragma once

#ifdef _WIN32
#include <windows.h>

struct AdvancedAppearanceSettings {
    int font{};
    int backdrop{};
    int strength{1};
    int textColor{};
    int highlightColor{1};
    int readColor{2};
};

bool ShowAdvancedAppearanceDialog(HWND owner, AdvancedAppearanceSettings& settings);
#endif
