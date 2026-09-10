#include "AdvancedDialog.hpp"

#ifdef _WIN32
#include <algorithm>
#include <array>

namespace {
constexpr wchar_t kWindowClass[] = L"LrcAdvancedAppearanceDialog";
constexpr int kPreset = 100;
constexpr int kFont = 101;
constexpr int kBackdrop = 102;
constexpr int kStrength = 103;
constexpr int kTextColor = 104;
constexpr int kHighlightColor = 105;
constexpr int kReadColor = 106;
constexpr int kBackgroundEnabled = 107;
constexpr int kBackgroundColor = 108;
constexpr int kPreview = 109;

constexpr const wchar_t* kFonts[] = {L"Arial", L"Segoe UI", L"Verdana", L"Tahoma", L"Trebuchet", L"Calibri"};
constexpr const wchar_t* kBackdrops[] = {L"Outline", L"Shadow", L"Outline + Shadow"};
constexpr const wchar_t* kStrengths[] = {L"Thin", L"Normal", L"Strong"};
struct ColorChoice { const wchar_t* name; COLORREF color; };
constexpr ColorChoice kColors[] = {
    {L"White", RGB(255,255,255)}, {L"Yellow", RGB(255,210,0)}, {L"Gray", RGB(150,150,150)},
    {L"Orange", RGB(255,138,36)}, {L"Red", RGB(240,68,68)}, {L"Green", RGB(66,214,107)},
    {L"Cyan", RGB(69,217,232)}, {L"Blue", RGB(75,131,255)}, {L"Magenta", RGB(217,81,232)}
};
constexpr ColorChoice kBackgroundColors[] = {
    {L"Black", RGB(0,0,0)}, {L"White", RGB(255,255,255)}, {L"Gray", RGB(150,150,150)},
    {L"Red", RGB(240,68,68)}, {L"Green", RGB(66,214,107)}, {L"Blue", RGB(75,131,255)},
    {L"Yellow", RGB(255,210,0)}, {L"Orange", RGB(255,138,36)}, {L"Magenta", RGB(217,81,232)}
};

struct DialogState {
    AdvancedAppearanceSettings working;
    AdvancedAppearanceSettings custom;
    bool accepted{};
    bool finished{};
    HWND owner{};
};

HWND AddControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id = 0) {
    return CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, width, height, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void FillCombo(HWND window, int id, const wchar_t* const* values, int count, int selected) {
    const HWND combo = GetDlgItem(window, id);
    for (int i = 0; i < count; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(values[i]));
    SendMessageW(combo, CB_SETCURSEL, std::clamp(selected, 0, count - 1), 0);
}

void FillColorCombo(HWND window, int id, int selected) {
    const HWND combo = GetDlgItem(window, id);
    for (const auto& color : kColors) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(color.name));
    SendMessageW(combo, CB_SETCURSEL, std::clamp(selected, 0, static_cast<int>(std::size(kColors)) - 1), 0);
}

void FillBackgroundCombo(HWND window, int selected) {
    const HWND combo = GetDlgItem(window, kBackgroundColor);
    for (const auto& color : kBackgroundColors)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(color.name));
    SendMessageW(combo, CB_SETCURSEL,
                 std::clamp(selected, 0, static_cast<int>(std::size(kBackgroundColors)) - 1), 0);
}

int Selection(HWND window, int id) {
    return std::max(0, static_cast<int>(SendDlgItemMessageW(window, id, CB_GETCURSEL, 0, 0)));
}

AdvancedAppearanceSettings ReadSelections(HWND window) {
    return {Selection(window, kFont), Selection(window, kBackdrop), Selection(window, kStrength),
            Selection(window, kTextColor), Selection(window, kHighlightColor), Selection(window, kReadColor),
            IsDlgButtonChecked(window, kBackgroundEnabled) == BST_CHECKED,
            Selection(window, kBackgroundColor)};
}

