#include "EmbeddedLyricsEditor.hpp"
#ifdef _WIN32
#include "LrcShift.hpp"
#include "LrclibLookup.hpp"
#include "Lyrics.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

// The lyrics window: what the MP3 holds now, an online search (TIDAL, then LRCLIB, run by
// LyricsTools in headless mode) and the manual Timed/Untimed editor. Nothing is written to
// the MP3 until "Save to MP3 tag"; found lyrics only replace the text in the editor.
namespace {
constexpr wchar_t kClass[] = L"EmbeddedLyricsEditorDialog";
constexpr int kMode = 100, kText = 101, kSearch = 102, kStatus = 103, kCurrent = 104;
constexpr int kStartAt = 105, kShift = 106;
constexpr UINT kLookupDone = WM_APP + 1;
constexpr DWORD kLookupTimeoutMs = 90000;

struct LookupResult {
    std::map<std::wstring, std::wstring> fields;
    std::wstring text;
    std::wstring failure;  // set when the helper could not run at all
};

struct State {
    EmbeddedLyricsEdit value;
    bool accepted{}, done{}, searching{};
    HWND owner{};
    HFONT font{};
};

HWND Add(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id = 0) {
    return CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

bool TimedSelected(HWND w) { return SendDlgItemMessageW(w, kMode, CB_GETCURSEL, 0, 0) == 0; }

std::wstring EditorText(HWND w) {
    HWND e = GetDlgItem(w, kText);
    const int n = std::max(0, GetWindowTextLengthW(e));
    std::wstring v(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(e, v.data(), n + 1);
    v.resize(static_cast<size_t>(n));
    return v;
}

void Store(HWND w, State& s, bool timed) {
    (timed ? s.value.timedText : s.value.untimedText) = EditorText(w);
}

void Show(HWND w, State& s) {
    const auto& v = TimedSelected(w) ? s.value.timedText : s.value.untimedText;
    SetDlgItemTextW(w, kText, v.c_str());
}

size_t CountLines(const std::wstring& text) {
    size_t count = 0;
    std::wistringstream lines(text);
    for (std::wstring line; std::getline(lines, line);)
        if (line.find_first_not_of(L" \t\r") != std::wstring::npos) ++count;
    return count;
}

std::wstring Describe(const std::wstring& text) {
    const auto lines = CountLines(text);
    return lines ? L"yes (" + std::to_wstring(lines) + L" lines)" : std::wstring(L"no");
}

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(0, size)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size);
    return result;
}

std::wstring ToEditLineBreaks(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size() + text.size() / 20);
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        if (c == L'\n') result += L"\r\n"; else result += c;
    }
    return result;
}

// Runs on a worker thread: starts LyricsTools' headless lookup and parses its result file.
std::unique_ptr<LookupResult> RunLookup(const std::wstring& executable, const std::wstring& script,
                                        const std::filesystem::path& audio) {
    auto result = std::make_unique<LookupResult>();
    std::error_code error;
    const auto output = std::filesystem::temp_directory_path(error) /
        (L"LrcLookup-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".txt");
    std::wstring command = L"\"" + executable + L"\"";
    if (!script.empty()) command += L" \"" + script + L"\"";
    command += L" --lookup-lyrics \"" + audio.wstring() + L"\" \"" + output.wstring() + L"\"";

    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) {
        result->failure = L"LyricsTools could not be started (error " +
                          std::to_wstring(GetLastError()) + L"). Reinstall the plugin from LyricsTools.";
        return result;
    }
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, kLookupTimeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        std::filesystem::remove(output, error);
        result->failure = L"The online search timed out.";
        return result;
    }
    CloseHandle(process.hProcess);

    std::ifstream input(output, std::ios::binary);
    std::ostringstream buffer; buffer << input.rdbuf();
    input.close();
    std::filesystem::remove(output, error);
    std::string raw = buffer.str();
    if (raw.empty()) {
        result->failure = L"LyricsTools returned no answer.";
        return result;
    }
    raw.erase(std::remove(raw.begin(), raw.end(), '\r'), raw.end());
    const auto split = raw.find("\n\n");
    const std::string header = raw.substr(0, split);
    if (split != std::string::npos) result->text = Widen(raw.substr(split + 2));
    std::istringstream lines(header);
    for (std::string line; std::getline(lines, line);) {
        const auto equals = line.find('=');
        if (equals != std::string::npos)
            result->fields[Widen(line.substr(0, equals))] = Widen(line.substr(equals + 1));
    }
    return result;
}

