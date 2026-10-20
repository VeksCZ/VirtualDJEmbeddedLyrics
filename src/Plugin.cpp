#ifdef _WIN32
#include "Lyrics.hpp"
#include "AdvancedDialog.hpp"
#include "EmbeddedLyricsEditor.hpp"
#include "LyricsTiming.hpp"
#include "LyricsLayout.hpp"
#include "AsyncLyricsLoader.hpp"
#include "Diagnostics.hpp"
#include "MasterDeckSelector.hpp"
#include "TextTexture.hpp"
#include "VideoRenderer.hpp"
#include "vdjVideo8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <shellapi.h>

#ifndef LRC_PLUGIN_VERSION
#define LRC_PLUGIN_VERSION "0.0.0-dev"
#endif

// LRC_DECK_VARIANT builds "LRC Deck": an ordinary (non-overlay, non-master-only) Video FX
// that VirtualDJ can activate in a single deck's video FX chain. It draws the deck's own
// picture first via DrawDeck() and then the lyrics on top, so the lyrics live inside that
// deck's video and follow it through the video crossfader instead of sitting on the master.
#ifdef LRC_DECK_VARIANT
#define LRC_PLUGIN_NAME "LRC Deck"
#define LRC_PLUGIN_WNAME L"LRC Deck"
#else
#define LRC_PLUGIN_NAME "LRC Master"
#define LRC_PLUGIN_WNAME L"LRC Master"
#endif

namespace {
int moduleAnchor;
struct PaletteEntry { const wchar_t* name; std::uint32_t color; };
constexpr PaletteEntry kPalette[] = {
    {L"White", 0x00ffffffu}, {L"Yellow", 0x0000d2ffu}, {L"Gray", 0x00969696u},
    {L"Orange", 0x00248affu}, {L"Red", 0x004444f0u}, {L"Green", 0x006bd642u},
    {L"Cyan", 0x00e8d945u}, {L"Blue", 0x00ff834bu}, {L"Magenta", 0x00e851d9u}
};
constexpr PaletteEntry kBackgroundPalette[] = {
    {L"Black", 0x00000000u}, {L"White", 0x00ffffffu}, {L"Gray", 0x00969696u},
    {L"Red", 0x004444f0u}, {L"Green", 0x006bd642u}, {L"Blue", 0x00ff834bu},
    {L"Yellow", 0x0000d2ffu}, {L"Orange", 0x00248affu}, {L"Magenta", 0x00e851d9u}
};
std::size_t DiscreteIndex(float value, std::size_t count) {
    return static_cast<std::size_t>(std::clamp(value, 0.0f, 1.0f) *
                                    static_cast<float>(count - 1) + 0.5f);
}
std::size_t PaletteIndex(float value) { return DiscreteIndex(value, std::size(kPalette)); }
constexpr const wchar_t* kFontNames[] = {L"Arial", L"Segoe UI", L"Verdana", L"Tahoma", L"Trebuchet", L"Calibri"};
constexpr const wchar_t* kBackdropNames[] = {L"Outline", L"Shadow", L"Outline + Shadow"};
constexpr const wchar_t* kStrengthNames[] = {L"Thin", L"Normal", L"Strong"};
std::filesystem::path PluginDirectory() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleAnchor), &module);
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())));
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path AdvancedSettingsPath() {
    std::wstring localAppData(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(),
                                                  static_cast<DWORD>(localAppData.size()));
    if (!length || length >= localAppData.size()) return PluginDirectory() / L"LRC Advanced.ini";
    localAppData.resize(length);
    return std::filesystem::path(localAppData) / L"VirtualDJ" / L"LRC Advanced.ini";
}
std::wstring WideFromUtf8(const char* value) {
    if (!value || !*value) return {};
    const int length = static_cast<int>(std::strlen(value));
    const int size = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, length, result.data(), size);
    return result;
}
}

class EmbeddedLyricsPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnLoad() override {
        if (FAILED(DeclareParameterButton(&editTextButton_, 6, "Edit embedded lyrics", "Edit lyrics")) ||
            FAILED(DeclareParameterButton(&editBrowsedButton_, 11,
                "Edit lyrics for the song selected in the browser", "Edit browsed")) ||
            FAILED(DeclareParameterButton(&nextLineButton_, 7, "Next line", "Next line")) ||
            FAILED(DeclareParameterButton(&previousLineButton_, 8, "Previous line", "Previous")) ||
            FAILED(DeclareParameterButton(&advancedButton_, 10, "Advanced", "Advanced")) ||
            FAILED(DeclareParameterSwitch(&wholeLineHighlightParameter_, 12,
                "Highlight whole active line", "Whole line", false)) ||
            FAILED(DeclareParameterSlider(&countdownGapParameter_, 13,
                "Countdown gap (3-10 seconds)", "Countdown gap", 2.0f / 7.0f)) ||
            FAILED(DeclareParameterSlider(&timingOffsetParameter_, 14,
                "Lyrics timing offset (-5 to +5 seconds)", "Timing offset", 0.5f)) ||
            FAILED(DeclareParameterSwitch(&showHeadingParameter_, 15,
                "Show track name and artist", "Show name", true))) return E_FAIL;
        LoadAdvancedSettings();
        Diagnostics::Info(L"Embedded Lyrics loaded");
        return S_OK;
    }
    HRESULT VDJ_API OnParameter(int id) override {
        if (id == 7 && nextLineButton_) { AdvanceUntimedLine(); nextLineButton_ = 0; }
        else if (id == 8 && previousLineButton_) {
            if (!lyrics_.synchronized && activeLine_ > 0) BeginUntimedScroll(activeLine_ - 1);
            previousLineButton_ = 0;
        } else if (id == 9) {
            recordedTimes_.assign(lyrics_.lines.size(), -1);
            recordingNextLine_ = 0;
            if (recordTimingParameter_ && !lyrics_.synchronized) activeLine_ = 0;
        } else if (id == 6 && editTextButton_) { double length = 0.0; char lengthQuery[64]{}; std::snprintf(lengthQuery, sizeof(lengthQuery), "deck %d get_songlength", currentDeck_ > 0 ? currentDeck_ : 1); if (FAILED(GetInfo(lengthQuery, &length))) length = 0.0; std::thread([this, length] { OpenEmbeddedLyricsEditor(length); }).detach(); editTextButton_ = 0; }
        else if (id == 11 && editBrowsedButton_) { char browserPath[4096]{}; GetStringInfo("get_browsed_filepath", browserPath, sizeof(browserPath)); const std::filesystem::path selected{std::u8string(reinterpret_cast<const char8_t*>(browserPath))}; Diagnostics::Info(L"Edit browsed lyrics: raw filepath=[" + std::wstring(WideFromUtf8(browserPath)) + L"] parsed path=[" + selected.wstring() + L"]"); std::thread([this, selected] { OpenBrowsedLyricsEditor(selected); }).detach(); editBrowsedButton_ = 0; }
        else if (id == 10 && advancedButton_) { OpenAdvancedDialog(); advancedButton_ = 0; }
        else if ((id == 12 || id == 13 || id == 15) && settingsSynced_) {
            // Shared settings: persist and tell the other instances. Ignored until the first
            // frame so VirtualDJ restoring this plugin's own saved values on load can't
            // overwrite what the other plugin saved.
            std::lock_guard<std::recursive_mutex> stateLock(stateMutex_);
            SaveAdvancedSettings();
            PublishSettingsChange();
            texture_.Reset();
            headingTexture_.Reset();
        }
        return S_OK;
    }
    HRESULT VDJ_API OnGetParameterString(int id, char* output, int outputSize) override {
        if (!output || outputSize <= 0) return E_NOTIMPL;

        int percent = 0;
        if (id == 1) percent = static_cast<int>(FontScale() * 100.0f + 0.5f);
        else if (id == 4) percent = static_cast<int>(VerticalPosition() * 100.0f + 0.5f);
        else if (id == 3) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%zu", PageSize());
            return S_OK;
        } else if (id == 2) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%zu", TimedLineCount());
            return S_OK;
        } else if (id == 13) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%d s", CountdownGapSeconds());
            return S_OK;
        } else if (id == 14) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%+d ms", TimingOffsetMs());
            return S_OK;
        } else return E_NOTIMPL;
        std::snprintf(output, static_cast<std::size_t>(outputSize), "%d%%", percent);
        return S_OK;
    }
    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
        info->PluginName = LRC_PLUGIN_NAME;
        info->Author = "Slava / OpenAI";
        info->Description = "Timed embedded/LRC lyrics and manual untimed lyrics pages";
        info->Version = LRC_PLUGIN_VERSION;