void SetSelections(HWND window, const AdvancedAppearanceSettings& value) {
    SendDlgItemMessageW(window, kFont, CB_SETCURSEL, value.font, 0);
    SendDlgItemMessageW(window, kBackdrop, CB_SETCURSEL, value.backdrop, 0);
    SendDlgItemMessageW(window, kStrength, CB_SETCURSEL, value.strength, 0);
    SendDlgItemMessageW(window, kTextColor, CB_SETCURSEL, value.textColor, 0);
    SendDlgItemMessageW(window, kHighlightColor, CB_SETCURSEL, value.highlightColor, 0);
    SendDlgItemMessageW(window, kReadColor, CB_SETCURSEL, value.readColor, 0);
    CheckDlgButton(window, kBackgroundEnabled,
                   value.backgroundEnabled ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(window, kBackgroundColor, CB_SETCURSEL, value.backgroundColor, 0);
    InvalidateRect(window, nullptr, FALSE);
}

void ApplyPreset(HWND window, int preset) {
    AdvancedAppearanceSettings value;
    if (preset == 1) value = {2, 2, 2, 0, 1, 2};
    else if (preset == 2) value = {1, 1, 1, 0, 1, 2};
    SetSelections(window, value);
}

bool SameAppearance(const AdvancedAppearanceSettings& left,
                    const AdvancedAppearanceSettings& right) {
    return left.font == right.font && left.backdrop == right.backdrop &&
           left.strength == right.strength && left.textColor == right.textColor &&
           left.highlightColor == right.highlightColor && left.readColor == right.readColor &&
           left.backgroundEnabled == right.backgroundEnabled &&
           left.backgroundColor == right.backgroundColor;
}

int MatchingPreset(const AdvancedAppearanceSettings& value) {
    AdvancedAppearanceSettings preset;
    if (SameAppearance(value, preset)) return 0;
    preset = {2, 2, 2, 0, 1, 2};
    if (SameAppearance(value, preset)) return 1;
    preset = {1, 1, 1, 0, 1, 2};
    return SameAppearance(value, preset) ? 2 : 3;
}

void DrawPreview(HWND window, const DRAWITEMSTRUCT& item) {
    const auto value = ReadSelections(window);
    RECT area = item.rcItem;
    HBRUSH base = CreateSolidBrush(value.backgroundEnabled
        ? kBackgroundColors[std::clamp(value.backgroundColor, 0, 8)].color : RGB(42, 47, 54));
    FillRect(item.hDC, &area, base);
    DeleteObject(base);
    if (!value.backgroundEnabled) {
        HBRUSH tile = CreateSolidBrush(RGB(54, 61, 69));
        for (int y = area.top; y < area.bottom; y += 32)
            for (int x = area.left; x < area.right; x += 32)
                if (((x - area.left) / 32 + (y - area.top) / 32) % 2)
                    {
                        RECT square{x, y, std::min<LONG>(x + 32, area.right),
                                    std::min<LONG>(y + 32, area.bottom)};
                        FillRect(item.hDC, &square, tile);
                    }
        DeleteObject(tile);
    }
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(190, 196, 204));
    RECT caption{area.left + 14, area.top + 10, area.right - 14, area.top + 32};
    DrawTextW(item.hDC, L"PREVIEW", -1, &caption, DT_LEFT | DT_SINGLELINE);

    HFONT font = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                             kFonts[std::clamp(value.font, 0, 5)]);
    const auto oldFont = SelectObject(item.hDC, font);
    const wchar_t* lines[] = {L"The previous lyric line", L"Current highlighted lyrics", L"The next lyric line"};
    const int colors[] = {value.readColor, value.highlightColor, value.textColor};
    const int centerY = (area.top + area.bottom) / 2;
    for (int index = 0; index < 3; ++index) {
        RECT line{area.left + 12, centerY - 55 + index * 42, area.right - 12,
                  centerY - 18 + index * 42};
        const int thickness = value.strength + 1;
        if (value.backdrop == 1 || value.backdrop == 2) {
            RECT shadow = line; OffsetRect(&shadow, thickness + 2, thickness + 2);
            SetTextColor(item.hDC, RGB(0, 0, 0));
            DrawTextW(item.hDC, lines[index], -1, &shadow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        if (value.backdrop == 0 || value.backdrop == 2) {
            SetTextColor(item.hDC, RGB(0, 0, 0));
            for (int dy = -thickness; dy <= thickness; ++dy)
                for (int dx = -thickness; dx <= thickness; ++dx)
                    if (dx || dy) {
                        RECT outline = line; OffsetRect(&outline, dx, dy);
                        DrawTextW(item.hDC, lines[index], -1, &outline,
                                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                    }
        }
        SetTextColor(item.hDC, kColors[std::clamp(colors[index], 0, 8)].color);
        DrawTextW(item.hDC, lines[index], -1, &line, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(item.hDC, oldFont);
    DeleteObject(font);
    FrameRect(item.hDC, &area, GetSysColorBrush(COLOR_WINDOWFRAME));
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<DialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE: {
        AddControl(window, L"STATIC", L"Preset", 0, 16, 17, 92, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 116, 14, 246, 180, kPreset);
        AddControl(window, L"BUTTON", L"Typography", BS_GROUPBOX, 12, 48, 354, 112);
        AddControl(window, L"STATIC", L"Font", 0, 28, 73, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 116, 70, 232, 160, kFont);
        AddControl(window, L"STATIC", L"Backdrop", 0, 28, 102, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 116, 99, 232, 120, kBackdrop);
        AddControl(window, L"STATIC", L"Strength", 0, 28, 131, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 116, 128, 232, 120, kStrength);
        AddControl(window, L"BUTTON", L"Colors", BS_GROUPBOX, 12, 168, 354, 112);
        AddControl(window, L"STATIC", L"Text", 0, 28, 193, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | WS_TABSTOP, 116, 190, 232, 190, kTextColor);
        AddControl(window, L"STATIC", L"Highlight", 0, 28, 222, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | WS_TABSTOP, 116, 219, 232, 190, kHighlightColor);
        AddControl(window, L"STATIC", L"Read", 0, 28, 251, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | WS_TABSTOP, 116, 248, 232, 190, kReadColor);
        AddControl(window, L"BUTTON", L"Background", BS_GROUPBOX, 12, 288, 354, 82);
        AddControl(window, L"BUTTON", L"Use solid background", BS_AUTOCHECKBOX | WS_TABSTOP,
                   28, 311, 166, 22, kBackgroundEnabled);
        AddControl(window, L"STATIC", L"Color", 0, 28, 341, 82, 20);
        AddControl(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | WS_TABSTOP,
                   116, 338, 232, 190, kBackgroundColor);
        AddControl(window, L"STATIC", L"", SS_OWNERDRAW, 386, 48, 350, 322, kPreview);
        AddControl(window, L"BUTTON", L"Apply", BS_DEFPUSHBUTTON | WS_TABSTOP, 570, 386, 78, 27, IDOK);
        AddControl(window, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 658, 386, 78, 27, IDCANCEL);
        const wchar_t* presets[] = {L"Default", L"Photos", L"Clean", L"Custom"};
        FillCombo(window, kPreset, presets, 4, MatchingPreset(state->working));
        FillCombo(window, kFont, kFonts, static_cast<int>(std::size(kFonts)), state->working.font);
        FillCombo(window, kBackdrop, kBackdrops, static_cast<int>(std::size(kBackdrops)), state->working.backdrop);
        FillCombo(window, kStrength, kStrengths, static_cast<int>(std::size(kStrengths)), state->working.strength);
        FillColorCombo(window, kTextColor, state->working.textColor);
        FillColorCombo(window, kHighlightColor, state->working.highlightColor);
        FillColorCombo(window, kReadColor, state->working.readColor);
        CheckDlgButton(window, kBackgroundEnabled,
                       state->working.backgroundEnabled ? BST_CHECKED : BST_UNCHECKED);
        FillBackgroundCombo(window, state->working.backgroundColor);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID == kPreview) { DrawPreview(window, *item); return TRUE; }
        if ((item->CtlID < kTextColor || item->CtlID > kReadColor) &&
            item->CtlID != kBackgroundColor) break;
        if (item->itemID == static_cast<UINT>(-1)) break;
        const auto& palette = item->CtlID == kBackgroundColor ? kBackgroundColors : kColors;
        const auto paletteSize = item->CtlID == kBackgroundColor
            ? std::size(kBackgroundColors) : std::size(kColors);
        const auto index = std::min<std::size_t>(item->itemID, paletteSize - 1);
        FillRect(item->hDC, &item->rcItem, GetSysColorBrush((item->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        RECT swatch{item->rcItem.left + 5, item->rcItem.top + 3, item->rcItem.left + 39, item->rcItem.bottom - 3};
        HBRUSH brush = CreateSolidBrush(palette[index].color);
        FillRect(item->hDC, &swatch, brush); DeleteObject(brush); FrameRect(item->hDC, &swatch, GetSysColorBrush(COLOR_WINDOWFRAME));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, GetSysColor((item->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        RECT label = item->rcItem; label.left += 47;
        DrawTextW(item->hDC, palette[index].name, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kPreset && HIWORD(wParam) == CBN_SELCHANGE) {
            const int preset = Selection(window, kPreset);
            if (preset < 3) ApplyPreset(window, preset);
            else SetSelections(window, state->custom);
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            state->working = ReadSelections(window);
            state->accepted = true; state->finished = true; DestroyWindow(window); return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { state->finished = true; DestroyWindow(window); return 0; }
        if ((HIWORD(wParam) == CBN_SELCHANGE || LOWORD(wParam) == kBackgroundEnabled) &&
            LOWORD(wParam) != kPreset) {
            state->custom = ReadSelections(window);
            SendDlgItemMessageW(window, kPreset, CB_SETCURSEL, 3, 0);
            InvalidateRect(GetDlgItem(window, kPreview), nullptr, FALSE);
        }
        break;
    case WM_CLOSE:
        if (state) state->finished = true;
        DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

bool ShowAdvancedAppearanceDialog(HWND owner, AdvancedAppearanceSettings& settings,
                                  AdvancedAppearanceSettings& customSettings) {
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    DialogState state{settings, customSettings, false, false, owner};
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass, L"LRC Presets",
                                  WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 764, 459,
                                  owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!window) return false;
    RECT bounds{}; GetWindowRect(window, &bounds);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - (bounds.right - bounds.left)) / 2;
    const int y = work.top + (work.bottom - work.top - (bounds.bottom - bounds.top)) / 2;
    SetWindowPos(window, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
    MSG message{};
    while (!state.finished && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    if (state.accepted) {
        settings = state.working;
        customSettings = state.custom;
    }
    return state.accepted;
}
#endif