std::wstring Field(const LookupResult& r, const wchar_t* key) {
    const auto it = r.fields.find(key);
    return it == r.fields.end() ? std::wstring{} : it->second;
}

void ShowCurrentState(HWND w, const State& s) {
    const auto text = L"In the MP3 now:   Timed: " + Describe(s.value.timedText) +
                      L"     Untimed: " + Describe(s.value.untimedText);
    SetDlgItemTextW(w, kCurrent, text.c_str());
}

// Fallback when the LrcHelper/LyricsTools is not installed: LRCLIB only, straight from C++.
std::unique_ptr<LookupResult> RunBuiltInLookup(const std::filesystem::path& audio, double duration) {
    auto result = std::make_unique<LookupResult>();
    const auto tags = ReadBasicTrackTags(audio);
    std::wstring query = tags.artist + (tags.artist.empty() ? L"" : L" - ") + tags.title;
    if (duration > 0) {
        const int total = static_cast<int>(duration + 0.5);
        query += L" (" + std::to_wstring(total / 60) + (total % 60 < 10 ? L":0" : L":") +
                 std::to_wstring(total % 60) + L")";
    }
    result->fields[L"query"] = query;
    const auto match = LookupLrclib(tags.artist, tags.title, duration);
    if (match.found) {
        result->fields[L"status"] = L"found";
        result->fields[L"source"] = match.source;
        result->fields[L"synchronized"] = match.synchronized ? L"1" : L"0";
        result->fields[L"matched"] = match.matched;
        result->text = match.text;
    } else {
        result->fields[L"status"] = match.error.empty() ? L"not_found" : L"error";
        result->fields[L"message"] = match.error.empty()
            ? L"LRCLIB: not found" : L"LRCLIB unavailable (" + match.error + L")";
    }
    result->fields[L"message"] += std::wstring(result->fields[L"message"].empty() ? L"" : L"; ") +
        L"TIDAL is searched only when the plugin is installed by LyricsTools";
    return result;
}

void StartSearch(HWND w, State& s) {
    if (s.searching) return;
    if (s.value.audioPath.empty()) {
        SetDlgItemTextW(w, kStatus, L"No MP3 file to search for.");
        return;
    }
    s.searching = true;
    EnableWindow(GetDlgItem(w, kSearch), FALSE);
    const bool helper = !s.value.lookupExecutable.empty();
    SetDlgItemTextW(w, kStatus, helper ? L"Searching TIDAL, then LRCLIB..." : L"Searching LRCLIB...");
    std::thread([w, helper, exe = s.value.lookupExecutable, script = s.value.lookupScript,
                 audio = s.value.audioPath, duration = s.value.durationSeconds] {
        auto result = helper ? RunLookup(exe, script, audio) : RunBuiltInLookup(audio, duration);
        // The helper could not even start (moved/deleted): still try LRCLIB directly.
        if (helper && !result->failure.empty()) {
            const auto failure = result->failure;
            result = RunBuiltInLookup(audio, duration);
            result->fields[L"message"] = failure + L" " + result->fields[L"message"];
        }
        if (PostMessageW(w, kLookupDone, 0, reinterpret_cast<LPARAM>(result.get())))
            result.release();  // ownership passes to the window
    }).detach();
}

void FinishSearch(HWND w, State& s, std::unique_ptr<LookupResult> result) {
    s.searching = false;
    EnableWindow(GetDlgItem(w, kSearch), TRUE);
    if (!result->failure.empty()) {
        SetDlgItemTextW(w, kStatus, result->failure.c_str());
        return;
    }
    const auto status = Field(*result, L"status");
    const auto message = Field(*result, L"message");
    const auto query = Field(*result, L"query");
    if (status != L"found" || result->text.empty()) {
        std::wstring text = status == L"login_required"
            ? L"Not found. TIDAL is not signed in: sign in once in LyricsTools (Online tab)."
            : status == L"error" ? L"Search failed." : L"No lyrics found online.";
        if (!message.empty()) text += L"\r\n" + message;
        if (!query.empty()) text += L"\r\nLooked for: " + query;
        SetDlgItemTextW(w, kStatus, text.c_str());
        return;
    }
    const bool timed = Field(*result, L"synchronized") == L"1";
    const auto lyrics = ToEditLineBreaks(result->text);
    Store(w, s, TimedSelected(w));
    auto& target = timed ? s.value.timedText : s.value.untimedText;
    if (!target.empty() && target != lyrics) {
        const auto question = std::wstring(L"Replace the current ") + (timed ? L"Timed" : L"Untimed") +
                              L" text in the editor with the lyrics found online?";
        if (MessageBoxW(w, question.c_str(), L"Lyrics found", MB_YESNO | MB_ICONQUESTION) != IDYES) {
            SetDlgItemTextW(w, kStatus, L"Found lyrics were not used.");
            return;
        }
    }
    target = lyrics;
    SendDlgItemMessageW(w, kMode, CB_SETCURSEL, timed ? 0 : 1, 0);
    Show(w, s);
    std::wstring text = L"Found on " + Field(*result, L"source") + L"  -  " +
                        (timed ? L"Timed (synchronized) lyrics, " : L"Untimed (plain) lyrics, ") +
                        std::to_wstring(CountLines(lyrics)) + L" lines.";
    const auto matched = Field(*result, L"matched");
    if (!matched.empty()) text += L"\r\nMatched: " + matched;
    if (!query.empty()) text += L"\r\nLooked for: " + query;
    text += L"\r\nLoaded into the editor below. Check it, then Save to MP3 tag.";
    SetDlgItemTextW(w, kStatus, text.c_str());
}

