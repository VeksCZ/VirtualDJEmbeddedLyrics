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
    bool backgroundEnabled{};
    int backgroundColor{};
    int timedLines{7};
    int untimedLines{7};
    int fontPercent{100};
    int verticalPercent{50};
    int timingMs{};
    bool useUpfaders{};
    bool autoTag{true};
    bool recordTiming{};
    bool wholeLineHighlight{};
    int countdownSeconds{5};
};

bool ShowAdvancedAppearanceDialog(HWND owner, AdvancedAppearanceSettings& settings,
                                  AdvancedAppearanceSettings& customSettings);
#endif
