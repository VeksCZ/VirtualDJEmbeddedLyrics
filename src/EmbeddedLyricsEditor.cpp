#include "EmbeddedLyricsEditor.hpp"

#ifdef _WIN32
#include <algorithm>

namespace {
constexpr wchar_t kClass[] = L"EmbeddedLyricsEditorDialog";
constexpr int kMode = 100;
constexpr int kText = 101;
struct State { EmbeddedLyricsEdit value; bool accepted{}; bool done{}; HWND owner{}; };

HWND Add(HWND parent, const wchar_t* kind, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id = 0) {
    return CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE: {
        Add(window, L"STATIC", L"Lyrics type", 0, 16, 18, 100, 20);
        const auto mode = Add(window, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                              118, 14, 180, 110, kMode);
        SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Timed (LRC timestamps)"));
        SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Untimed"));
        SendMessageW(mode, CB_SETCURSEL, state->value.synchronized ? 0 : 1, 0);
        Add(window, L"STATIC", L"Timed rows use [mm:ss.xx] text. Saving replaces existing embedded lyrics.",
            0, 16, 48, 650, 20);
        const auto edit = Add(window, L"EDIT", state->value.text.c_str(),
                              WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
                              ES_WANTRETURN | WS_VSCROLL, 16, 72, 650, 350, kText);
        SendMessageW(edit, EM_SETLIMITTEXT, 1024 * 1024, 0);
        Add(window, L"BUTTON", L"Save to MP3 tag", BS_DEFPUSHBUTTON | WS_TABSTOP,
            430, 438, 130, 30, IDOK);
        Add(window, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP,
            572, 438, 94, 30, IDCANCEL);
        SetFocus(edit); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            const auto edit = GetDlgItem(window, kText);
            const int length = GetWindowTextLengthW(edit);
            state->value.text.resize(static_cast<std::size_t>(std::max(0, length)));
            if (length) GetWindowTextW(edit, state->value.text.data(), length + 1);
            state->value.synchronized = SendDlgItemMessageW(window, kMode, CB_GETCURSEL, 0, 0) == 0;
            state->accepted = true; state->done = true; DestroyWindow(window); return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { state->done = true; DestroyWindow(window); return 0; }
        break;
    case WM_CLOSE: if (state) state->done = true; DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

bool ShowEmbeddedLyricsEditor(HWND owner, EmbeddedLyricsEdit& edit) {
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = Proc; cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_IBEAM); cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    cls.lpszClassName = kClass; RegisterClassExW(&cls);
    State state{edit, false, false, owner};
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"Edit embedded lyrics",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 690, 510,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!window) return false;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
    MSG message{};
    while (!state.done && GetMessageW(&message, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    if (state.accepted) edit = std::move(state.value);
    return state.accepted;
}
#endif