#ifdef LRC_DECK_VARIANT
        // No MASTERONLY/OVERLAY: VirtualDJ may then host the effect in a deck's own FX
        // chain. OUTPUTRESOLUTION renders at the output size instead of the source video's
        // (otherwise text on a low-res clip or an audio-only deck would be blurry).
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_OUTPUTRESOLUTION;
#else
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_MASTERONLY |
                      VDJFLAG_VIDEO_OVERLAY;
#endif
        info->Bitmap = nullptr;
        return S_OK;
    }
    ULONG VDJ_API Release() override { delete this; return 0; }
    HRESULT VDJ_API OnDeviceInit() override {
        if (FAILED(GetDevice(VdjVideoEngineDirectX11, reinterpret_cast<void**>(&device_))) || !device_) {
            Diagnostics::Error(L"VirtualDJ did not provide a DirectX 11 device");
            return E_FAIL;
        }
        if (!texture_.Initialize(device_) || !headingTexture_.Initialize(device_) ||
            !renderer_.Initialize(device_)) {
            Diagnostics::Error(L"DirectX 11 lyrics renderer initialization failed");
            return E_FAIL;
        }
        Diagnostics::Info(L"DirectX 11 lyrics renderer initialized");
        return S_OK;
    }
    HRESULT VDJ_API OnDeviceClose() override {
        renderer_.Reset(); texture_.Reset(); headingTexture_.Reset(); device_ = nullptr;
        drawContextLogged_ = false;
        return S_OK;
    }
    HRESULT VDJ_API OnDraw() override {
        // The lyrics/text-editor dialogs run on their own background thread and read/write
        // loadedPath_, lyrics_, texture_, activeLine_ etc. directly. Without this lock, the
        // render thread and the editor thread touch that state concurrently, which is what
        // was causing playback to stutter/freeze while an "Edit lyrics" dialog was open.
        std::lock_guard<std::recursive_mutex> stateLock(stateMutex_);
#ifdef LRC_DECK_VARIANT
        // Without VDJFLAG_VIDEO_OVERLAY nothing is drawn for us: put the deck's own picture
        // down first, on every frame and before any early return, or the deck goes black.
        const HRESULT deckDrawn = DrawDeck();
        SyncSharedSettings();
        const auto deck = OwnDeck();
#else
        SyncSharedSettings();
        const auto deck = VisibleVideoDeck();
#endif
        TryCommitPendingRecording();
        SuppressVirtualDjLyrics();
        currentDeck_ = deck;
        if (!drawContextLogged_) {
            Diagnostics::Info(std::wstring(LRC_PLUGIN_WNAME) + L" draw context: deck=" +
                              std::to_wstring(deck) + L", size=" + std::to_wstring(width) +
                              L"x" + std::to_wstring(height));
#ifdef LRC_DECK_VARIANT
            LogDeckContext(deckDrawn);
#endif
            drawContextLogged_ = true;
        }
        if (deck <= 0) {
            return S_OK;
        }
        char pathBuffer[4096]{};
        char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
        if (FAILED(GetStringInfo(command, pathBuffer, sizeof(pathBuffer)))) {
            Diagnostics::Error(L"VirtualDJ get_filepath query failed");
            return S_OK;
        }
        const auto* utf8Path = reinterpret_cast<const char8_t*>(pathBuffer);
        const std::filesystem::path path{std::u8string{utf8Path}};
        if (path.empty()) {
            loadedPath_.clear();
            lyrics_ = {};
            texture_.Reset();
            return S_OK;
        }
        if (!path.empty() && path != loadedPath_) {
            loadedPath_ = path;
            LoadTrackHeading(deck);
            texture_.Reset();
            lyrics_ = {};
            activeLine_ = 0;
            untimedScrollActive_ = false;
            recordTimingParameter_ = 0;
            timingOffsetParameter_ = 0.5f;
            recordedTimes_.clear();
            recordingNextLine_ = 0;
            loader_.Request(path);
            Diagnostics::Info(L"Queued asynchronous lyrics load: " + path.wstring());
        }
        if (auto completed = loader_.Poll(); completed && completed->path == loadedPath_) {
            lyrics_ = std::move(completed->result.document);
            if (lyrics_.empty()) {
                Diagnostics::Error(L"No lyrics found for: " + loadedPath_.wstring() + L"; " + completed->result.error);
            } else {
                Diagnostics::Info(L"Lyrics loaded: " + loadedPath_.wstring());
                if (autoTagLrcParameter_) EnsureLyricsHashtag(deck, lyrics_.synchronized);
            }
        }
        if (lyrics_.empty()) {
            if (!texture_.UpdateMessage(L"...", width, height, FontScale(), VerticalPosition(),
                                        backgroundParameter_ != 0, BackgroundColor()) ||
                !DrawLyricsTexture()) {
                Diagnostics::Error(L"Failed to render missing-lyrics indication");
            }
            DrawHeadingBanner();
            return S_OK;
        }
        if (!lyrics_.synchronized) {
            if (!UpdateUntimedRibbon() || !DrawLyricsTexture())
                Diagnostics::Error(L"Failed to render untimed lyrics ribbon");
            DrawHeadingBanner();
            return S_OK;
        }
        double elapsedMs = 0.0;
        std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", deck);
        if (FAILED(GetInfo(command, &elapsedMs))) {
            Diagnostics::Error(L"VirtualDJ elapsed-time query failed");
            return S_OK;
        }
        UpdateVisible(AdjustLyricsTime(static_cast<std::int64_t>(elapsedMs), TimingOffsetMs()));
        if (!DrawLyricsTexture()) Diagnostics::Error(L"Failed to render synchronized lyrics");
        DrawHeadingBanner();
        return S_OK;
    }