void ApplyFont(HWND w, HFONT font) {
    EnumChildWindows(w, [](HWND child, LPARAM f) {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

void Create(HWND w, State& s) {
    const auto song = L"Song: " + (s.value.songLabel.empty() ? std::wstring(L"(unknown song)") : s.value.songLabel);
    Add(w, L"STATIC", song.c_str(), SS_ENDELLIPSIS, 16, 12, 720, 20);
    Add(w, L"STATIC", (L"File: " + s.value.audioPath.wstring()).c_str(), SS_PATHELLIPSIS, 16, 34, 720, 20);
    Add(w, L"STATIC", L"", 0, 16, 58, 720, 20, kCurrent);

    Add(w, L"BUTTON", L"Find online", BS_GROUPBOX, 12, 86, 728, 112);
    Add(w, L"BUTTON", L"Search TIDAL / LRCLIB", BS_PUSHBUTTON | WS_TABSTOP, 26, 110, 180, 30, kSearch);
    Add(w, L"STATIC", L"Uses the song's ISRC, artist, title and length. Found lyrics go into "
                      L"the editor; nothing is saved until you press Save.",
        0, 220, 108, 506, 80, kStatus);

    Add(w, L"BUTTON", L"Lyrics", BS_GROUPBOX, 12, 206, 728, 404);
    Add(w, L"STATIC", L"Type", 0, 26, 234, 50, 20);
    HWND mode = Add(w, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 80, 230, 200, 110, kMode);
    SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Timed (LRC timestamps)"));
    SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Untimed"));
    SendMessageW(mode, CB_SETCURSEL, s.value.synchronized ? 0 : 1, 0);
    Add(w, L"STATIC", L"Timed rows use [mm:ss.xx] text. Each type keeps its own text; paste or type here.",
        0, 296, 234, 436, 20);
    Add(w, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        26, 262, 700, 336, kText);
    SendDlgItemMessageW(w, kText, EM_SETLIMITTEXT, 1024 * 1024, 0);

    // Timing fix: move every timestamp so the first line starts where the vocals really start.
    Add(w, L"STATIC", L"First line at", 0, 16, 629, 76, 20);
    const auto startAt = s.value.positionSeconds >= 0
        ? FormatLyricsTime(static_cast<std::int64_t>(s.value.positionSeconds * 1000.0)) : std::wstring{};
    Add(w, L"EDIT", startAt.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 94, 625, 80, 24, kStartAt);
    Add(w, L"BUTTON", L"Shift timing", BS_PUSHBUTTON | WS_TABSTOP, 180, 622, 110, 30, kShift);
    Add(w, L"BUTTON", L"Save to MP3 tag", BS_DEFPUSHBUTTON | WS_TABSTOP, 500, 622, 140, 30, IDOK);
    Add(w, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 648, 622, 92, 30, IDCANCEL);

    ShowCurrentState(w, s);
    Show(w, s);
    if (s.font) ApplyFont(w, s.font);
}

LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM a, LPARAM b) {
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    switch (m) {
    case WM_CREATE: Create(w, *s); return 0;
    case kLookupDone:
        if (s) FinishSearch(w, *s, std::unique_ptr<LookupResult>(reinterpret_cast<LookupResult*>(b)));
        else delete reinterpret_cast<LookupResult*>(b);
        return 0;
    case WM_COMMAND:
        if (LOWORD(a) == kMode && HIWORD(a) == CBN_SELCHANGE) {
            Store(w, *s, !TimedSelected(w));
            Show(w, *s);
            return 0;
        }
        if (LOWORD(a) == kSearch) { StartSearch(w, *s); return 0; }
        if (LOWORD(a) == kShift) {
            wchar_t value[64]{};
            GetDlgItemTextW(w, kStartAt, value, 64);
            const auto target = ParseLyricsTime(value);
            if (target < 0) {
                MessageBoxW(w, L"Enter the time where the first line starts, e.g. 0:23.40 (the current "
                               L"playback position is filled in when the window opens).", L"Shift timing",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            if (!TimedSelected(w)) {
                SendDlgItemMessageW(w, kMode, CB_SETCURSEL, 0, 0);
                Store(w, *s, false);
                Show(w, *s);
            }
            auto text = EditorText(w);
            std::int64_t delta = 0;
            if (!ShiftLrcTimestamps(text, target, &delta)) {
                MessageBoxW(w, L"The Timed text has no [mm:ss.xx] timestamps to shift.", L"Shift timing",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            SetDlgItemTextW(w, kText, text.c_str());
            Store(w, *s, true);
            wchar_t status[160]{};
            std::swprintf(status, std::size(status),
                          L"All timestamps moved by %+.2f s; first line now at %ls. Save to MP3 tag to keep it.",
                          static_cast<double>(delta) / 1000.0, FormatLyricsTime(target).c_str());
            SetDlgItemTextW(w, kStatus, status);
            return 0;
        }
        if (LOWORD(a) == IDOK) {
            Store(w, *s, TimedSelected(w));
            s->value.synchronized = TimedSelected(w);
            s->accepted = s->done = true;
            DestroyWindow(w);
            return 0;
        }
        if (LOWORD(a) == IDCANCEL) { s->done = true; DestroyWindow(w); return 0; }
        break;
    case WM_CLOSE:
        if (s) s->done = true;
        DestroyWindow(w);
        return 0;
    }
    return DefWindowProcW(w, m, a, b);
}
}  // namespace

bool ShowEmbeddedLyricsEditor(HWND o, EmbeddedLyricsEdit& e) {
    WNDCLASSEXW c{sizeof(c)};
    c.lpfnWndProc = Proc;
    c.hInstance = GetModuleHandleW(nullptr);
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    c.lpszClassName = kClass;
    RegisterClassExW(&c);

    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    HFONT font = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)
        ? CreateFontIndirectW(&metrics.lfMessageFont) : nullptr;
    State s{e, false, false, false, o, font};

    // The dialog runs on its own background thread (see Plugin.cpp), so its input queue is
    // not attached to the owner window's thread by default. Without AttachThreadInput, Windows
    // can fail to hand activation back to the owner when this popup is destroyed and instead
    // minimizes it. Attaching for the lifetime of the dialog makes activation behave as if this
    // ran on the owner's own thread.
    const DWORD ownerThreadId = o ? GetWindowThreadProcessId(o, nullptr) : 0;
    const DWORD thisThreadId = GetCurrentThreadId();
    const bool attached = ownerThreadId && ownerThreadId != thisThreadId &&
        AttachThreadInput(thisThreadId, ownerThreadId, TRUE);
    const auto title = e.songLabel.empty() ? std::wstring(L"LRC lyrics")
                                           : L"LRC lyrics - " + e.songLabel;
    RECT frame{0, 0, 752, 666};
    AdjustWindowRectEx(&frame, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME);
    HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(), WS_CAPTION | WS_SYSMENU | WS_POPUP,
                             CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top,
                             o, nullptr, GetModuleHandleW(nullptr), &s);
    if (!w) {
        if (attached) AttachThreadInput(thisThreadId, ownerThreadId, FALSE);
        if (font) DeleteObject(font);
        return false;
    }
    if (o) EnableWindow(o, FALSE);
    ShowWindow(w, SW_SHOW);
    SetForegroundWindow(w);
    MSG m{};
    while (!s.done && GetMessageW(&m, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(w, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (o) { EnableWindow(o, TRUE); SetActiveWindow(o); SetForegroundWindow(o); }
    if (attached) AttachThreadInput(thisThreadId, ownerThreadId, FALSE);
    if (font) DeleteObject(font);
    // A search that finished after the window closed was never delivered; free its result.
    while (PeekMessageW(&m, nullptr, kLookupDone, kLookupDone, PM_REMOVE))
        delete reinterpret_cast<LookupResult*>(m.lParam);
    if (s.accepted) e = std::move(s.value);
    return s.accepted;
}
#endif