private:
    bool DrawLyricsTexture() {
        return renderer_.Draw(texture_.View());
    }

    // The artist/title banner is a second, independent overlay drawn on top of the scrolling
    // lyrics texture instead of being woven into its timeline: it doesn't participate in the
    // scroll/highlight/fade-mask machinery at all, so it can't destabilize that (already
    // fragile-enough) logic the way folding it into the lyrics timeline previously did.
    void DrawHeadingBanner() {
        if (!showHeadingParameter_ || trackHeading_.empty()) return;
        std::wstring text = trackHeading_.front();
        for (std::size_t i = 1; i < trackHeading_.size(); ++i) text += L" - " + trackHeading_[i];
        if (!headingTexture_.UpdateBanner(text, width, height, TextColor(), FontFamily(),
                                          BackdropStyle(), BackdropStrength()) ||
            !renderer_.Draw(headingTexture_.View()))
            Diagnostics::Error(L"Failed to render track name banner");
    }

    void EnsureLyricsHashtag(int deck, bool synchronized) {
        char command[256]{};
        char user1[4096]{};
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'user 1'", deck);
        if (FAILED(GetStringInfo(command, user1, sizeof(user1)))) {
            Diagnostics::Error(L"VirtualDJ User 1 query failed");
            return;
        }
        std::string current{user1};
        std::transform(current.begin(), current.end(), current.begin(), [](unsigned char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                               : static_cast<char>(value);
        });
        const char* wanted = synchronized ? "#sylt" : "# - uslt";
        for (const char* obsolete : {"#lrc", "#uslt", "#-uslt",
                                     synchronized ? "# - uslt" : "#sylt"}) {
            if (current.find(obsolete) == std::string::npos) continue;
            std::snprintf(command, sizeof(command),
                          "deck %d loaded_song_hashtag 'user 1' '%s'", deck, obsolete);
            SendCommand(command);
        }
        if (current.find(wanted) != std::string::npos) return;
        std::snprintf(command, sizeof(command),
                      "deck %d loaded_song_hashtag 'user 1' '%s'", deck, wanted);
        if (FAILED(SendCommand(command))) Diagnostics::Error(L"VirtualDJ failed to tag lyrics type");
        else Diagnostics::Info(synchronized ? L"Added #sylt to VirtualDJ User 1"
                                                     : L"Added # - uslt to VirtualDJ User 1");
    }

#ifdef LRC_DECK_VARIANT
    // Hosted in a deck's FX chain, unprefixed script queries are evaluated against that
    // deck, so "get_deck" names the deck this instance belongs to. Each deck gets its own
    // plugin instance, so the answer is stable; it's re-read every frame anyway in case
    // VirtualDJ moves the effect. Returns 0 when it can't tell (nothing is drawn then).
    int OwnDeck() {
        double deck = 0.0;
        if (SUCCEEDED(GetInfo("get_deck", &deck)) && deck >= 1.0 && deck <= 99.0)
            return static_cast<int>(deck);
        return 0;
    }

    // One-shot diagnostics so a first test in VirtualDJ tells us whether the deck context
    // actually works as assumed (the previous per-deck attempt was dropped without notes).
    void LogDeckContext(HRESULT deckDrawn) {
        double getDeck = -1.0;
        const HRESULT deckResult = GetInfo("get_deck", &getDeck);
        char ownPath[4096]{};
        const HRESULT pathResult = GetStringInfo("get_filepath", ownPath, sizeof(ownPath));
        Diagnostics::Info(L"Deck context: DrawDeck hr=" + std::to_wstring(deckDrawn) +
                          L", get_deck hr=" + std::to_wstring(deckResult) + L" value=" +
                          std::to_wstring(getDeck) + L", unprefixed get_filepath hr=" +
                          std::to_wstring(pathResult) + L" value=[" +
                          WideFromUtf8(ownPath) + L"]");
        for (int deck = 1; deck <= 4; ++deck) {
            char command[64]{}, value[4096]{};
            std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
            if (SUCCEEDED(GetStringInfo(command, value, sizeof(value))) && value[0])
                Diagnostics::Info(L"  deck " + std::to_wstring(deck) + L" file=[" +
                                  WideFromUtf8(value) + L"]");
        }
    }
#endif

    int VisibleVideoDeck() {
        double balance = 0.0, leftDeck = 1.0, rightDeck = 2.0;
        if (FAILED(GetInfo("get_leftdeck", &leftDeck))) leftDeck = 1.0;
        if (FAILED(GetInfo("get_rightdeck", &rightDeck))) rightDeck = 2.0;
        const char* balanceQuery = useVolumeFadersParameter_
            ? "get_crossfader_result" : "video_crossfader";
        if (FAILED(GetInfo(balanceQuery, &balance))) return masterDeckSelector_.Current();
        return masterDeckSelector_.Select(balance, static_cast<int>(leftDeck), static_cast<int>(rightDeck));
    }

    void UpdateVisible(std::int64_t now) {
        if (lyrics_.lines.empty()) return;

        struct DisplayLine {
            std::wstring text;
            std::int64_t timeMs{};
            bool pause{};
        };
        constexpr std::int64_t scrollDuration = 650;
        std::vector<DisplayLine> timeline;
        timeline.reserve(lyrics_.lines.size() * 2 + 1);
        const auto firstLyricTime = lyrics_.lines.front().timeMs;
        if (ShowLyricCountdown(firstLyricTime, CountdownGapSeconds())) {
            timeline.push_back({std::to_wstring(LyricCountdown(firstLyricTime)), 0, true});
        }
        for (std::size_t i = 0; i < lyrics_.lines.size(); ++i) {
            const auto& lyric = lyrics_.lines[i];
            timeline.push_back({lyric.text, lyric.timeMs, false});
            if (i + 1 >= lyrics_.lines.size()) continue;
            const auto nextTime = lyrics_.lines[i + 1].timeMs;
            const auto highlight = std::min(EstimateLyricHighlightMs(lyric.text),
                std::max<std::int64_t>(500, nextTime - lyric.timeMs - scrollDuration));
            const auto pauseStart = lyric.timeMs + highlight + 700;
            const auto pauseDuration = nextTime - pauseStart;
            if (ShowLyricCountdown(pauseDuration, CountdownGapSeconds()))
                timeline.push_back({std::to_wstring(LyricCountdown(pauseDuration)), pauseStart, true});
        }

        const auto it = std::upper_bound(timeline.begin(), timeline.end(), now,
            [](std::int64_t value, const DisplayLine& line) { return value < line.timeMs; });
        const auto active = it == timeline.begin() ? 0u
            : static_cast<std::size_t>(std::distance(timeline.begin(), it) - 1);
        const auto start = timeline[active].timeMs;
        const auto next = active + 1 < timeline.size() ? timeline[active + 1].timeMs : start + 5000;
        const auto interval = std::max<std::int64_t>(1, next - start);

        // Hand TextTexture the whole timeline as candidates around "active", not a pre-shrunk
        // window: the anchor and the fade band are computed there purely from TimedLineCount(),
        // never from which candidates end up on screen or how they wrap, so neither can jump
        // between frames just because a line happens to wrap to two rows. TextTexture works out
        // for itself how many candidates it actually needs to fill the frame.
        std::vector<std::wstring> visibleLines;
        std::vector<bool> subduedLines;
        visibleLines.reserve(timeline.size());
        subduedLines.reserve(timeline.size());
        bool countdownVisible = false;
        for (std::size_t i = 0; i < timeline.size(); ++i) {
            if (timeline[i].pause) {
                const int position = i < active ? -1 : i > active ? 1 : 0;
                visibleLines.push_back(
                    LyricPauseDisplayText(timeline[i].text, position, next - now));
                if (i == active) countdownVisible = !visibleLines.back().empty();
            } else {
                visibleLines.push_back(timeline[i].text);
            }
            subduedLines.push_back(timeline[i].pause && i != active);
        }

        float highlightProgress = countdownVisible ? 1.0f : 0.0f;
        if (!timeline[active].pause) {
            const auto highlightDuration = std::min(EstimateLyricHighlightMs(timeline[active].text),
                std::max<std::int64_t>(500, interval - scrollDuration));
            highlightProgress = LyricHighlightProgress(
                now, start, highlightDuration, wholeLineHighlightParameter_ != 0);
        }
        const auto scrollProgress = timeline[active].pause
            ? UnitProgress(now, next - scrollDuration, scrollDuration)
            : UnitProgress(now, start, interval);
        if (!texture_.UpdateTimed(visibleLines, active, highlightProgress, scrollProgress,
                                  width, height, FontScale(), VerticalPosition(), subduedLines,
                                  TextColor(), HighlightColor(), ReadColor(),
                                  FontFamily(), BackdropStyle(), BackdropStrength(),
                                  backgroundParameter_ != 0, BackgroundColor(), TimedLineCount()))
            Diagnostics::Error(L"Failed to update lyrics texture");
    }


    float FontScale() const noexcept {
        return 0.5f + std::clamp(fontSizeParameter_, 0.0f, 1.0f) * 1.5f;
    }

    float VerticalPosition() const noexcept {
        return 0.1f + std::clamp(verticalPositionParameter_, 0.0f, 1.0f) * 0.8f;
    }

    std::size_t PageSize() const noexcept {
        return static_cast<std::size_t>(untimedLines_);
    }
    std::size_t TimedLineCount() const noexcept {
        return static_cast<std::size_t>(timedLines_);
    }
    int CountdownGapSeconds() const noexcept {
        return 3 + static_cast<int>(std::clamp(countdownGapParameter_, 0.0f, 1.0f) * 7.0f + 0.5f);
    }
    int TimingOffsetMs() const noexcept {
        const int raw = static_cast<int>((std::clamp(timingOffsetParameter_, 0.0f, 1.0f) - 0.5f) * 10000.0f);
        return static_cast<int>(std::round(raw / 10.0)) * 10;
    }
    std::uint32_t TextColor() const noexcept { return kPalette[PaletteIndex(textColorParameter_)].color; }
    std::uint32_t HighlightColor() const noexcept { return kPalette[PaletteIndex(highlightColorParameter_)].color; }
    std::uint32_t ReadColor() const noexcept { return kPalette[PaletteIndex(readColorParameter_)].color; }
    std::uint32_t BackgroundColor() const noexcept {
        return kBackgroundPalette[DiscreteIndex(backgroundColorParameter_,
                                                 std::size(kBackgroundPalette))].color;
    }
    int FontFamily() const noexcept {
        return static_cast<int>(DiscreteIndex(fontFamilyParameter_, std::size(kFontNames)));
    }
    int BackdropStyle() const noexcept {
        return static_cast<int>(DiscreteIndex(backdropStyleParameter_, std::size(kBackdropNames)));
    }
    int BackdropStrength() const noexcept {
        return static_cast<int>(DiscreteIndex(backdropStrengthParameter_, std::size(kStrengthNames)));
    }

    // Pad "VDJ lyrics off" sets $lrcnovdjlyrics: VirtualDJ's own lyrics video effect switches
    // itself on for songs with lyrics (e.g. a same-name .lrc), so keep it off while the
    // variable is set. Checked once a second; costs a few GetInfo calls.
    void SuppressVirtualDjLyrics() {
        const auto now = std::chrono::steady_clock::now();
        if (now < nextLyricsSuppressCheck_) return;
        nextLyricsSuppressCheck_ = now + std::chrono::seconds(1);
        double off = 0.0;
        if (GetInfo("get_var '$lrcnovdjlyrics'", &off) != S_OK || off < 0.5) return;
        double decks = 2.0;
        if (GetInfo("get_decks", &decks) != S_OK || decks < 1.0) decks = 2.0;
        auto suppress = [this](const std::string& target) {
            for (const char* effect : {"lyrics", "karaoke"}) {
                const auto query = target + " effect_active '" + effect + "'";
                double active = 0.0;
                if (GetInfo(query.c_str(), &active) == S_OK && active >= 0.5)
                    SendCommand((query + " off").c_str());
            }
        };
        for (int deck = 1; deck <= static_cast<int>(decks) && deck <= 4; ++deck)
            suppress("deck " + std::to_string(deck));
        suppress("deck master");
    }

    void BeginUntimedScroll(std::size_t target) {
        if (lyrics_.lines.empty()) return;
        target = std::min(target, lyrics_.lines.size() - 1);
        if (target == activeLine_) return;
        scrollFromLine_ = activeLine_; activeLine_ = target;
        untimedScrollStarted_ = std::chrono::steady_clock::now();
        untimedScrollActive_ = target > scrollFromLine_;
    }
    bool UpdateUntimedRibbon() {
        if (lyrics_.lines.empty()) return false;
        auto renderActive = activeLine_; float scroll = 0.0f;
        if (untimedScrollActive_) {
            scroll = std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() - untimedScrollStarted_).count() / 0.45f, 0.0f, 1.0f);
            if (scroll < 1.0f) renderActive = scrollFromLine_;
            else { untimedScrollActive_ = false; scroll = 0.0f; }
        }
        // See the matching comment in UpdateVisible: hand over every line as a candidate and let
        // TextTexture size the anchor/fade band from PageSize() alone, then work out how many
        // candidates it actually needs.
        std::vector<std::wstring> visible; visible.reserve(lyrics_.lines.size());
        for (const auto& line : lyrics_.lines) visible.push_back(line.text);
        return texture_.UpdateTimed(visible, renderActive, 1.0f, scroll,
                                    width, height, FontScale(), VerticalPosition(), {},
                                    TextColor(), HighlightColor(), ReadColor(),
                                    FontFamily(), BackdropStyle(), BackdropStrength(),
                                    backgroundParameter_ != 0, BackgroundColor(), PageSize());
    }
    void AdvanceUntimedLine() {
        if (lyrics_.synchronized || lyrics_.lines.empty()) return;
        if (!recordTimingParameter_) {
            if (activeLine_ + 1 < lyrics_.lines.size()) BeginUntimedScroll(activeLine_ + 1);
            return;
        }
        if (recordingNextLine_ >= lyrics_.lines.size() || currentDeck_ <= 0) return;
        double elapsed = 0.0; char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", currentDeck_);
        if (FAILED(GetInfo(command, &elapsed))) return;
        const auto target = recordingNextLine_++;
        if (target != activeLine_) BeginUntimedScroll(target);
        recordedTimes_[target] = static_cast<std::int64_t>(elapsed);
        if (recordingNextLine_ == lyrics_.lines.size()) QueueTimingRecording();
    }

    void LoadTrackHeading(int deck) {
        char command[128]{}, value[4096]{};
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'artist'", deck);
        const auto artist = SUCCEEDED(GetStringInfo(command, value, sizeof(value)))
            ? WideFromUtf8(value) : std::wstring{};
        value[0] = '\0';
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'title'", deck);
        const auto title = SUCCEEDED(GetStringInfo(command, value, sizeof(value)))
            ? WideFromUtf8(value) : std::wstring{};
        trackHeading_.clear();
        if (!artist.empty()) trackHeading_.push_back(artist);
        if (!title.empty()) trackHeading_.push_back(title);
    }
    static std::string Utf8(const std::wstring& value) {
        if (value.empty()) return {};
        const auto size = WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        std::string result(static_cast<std::size_t>(size),'\0');
        WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);
        return result;
    }
    std::wstring LyricsText(const LyricsDocument& document, bool synchronized) const {
        std::wstring result;
        for (const auto& line : document.lines) {
            if (!result.empty()) result += L"\r\n";
            if (synchronized) {
                const auto total = std::max<std::int64_t>(0, line.timeMs);
                const auto minutes = total / 60000;
                const auto seconds = (total / 1000) % 60;
                const auto hundredths = (total % 1000) / 10;
                wchar_t timestamp[32]{};
                std::swprintf(timestamp, std::size(timestamp), L"[%02lld:%02lld.%02lld] ",
                              static_cast<long long>(minutes), static_cast<long long>(seconds),
                              static_cast<long long>(hundredths));
                result += timestamp;
            }
            result += line.text;
        }
        return result;
    }

    void OpenEmbeddedLyricsEditor(double durationSeconds) {
        // Snapshot the state the dialog needs, then release the lock before showing it: the
        // dialog runs a modal message loop on this (background) thread and can stay open for
        // a while, so we must not hold stateMutex_ across it or OnDraw on the render thread
        // would block/stutter for as long as the editor window is open.
        std::filesystem::path currentPath;
        bool synchronized{};
        {
            std::lock_guard<std::recursive_mutex> stateLock(stateMutex_);
            if (loadedPath_.empty()) {
                Diagnostics::Error(L"Embedded lyrics can be edited only for a loaded MP3");
                return;
            }
            currentPath = loadedPath_;
            synchronized = lyrics_.synchronized;
        }
        EditEmbeddedLyrics(currentPath, synchronized, /*applyLiveDisplay=*/true, durationSeconds);
    }
    void OpenBrowsedLyricsEditor(const std::filesystem::path& browsed) {
        // get_browsed_filepath returns whatever the browser currently considers "browsed"; if
        // that's a folder/tree node rather than an actual song row, it returns a folder path
        // instead of a file, which used to be silently accepted and fail obscurely much later
        // when writing the tag. Keep this guard even though get_browsed_filepath (vs. the
        // get_browsed_song 'filepath' column this used to call) fixed the common case of it
        // firing right after startup or on a genuine folder/playlist selection.
        auto extension = browsed.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (browsed.empty() || extension != L".mp3") {
            Diagnostics::Error(L"Select an MP3 in the VirtualDJ browser first");
            // This used to only log, which looked like the button did nothing: clicking it
            // with a folder/playlist focused in the browser (rather than an actual song row)
            // is a common way to hit this, so tell the user instead of failing silently.
            MessageBoxW(GetForegroundWindow(), L"Select an MP3 song row in the VirtualDJ browser first.",
                       LRC_PLUGIN_WNAME, MB_OK | MB_ICONWARNING);
            return;
        }
        // Never touch loadedPath_/lyrics_/currentDeck_/texture_ here: those belong to the
        // render thread's OnDraw(), which keeps running concurrently on this background
        // thread's whole editing session and would immediately stomp any temporary override
        // back to the actually-playing track. Default to whichever type this file already has.
        const auto timedPreview = LoadEmbeddedTimedLyrics(browsed);
        EditEmbeddedLyrics(browsed, !timedPreview.document.empty(), /*applyLiveDisplay=*/false);
    }
    void EditEmbeddedLyrics(const std::filesystem::path& targetPath, bool synchronized, bool applyLiveDisplay,
                            double durationSeconds = 0.0) {
        Diagnostics::Info(L"EditEmbeddedLyrics targetPath=[" + targetPath.wstring() + L"]");
        auto timed = LoadEmbeddedTimedLyrics(targetPath);
        auto untimed = LoadEmbeddedUntimedLyrics(targetPath);
        EmbeddedLyricsEdit edit{LyricsText(timed.document, true), LyricsText(untimed.document, false), synchronized,
                                targetPath.stem().wstring()};
        edit.audioPath = targetPath;
        edit.durationSeconds = durationSeconds;
        if (const auto helper = HelperExecutable(); !helper.empty()) {
            edit.lookupExecutable = helper.wstring();
        } else {
            // Older installs: LyricsTools registers itself here (install/update or start).
            const auto ini = AdvancedSettingsPath().wstring();
            wchar_t value[2048]{};
            GetPrivateProfileStringW(L"Lookup", L"Executable", L"", value, 2048, ini.c_str());
            edit.lookupExecutable = value;
            GetPrivateProfileStringW(L"Lookup", L"Script", L"", value, 2048, ini.c_str());
            edit.lookupScript = value;
        }
        if (!ShowEmbeddedLyricsEditor(GetForegroundWindow(), edit)) return;
        const auto& editedText = edit.synchronized ? edit.timedText : edit.untimedText;
        if (editedText.empty()) return;
        std::error_code error;
        const auto pendingTextPath = std::filesystem::temp_directory_path(error) /
            (L"EmbeddedLyrics-" + std::to_wstring(std::hash<std::wstring>{}(targetPath.wstring())) + L".lyrics");
        std::ofstream output(pendingTextPath, std::ios::binary | std::ios::trunc);
        output << Utf8(editedText);
        if (!output) {
            Diagnostics::Error(L"Cannot save pending embedded lyrics text");
            return;
        }
        output.close();
        const auto validation = LoadPlainTextLyrics(pendingTextPath);
        if (validation.document.empty()) {
            std::filesystem::remove(pendingTextPath, error);
            Diagnostics::Error(edit.synchronized
                ? L"Timed lyrics must contain LRC timestamps such as [01:23.45] Text"
                : L"Untimed lyrics must contain at least one non-empty line");
            return;
        }
        // LoadPlainTextLyrics classifies Timed vs. Untimed purely from the text (presence of
        // [mm:ss.xx] timestamps), independent of which tab was selected in the dialog. Trust
        // that detection instead of silently discarding a save whose content doesn't match the
        // tab the user happened to leave selected.
        if (validation.document.synchronized != edit.synchronized) {
            Diagnostics::Info(L"Saved text looks " +
                std::wstring(validation.document.synchronized ? L"Timed" : L"Untimed") +
                L" though the " + std::wstring(edit.synchronized ? L"Timed" : L"Untimed") +
                L" tab was selected; saving using the detected type");
            edit.synchronized = validation.document.synchronized;
        }
        // Deferring to OnDraw's TryCommitPendingRecording (checking VirtualDJ's own idea of
        // "loaded") used to be the only option here: GetStringInfo/GetInfo/SendCommand are only
        // safe to call from the thread VirtualDJ itself invokes the plugin on, and calling them
        // from this editor's own background thread previously broke saving entirely. But for the
        // common "Edit browsed" case the file was never loaded on a deck in the first place, and
        // making that wait for OnDraw to even run (which needs the video window open) made saves
        // look like they silently vanished. A plain exclusive-open probe needs no VDJ API call at
        // all, so it's safe to try right here: if nothing else holds the file open, write now
        // instead of waiting for a render frame that might not come for a while.
        const bool fileIsFree = [&targetPath] {
            const HANDLE probe = CreateFileW(targetPath.c_str(), GENERIC_READ, 0, nullptr,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (probe == INVALID_HANDLE_VALUE) return false;
            CloseHandle(probe);
            return true;
        }();
        {
            std::lock_guard<std::recursive_mutex> stateLock(stateMutex_);
            if (fileIsFree) {
                const std::wstring arguments = L"--write-editor-text \"" + targetPath.wstring() +
                    L"\" \"" + pendingTextPath.wstring() + L"\" " +
                    (edit.synchronized ? L"timed" : L"untimed");
                RunTagWriterScript(arguments, L"embedded lyrics");
            } else {
                pendingEditorTextPath_ = pendingTextPath;
                pendingEditorAudioPath_ = targetPath;
                pendingEditorSynchronized_ = edit.synchronized;
                pendingEditorWrite_ = true;
            }
            // Only refresh the on-screen lyrics if the track being edited is still the one
            // actually loaded; it may have changed on deck while the dialog was open.
            if (applyLiveDisplay && targetPath == loadedPath_) {
                lyrics_ = validation.document;
                activeLine_ = 0;
                untimedScrollActive_ = false;
                texture_.Reset();
                if (currentDeck_ > 0) EnsureLyricsHashtag(currentDeck_, edit.synchronized);
            }
        }
        Diagnostics::Info(fileIsFree ? L"Embedded lyrics updated; writing tag now"
                                     : L"Embedded lyrics updated; tag write waits for track unload");
    }
    void QueueTimingRecording() {
        auto extension = loadedPath_.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".mp3") {
            recordTimingParameter_ = 0;
            Diagnostics::Error(L"Recorded timing was discarded because only MP3 files support "
                               L"embedded SYLT/SYNCEDLYRICS writes: " + loadedPath_.wstring());
            return;
        }
        std::error_code error;
        pendingTimingPath_ = std::filesystem::temp_directory_path(error) /
            (L"EmbeddedLyrics-" + std::to_wstring(std::hash<std::wstring>{}(loadedPath_.wstring())) + L".timing");
        std::ofstream output(pendingTimingPath_, std::ios::binary | std::ios::trunc);
        for (std::size_t i=0;i<lyrics_.lines.size();++i) output << recordedTimes_[i] << '\t' << Utf8(lyrics_.lines[i].text) << '\n';
        output.close(); pendingAudioPath_ = loadedPath_; pendingTimingWrite_ = true; recordTimingParameter_ = 0;
        Diagnostics::Info(L"Timing recorded; waiting for track unload");
    }
    bool TrackLoadedAnywhere(const std::filesystem::path& path) {
        for (int deck=1; deck<=4; ++deck) { char command[64]{}, value[4096]{}; std::snprintf(command,sizeof(command),"deck %d get_filepath",deck);
            if (SUCCEEDED(GetStringInfo(command,value,sizeof(value))) && std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value))) == path) return true; }
        return false;
    }
    // LrcHelper.exe (installed by LyricsTools) bundles Python, mutagen and tidalapi, so tag
    // writes and online lookups need nothing else on the machine. LRC Deck lives in
    // VideoEffect and shares the single copy installed next to LRC Master.
    static std::filesystem::path HelperExecutable() {
        const auto own = PluginDirectory();
        for (const auto& directory : {own, own.parent_path() / L"VideoOverlay"}) {
            std::error_code error;
            const auto candidate = directory / L"LrcHelper.exe";
            if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        }
        return {};
    }

    // Launches the tag-writer script without blocking OnDraw (the process runs in the
    // background; PollTagWriterScripts picks up its exit code and any stdout/stderr on a later
    // frame). Waiting here would stall the render thread for however long python.exe takes to
    // start and run, which is exactly the stutter the rest of this file works hard to avoid.
    void RunTagWriterScript(const std::wstring& arguments, const wchar_t* label) {
        const auto helper = HelperExecutable();
        const auto script = PluginDirectory() / L"EmbeddedLyricsTagWriter.py";
        const std::wstring command = !helper.empty()
            ? L"\"" + helper.wstring() + L"\" " + arguments
            : L"py.exe \"" + script.wstring() + L"\" " + arguments;
        Diagnostics::Info(std::wstring(L"Tag writer command for ") + label + L": [" + command + L"]");

        std::error_code fsError;
        const auto outputPath = std::filesystem::temp_directory_path(fsError) /
            (L"EmbeddedLyricsTagWriter-" + std::to_wstring(GetTickCount64()) + L".log");
        SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                    &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);

        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring mutableCommand = command;
        BOOL launched;
        if (output != INVALID_HANDLE_VALUE) {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = output;
            startup.hStdError = output;
            launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        } else {
            launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
        }
        if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
        if (!launched) {
            Diagnostics::Error(std::wstring(L"Failed to launch tag writer for ") + label +
                              L": GetLastError=" + std::to_wstring(GetLastError()));
            if (output != INVALID_HANDLE_VALUE) std::filesystem::remove(outputPath, fsError);
            return;
        }
        CloseHandle(process.hThread);
        pendingScripts_.push_back({process.hProcess,
            output != INVALID_HANDLE_VALUE ? outputPath : std::filesystem::path{}, label});
        Diagnostics::Info(std::wstring(L"Queued tag writer for ") + label);
    }
    // Non-blocking: reaps any tag-writer processes that have finished since the last frame and
    // logs their result, without waiting on ones still running.
    void PollTagWriterScripts() {
        for (auto it = pendingScripts_.begin(); it != pendingScripts_.end();) {
            if (WaitForSingleObject(it->process, 0) != WAIT_OBJECT_0) { ++it; continue; }
            DWORD exitCode = 0;
            GetExitCodeProcess(it->process, &exitCode);
            CloseHandle(it->process);
            std::wstring captured;
            if (!it->outputPath.empty()) {
                // The child's stdout/stderr are captured as raw UTF-8 bytes, not text-mode wide
                // characters, so read narrow and decode explicitly (WideFromUtf8, used elsewhere
                // in this file for the same reason) instead of a std::wifstream misreading them.
                std::ifstream capture(it->outputPath, std::ios::binary);
                std::ostringstream buffer; buffer << capture.rdbuf();
                captured = WideFromUtf8(buffer.str().c_str());
                capture.close();
                std::error_code fsError;
                std::filesystem::remove(it->outputPath, fsError);
            }
            if (exitCode != 0) {
                Diagnostics::Error(L"Tag writer for " + it->label + L" exited with code " +
                                  std::to_wstring(exitCode) +
                                  (captured.empty() ? L"" : L"; output: " + captured));
            } else {
                Diagnostics::Info(L"Tag writer for " + it->label + L" completed successfully");
            }
            it = pendingScripts_.erase(it);
        }
    }
    void TryCommitPendingRecording() {
        PollTagWriterScripts();
        if (pendingEditorWrite_ && !TrackLoadedAnywhere(pendingEditorAudioPath_)) {
            const std::wstring arguments = L"--write-editor-text \"" +
                pendingEditorAudioPath_.wstring() + L"\" \"" + pendingEditorTextPath_.wstring() +
                L"\" " + (pendingEditorSynchronized_ ? L"timed" : L"untimed");
            RunTagWriterScript(arguments, L"embedded lyrics");
            pendingEditorWrite_ = false;
        }
        if (!pendingTimingWrite_ || TrackLoadedAnywhere(pendingAudioPath_)) return;
        const std::wstring arguments = L"--write-recording \"" + pendingAudioPath_.wstring() +
            L"\" \"" + pendingTimingPath_.wstring() + L"\"";
        RunTagWriterScript(arguments, L"recorded timing");
        pendingTimingWrite_ = false;
    }

    void OpenAdvancedDialog() {
        AdvancedAppearanceSettings settings{
            FontFamily(), BackdropStyle(), BackdropStrength(),
            static_cast<int>(PaletteIndex(textColorParameter_)),
            static_cast<int>(PaletteIndex(highlightColorParameter_)),
            static_cast<int>(PaletteIndex(readColorParameter_)),
            backgroundParameter_ != 0,
            static_cast<int>(DiscreteIndex(backgroundColorParameter_, std::size(kBackgroundPalette)))};
        settings.timedLines = timedLines_;
        settings.untimedLines = untimedLines_;
        settings.fontPercent = static_cast<int>(FontScale() * 100 + 0.5f);
        settings.verticalPercent = static_cast<int>(VerticalPosition() * 100 + 0.5f);
        settings.useUpfaders = useVolumeFadersParameter_ != 0;
        settings.autoTag = autoTagLrcParameter_ != 0;
        settings.recordTiming = recordTimingParameter_ != 0;
        if (!ShowAdvancedAppearanceDialog(GetForegroundWindow(), settings, customAppearance_)) return;
        timedLines_ = settings.timedLines;
        untimedLines_ = settings.untimedLines;
        fontSizeParameter_ = (settings.fontPercent / 100.0f - 0.5f) / 1.5f;
        verticalPositionParameter_ = (settings.verticalPercent / 100.0f - 0.1f) / 0.8f;
        useVolumeFadersParameter_ = settings.useUpfaders;
        autoTagLrcParameter_ = settings.autoTag;
        if (recordTimingParameter_ != static_cast<int>(settings.recordTiming)) {
            recordTimingParameter_ = settings.recordTiming ? 1 : 0;
            OnParameter(9);
        }
        fontFamilyParameter_ = static_cast<float>(settings.font) /
                               static_cast<float>(std::size(kFontNames) - 1);
        backdropStyleParameter_ = static_cast<float>(settings.backdrop) /
                                  static_cast<float>(std::size(kBackdropNames) - 1);
        backdropStrengthParameter_ = static_cast<float>(settings.strength) /
                                     static_cast<float>(std::size(kStrengthNames) - 1);
        textColorParameter_ = static_cast<float>(settings.textColor) /
                              static_cast<float>(std::size(kPalette) - 1);
        highlightColorParameter_ = static_cast<float>(settings.highlightColor) /
                                   static_cast<float>(std::size(kPalette) - 1);
        readColorParameter_ = static_cast<float>(settings.readColor) /
                              static_cast<float>(std::size(kPalette) - 1);
        backgroundParameter_ = settings.backgroundEnabled ? 1 : 0;
        backgroundColorParameter_ = static_cast<float>(settings.backgroundColor) /
                                    static_cast<float>(std::size(kBackgroundPalette) - 1);
        texture_.Reset();
        SaveAdvancedSettings();
        PublishSettingsChange();
    }

    void LoadAdvancedSettings() {
        const auto path = AdvancedSettingsPath().wstring();
        const auto value = [&path](const wchar_t* key, int fallback, int maximum) {
            return std::clamp(static_cast<int>(GetPrivateProfileIntW(
                L"Advanced", key, fallback, path.c_str())), 0, maximum);
        };
        fontFamilyParameter_ = static_cast<float>(value(L"Font", 0, 5)) / 5.0f;
        backdropStyleParameter_ = static_cast<float>(value(L"Backdrop", 0, 2)) / 2.0f;
        backdropStrengthParameter_ = static_cast<float>(value(L"Strength", 1, 2)) / 2.0f;
        textColorParameter_ = static_cast<float>(value(L"TextColor", 0, 8)) / 8.0f;
        highlightColorParameter_ = static_cast<float>(value(L"HighlightColor", 1, 8)) / 8.0f;
        readColorParameter_ = static_cast<float>(value(L"ReadColor", 2, 8)) / 8.0f;
        backgroundParameter_ = value(L"BackgroundEnabled", 0, 1);
        backgroundColorParameter_ = static_cast<float>(value(L"BackgroundColor", 0, 8)) / 8.0f;
        timedLines_ = std::max(1, value(L"TimedLines", 7, 12));
        untimedLines_ = std::max(1, value(L"UntimedLines", 7, 12));
        fontSizeParameter_ = (std::max(50, value(L"FontPercent", 100, 200)) / 100.0f - 0.5f) / 1.5f;
        verticalPositionParameter_ = (std::max(10, value(L"VerticalPercent", 50, 90)) / 100.0f - 0.1f) / 0.8f;
        useVolumeFadersParameter_ = value(L"UseUpfaders", 0, 1);
        autoTagLrcParameter_ = value(L"AutoTag", 1, 1);
        // Shared with the other LRC instances; the fallback keeps the current value so an
        // ini written by an older version doesn't reset what VirtualDJ restored.
        wholeLineHighlightParameter_ = value(L"WholeLineHighlight", wholeLineHighlightParameter_ != 0, 1);
        countdownGapParameter_ = static_cast<float>(
            std::clamp(value(L"CountdownGapSeconds", CountdownGapSeconds(), 10), 3, 10) - 3) / 7.0f;
        showHeadingParameter_ = value(L"ShowHeading", showHeadingParameter_ != 0, 1);
        customAppearance_ = {
            value(L"CustomFont", FontFamily(), 5),
            value(L"CustomBackdrop", BackdropStyle(), 2),
            value(L"CustomStrength", BackdropStrength(), 2),
            value(L"CustomTextColor", static_cast<int>(PaletteIndex(textColorParameter_)), 8),
            value(L"CustomHighlightColor", static_cast<int>(PaletteIndex(highlightColorParameter_)), 8),
            value(L"CustomReadColor", static_cast<int>(PaletteIndex(readColorParameter_)), 8),
            value(L"CustomBackgroundEnabled", backgroundParameter_ != 0, 1) != 0,
            value(L"CustomBackgroundColor",
                  static_cast<int>(DiscreteIndex(backgroundColorParameter_,
                                                 std::size(kBackgroundPalette))), 8)};
    }

    void SaveAdvancedSettings() const {
        const auto path = AdvancedSettingsPath().wstring();
        const auto write = [&path](const wchar_t* key, std::size_t value) {
            const auto text = std::to_wstring(value);
            WritePrivateProfileStringW(L"Advanced", key, text.c_str(), path.c_str());
        };
        write(L"TimedLines", timedLines_);
        write(L"UntimedLines", untimedLines_);
        write(L"FontPercent", static_cast<int>(FontScale() * 100 + 0.5f));
        write(L"VerticalPercent", static_cast<int>(VerticalPosition() * 100 + 0.5f));
        write(L"UseUpfaders", useVolumeFadersParameter_);
        write(L"AutoTag", autoTagLrcParameter_);
        write(L"Font", static_cast<std::size_t>(FontFamily()));
        write(L"Backdrop", static_cast<std::size_t>(BackdropStyle()));
        write(L"Strength", static_cast<std::size_t>(BackdropStrength()));
        write(L"TextColor", PaletteIndex(textColorParameter_));
        write(L"HighlightColor", PaletteIndex(highlightColorParameter_));
        write(L"ReadColor", PaletteIndex(readColorParameter_));
        write(L"BackgroundEnabled", backgroundParameter_ != 0 ? 1u : 0u);
        write(L"BackgroundColor", DiscreteIndex(backgroundColorParameter_,
                                                  std::size(kBackgroundPalette)));
        write(L"CustomFont", customAppearance_.font);
        write(L"CustomBackdrop", customAppearance_.backdrop);
        write(L"CustomStrength", customAppearance_.strength);
        write(L"CustomTextColor", customAppearance_.textColor);
        write(L"CustomHighlightColor", customAppearance_.highlightColor);
        write(L"CustomReadColor", customAppearance_.readColor);
        write(L"CustomBackgroundEnabled", customAppearance_.backgroundEnabled ? 1u : 0u);
        write(L"CustomBackgroundColor", customAppearance_.backgroundColor);
        write(L"WholeLineHighlight", wholeLineHighlightParameter_ != 0 ? 1u : 0u);
        write(L"CountdownGapSeconds", static_cast<std::size_t>(CountdownGapSeconds()));
        write(L"ShowHeading", showHeadingParameter_ != 0 ? 1u : 0u);
    }

    // --- Settings shared between LRC Master and LRC Deck instances ---------------------
    // All appearance settings live in LRC Advanced.ini. Whichever instance saves a change
    // bumps the global VirtualDJ variable $lrc_settings_rev; every instance compares it once
    // per frame (an in-process VDJ query, no file I/O) and re-reads the ini only when the
    // number moved. The per-track timing offset is deliberately not shared.
    static constexpr const char* kSettingsRevQuery = "get_var '$lrc_settings_rev'";

    int SettingsRevision() {
        double revision = 0.0;
        if (FAILED(GetInfo(kSettingsRevQuery, &revision))) return seenSettingsRevision_;
        return static_cast<int>(revision);
    }

    void PublishSettingsChange() {
        const int next = SettingsRevision() + 1;
        char command[64]{};
        std::snprintf(command, sizeof(command), "set '$lrc_settings_rev' %d", next);
        SendCommand(command);
        seenSettingsRevision_ = next;
    }

    void SyncSharedSettings() {
        if (!settingsSynced_) {
            // First frame: VirtualDJ has finished restoring this plugin's own saved
            // parameters by now. The shared ini wins; if it predates the shared keys,
            // seed them from whatever this instance currently has.
            const auto path = AdvancedSettingsPath().wstring();
            const bool hasSharedKeys = GetPrivateProfileIntW(L"Advanced", L"CountdownGapSeconds",
                                                             -1, path.c_str()) >= 0;
            LoadAdvancedSettings();
            if (!hasSharedKeys) SaveAdvancedSettings();
            seenSettingsRevision_ = SettingsRevision();
            settingsSynced_ = true;
            return;
        }
        const int revision = SettingsRevision();
        if (revision == seenSettingsRevision_) return;
        seenSettingsRevision_ = revision;
        LoadAdvancedSettings();
        texture_.Reset();
        headingTexture_.Reset();
        Diagnostics::Info(L"Shared settings reloaded (revision " + std::to_wstring(revision) + L")");
    }

    ID3D11Device* device_{};
    TextTexture texture_;
    TextTexture headingTexture_;
    VideoRenderer renderer_;
    AsyncLyricsLoader loader_;
    std::recursive_mutex stateMutex_;
    std::filesystem::path loadedPath_;
    std::vector<std::wstring> trackHeading_;
    LyricsDocument lyrics_;
    bool drawContextLogged_{};
    int nextLineButton_{}; int previousLineButton_{}; int advancedButton_{}; int editBrowsedButton_{};
    float fontSizeParameter_{1.0f / 3.0f}; int recordTimingParameter_{};
    float verticalPositionParameter_{0.5f}; int editTextButton_{};
    int untimedLines_{7}; int timedLines_{7};
    int wholeLineHighlightParameter_{};
    float countdownGapParameter_{2.0f / 7.0f};
    float timingOffsetParameter_{0.5f};
    int showHeadingParameter_{1};
    float textColorParameter_{}; float highlightColorParameter_{1.0f / 8.0f};
    float readColorParameter_{2.0f / 8.0f};
    float fontFamilyParameter_{}; float backdropStyleParameter_{};
    float backdropStrengthParameter_{0.5f};

    std::size_t activeLine_{}; std::size_t scrollFromLine_{}; bool untimedScrollActive_{};
    std::chrono::steady_clock::time_point untimedScrollStarted_{};
    std::chrono::steady_clock::time_point nextLyricsSuppressCheck_{};
    std::vector<std::int64_t> recordedTimes_; std::size_t recordingNextLine_{}; int currentDeck_{};
    bool pendingTimingWrite_{}; std::filesystem::path pendingAudioPath_, pendingTimingPath_;
    bool pendingEditorWrite_{}; bool pendingEditorSynchronized_{};
    std::filesystem::path pendingEditorAudioPath_, pendingEditorTextPath_;
    struct PendingScript { HANDLE process; std::filesystem::path outputPath; std::wstring label; };
    std::vector<PendingScript> pendingScripts_;
    MasterDeckSelector masterDeckSelector_;
    int useVolumeFadersParameter_{};
    int autoTagLrcParameter_{1};
    int backgroundParameter_{};
    float backgroundColorParameter_{};
    AdvancedAppearanceSettings customAppearance_{};
    bool settingsSynced_{};
    int seenSettingsRevision_{};
};

STDAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, LPVOID* object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (memcmp(&classId, &CLSID_VdjPlugin8, sizeof(GUID)) != 0 ||
        memcmp(&interfaceId, &IID_IVdjPluginVideoFx8, sizeof(GUID)) != 0) return CLASS_E_CLASSNOTAVAILABLE;
    *object = new EmbeddedLyricsPlugin();
    return S_OK;
}
#endif








