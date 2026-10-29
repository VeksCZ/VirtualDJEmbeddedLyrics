#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>

#include "AsyncLyricsLoader.hpp"
#include "LrclibLookup.hpp"
#include "LyricsLayout.hpp"
#include "LrcShift.hpp"
#include "LyricsTiming.hpp"
#include "MacLyricsWindow.hpp"
#include "MacTextLayer.hpp"
#include "MasterDeckSelector.hpp"
#include "vdjVideo8.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef LRC_PLUGIN_VERSION
#define LRC_PLUGIN_VERSION "0.0.0-dev"
#endif

// One source, two plugins (like Plugin.cpp on Windows): LRC Master (master-only overlay)
// and, with LRC_DECK_VARIANT, LRC Deck (an ordinary video FX for one deck's FX chain).
#ifdef LRC_DECK_VARIANT
#define LRC_PLUGIN_NAME "LRC Deck"
#else
#define LRC_PLUGIN_NAME "LRC Master"
#endif

// Bridges an NSControl action to a block (used for live slider labels in Advanced).
@interface LRCVDJActionRelay : NSObject
@property(nonatomic, copy) void (^block)(void);
- (void)fire:(id)sender;
@end
@implementation LRCVDJActionRelay
- (void)fire:(id)sender {
    (void)sender;
    if (self.block) self.block();
}
@end

namespace {
// Same palettes and order as Windows so indices stored in the shared settings mean the same.
constexpr MacRgb kPalette[] = {
    {1.0, 1.0, 1.0}, {1.0, 210 / 255.0, 0.0}, {150 / 255.0, 150 / 255.0, 150 / 255.0},
    {1.0, 138 / 255.0, 36 / 255.0}, {240 / 255.0, 68 / 255.0, 68 / 255.0}, {66 / 255.0, 214 / 255.0, 107 / 255.0},
    {69 / 255.0, 217 / 255.0, 232 / 255.0}, {75 / 255.0, 131 / 255.0, 1.0}, {217 / 255.0, 81 / 255.0, 232 / 255.0}};
constexpr const char* kPaletteNames[] = {"White", "Yellow", "Gray", "Orange", "Red", "Green", "Cyan", "Blue", "Magenta"};
constexpr MacRgb kBackgrounds[] = {
    {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, {150 / 255.0, 150 / 255.0, 150 / 255.0},
    {240 / 255.0, 68 / 255.0, 68 / 255.0}, {66 / 255.0, 214 / 255.0, 107 / 255.0}, {75 / 255.0, 131 / 255.0, 1.0},
    {1.0, 210 / 255.0, 0.0}, {1.0, 138 / 255.0, 36 / 255.0}, {217 / 255.0, 81 / 255.0, 232 / 255.0}};
constexpr const char* kBackgroundNames[] = {"Black", "White", "Gray", "Red", "Green", "Blue", "Yellow", "Orange", "Magenta"};
constexpr const char* kFontNames[] = {"Arial", "Segoe UI", "Verdana", "Tahoma", "Trebuchet", "Calibri"};
constexpr const char* kBackdropNames[] = {"Outline", "Shadow", "Outline + Shadow"};
constexpr const char* kStrengthNames[] = {"Thin", "Normal", "Strong"};

// Shared with LRC Master/LRC Deck in the same VirtualDJ process (and with the key names of
// the Windows LRC Advanced.ini). Whichever instance saves bumps $lrc_settings_rev.
struct LrcSettings {
    int font{}, backdrop{}, strength{1};
    int textColor{}, highlightColor{1}, readColor{2};
    bool backgroundEnabled{};
    int backgroundColor{};
    int timedLines{7}, untimedLines{7};
    int fontPercent{100}, verticalPercent{50};
    bool useUpfaders{}, autoTag{true};
    bool wholeLine{}, showHeading{true};
    int countdownGapSeconds{5};
};

NSString* Key(const char* name) {
    return [@"VirtualDJEmbeddedLyrics." stringByAppendingString:[NSString stringWithUTF8String:name]];
}

int ReadInt(NSUserDefaults* d, const char* name, int fallback, int low, int high) {
    if (![d objectForKey:Key(name)]) return fallback;
    return std::clamp(static_cast<int>([d integerForKey:Key(name)]), low, high);
}

LrcSettings LoadSettings() {
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    LrcSettings s;
    s.font = ReadInt(d, "Font", s.font, 0, 5);
    s.backdrop = ReadInt(d, "Backdrop", s.backdrop, 0, 2);
    s.strength = ReadInt(d, "Strength", s.strength, 0, 2);
    s.textColor = ReadInt(d, "TextColor", s.textColor, 0, 8);
    s.highlightColor = ReadInt(d, "HighlightColor", s.highlightColor, 0, 8);
    s.readColor = ReadInt(d, "ReadColor", s.readColor, 0, 8);
    s.backgroundEnabled = ReadInt(d, "BackgroundEnabled", 0, 0, 1) != 0;
    s.backgroundColor = ReadInt(d, "BackgroundColor", s.backgroundColor, 0, 8);
    s.timedLines = ReadInt(d, "TimedLines", s.timedLines, 1, 12);
    s.untimedLines = ReadInt(d, "UntimedLines", s.untimedLines, 1, 12);
    s.fontPercent = ReadInt(d, "FontPercent", s.fontPercent, 50, 200);
    s.verticalPercent = ReadInt(d, "VerticalPercent", s.verticalPercent, 10, 90);
    s.useUpfaders = ReadInt(d, "UseUpfaders", 0, 0, 1) != 0;
    s.autoTag = ReadInt(d, "AutoTag", 1, 0, 1) != 0;
    s.wholeLine = ReadInt(d, "WholeLineHighlight", 0, 0, 1) != 0;
    s.showHeading = ReadInt(d, "ShowHeading", 1, 0, 1) != 0;
    s.countdownGapSeconds = ReadInt(d, "CountdownGapSeconds", s.countdownGapSeconds, 3, 10);
    return s;
}

void SaveSettings(const LrcSettings& s) {
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    auto put = [&](const char* name, int value) { [d setInteger:value forKey:Key(name)]; };
    put("Font", s.font); put("Backdrop", s.backdrop); put("Strength", s.strength);
    put("TextColor", s.textColor); put("HighlightColor", s.highlightColor); put("ReadColor", s.readColor);
    put("BackgroundEnabled", s.backgroundEnabled); put("BackgroundColor", s.backgroundColor);
    put("TimedLines", s.timedLines); put("UntimedLines", s.untimedLines);
    put("FontPercent", s.fontPercent); put("VerticalPercent", s.verticalPercent);
    put("UseUpfaders", s.useUpfaders); put("AutoTag", s.autoTag);
    put("WholeLineHighlight", s.wholeLine); put("ShowHeading", s.showHeading);
    put("CountdownGapSeconds", s.countdownGapSeconds);
}

MacTextStyle MakeTextStyle(const LrcSettings& s) {
    MacTextStyle style;
    style.font = s.font;
    style.backdrop = s.backdrop;
    style.strength = s.strength;
    style.text = kPalette[s.textColor];
    style.highlight = kPalette[s.highlightColor];
    style.read = kPalette[s.readColor];
    style.background = s.backgroundEnabled;
    style.backgroundColor = kBackgrounds[s.backgroundColor];
    return style;
}

std::wstring WideFromUtf8(const char* value) { return value ? LrclibWiden(value) : std::wstring{}; }

NSPopUpButton* Popup(NSView* parent, NSRect frame, const char* const* names, std::size_t count, int selected) {
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:frame pullsDown:NO];
    for (std::size_t i = 0; i < count; ++i) [popup addItemWithTitle:[NSString stringWithUTF8String:names[i]]];
    [popup selectItemAtIndex:std::clamp<NSInteger>(selected, 0, static_cast<NSInteger>(count) - 1)];
    [parent addSubview:popup];
    return popup;
}

NSTextField* Label(NSView* parent, NSRect frame, NSString* text) {
    NSTextField* label = [NSTextField labelWithString:text];
    label.frame = frame;
    [parent addSubview:label];
    return label;
}

NSButton* Check(NSView* parent, NSRect frame, NSString* title, bool on) {
    NSButton* check = [NSButton checkboxWithTitle:title target:nil action:nil];
    check.frame = frame;
    check.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    [parent addSubview:check];
    return check;
}

NSSlider* Slider(NSView* parent, NSRect frame, double low, double high, double value) {
    NSSlider* slider = [NSSlider sliderWithValue:value minValue:low maxValue:high target:nil action:nil];
    slider.frame = frame;
    slider.numberOfTickMarks = static_cast<NSInteger>(std::min(high - low + 1, 20.0));
    slider.allowsTickMarkValuesOnly = high - low <= 20;
    [parent addSubview:slider];
    return slider;
}
}  // namespace

class MacLyricsPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnLoad() override {
        settings_ = LoadSettings();
        ApplySettingsToParameters();
        // Same parameter set and ids as the Windows plugin.
        if (DeclareParameterButton(&editTextButton_, 6, "Edit or find lyrics for the playing song", "Edit lyrics") != S_OK ||
            DeclareParameterButton(&nextLineButton_, 7, "Next line", "Next line") != S_OK ||
            DeclareParameterButton(&previousLineButton_, 8, "Previous line", "Previous") != S_OK ||
            DeclareParameterButton(&advancedButton_, 10, "Advanced", "Advanced") != S_OK ||
            DeclareParameterButton(&editBrowsedButton_, 11, "Edit or find lyrics for the song selected in the browser",
                                   "Edit browsed") != S_OK ||
            DeclareParameterSwitch(&wholeLineParameter_, 12, "Highlight whole active line", "Whole line",
                                   settings_.wholeLine) != S_OK ||
            DeclareParameterSlider(&countdownGapParameter_, 13, "Countdown gap (3-10 seconds)", "Countdown gap",
                                   countdownGapParameter_) != S_OK ||
            DeclareParameterSlider(&timingOffsetParameter_, 14, "Lyrics timing offset (-5 to +5 seconds)",
                                   "Timing offset", 0.5f) != S_OK ||
            DeclareParameterSwitch(&showHeadingParameter_, 15, "Show track name and artist", "Show name",
                                   settings_.showHeading) != S_OK ||
            DeclareParameterButton(&startHereButton_, 16,
                                   "Timed lyrics: move all lines so the first one starts now", "Start here") != S_OK)
            return -1;
        return S_OK;
    }

    HRESULT VDJ_API OnParameter(int id) override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (id == 7 && nextLineButton_) { AdvanceUntimedLine(); nextLineButton_ = 0; }
        else if (id == 8 && previousLineButton_) {
            if (!lyrics_.synchronized && activeLine_ > 0) BeginUntimedScroll(activeLine_ - 1);
            previousLineButton_ = 0;
        } else if (id == 10 && advancedButton_) { advancedButton_ = 0; OpenAdvancedDialog(); }
        else if (id == 6 && editTextButton_) {
            editTextButton_ = 0;
            const int deck = currentDeck_ > 0 ? currentDeck_ : 1;
            char command[128]{}, value[4096]{};
            std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
            double length = 0.0;
            if (GetStringInfo(command, value, sizeof(value)) == S_OK) {
                std::snprintf(command, sizeof(command), "deck %d get_songlength", deck);
                if (GetInfo(command, &length) != S_OK) length = 0.0;
                OpenLyricsWindow(std::filesystem::path{std::u8string(reinterpret_cast<const char8_t*>(value))}, length);
            }
        } else if (id == 16 && startHereButton_) {
            startHereButton_ = 0;
            StartLyricsHere();
        } else if (id == 11 && editBrowsedButton_) {
            editBrowsedButton_ = 0;
            char value[4096]{};
            if (GetStringInfo("get_browsed_filepath", value, sizeof(value)) == S_OK)
                OpenLyricsWindow(std::filesystem::path{std::u8string(reinterpret_cast<const char8_t*>(value))}, 0.0);
        } else if ((id == 12 || id == 13 || id == 15) && settingsSynced_) {
            // Panel options are shared settings: persist and tell the other instances.
            settings_.wholeLine = wholeLineParameter_ != 0;
            settings_.showHeading = showHeadingParameter_ != 0;
            settings_.countdownGapSeconds = CountdownGapSeconds();
            SaveSettings(settings_);
            PublishSettingsChange();
            lyricsLayer_.Reset();
            bannerLayer_.Reset();
        }
        return S_OK;
    }

    HRESULT VDJ_API OnGetParameterString(int id, char* output, int outputSize) override {
        if (!output || outputSize <= 0) return E_NOTIMPL;
        if (id == 13) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%d s", CountdownGapSeconds());
            return S_OK;
        }
        if (id == 14) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%+d ms", TimingOffsetMs());
            return S_OK;
        }
        return E_NOTIMPL;
    }

    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
        info->PluginName = LRC_PLUGIN_NAME;
        info->Author = "Slava / OpenAI";
        info->Description = "Timed embedded/LRC lyrics and manual untimed lyrics pages (macOS Metal)";
        info->Version = LRC_PLUGIN_VERSION;
#ifdef LRC_DECK_VARIANT
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_OUTPUTRESOLUTION;
#else
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_MASTERONLY | VDJFLAG_VIDEO_OVERLAY;
#endif
        info->Bitmap = nullptr;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }
    HRESULT VDJ_API OnDeviceClose() override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        lyricsLayer_.Reset();
        bannerLayer_.Reset();
        return S_OK;
    }

    HRESULT VDJ_API OnDraw() override {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
#ifdef LRC_DECK_VARIANT
        DrawDeck();  // no OVERLAY flag: put the deck's own picture down first, every frame
#endif
        void* encoderPointer = nullptr;
        if (GetDevice(VdjVideoEngineMetal, &encoderPointer) != S_OK || !encoderPointer) return S_FALSE;
        id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPointer;
        SyncSharedSettings();
        TryCommitPendingRecording();
#ifdef LRC_DECK_VARIANT
        const int deck = OwnDeck();
#else
        const int deck = VisibleVideoDeck();
#endif
        currentDeck_ = deck;
        if (deck <= 0) return S_OK;

        char pathBuffer[4096]{};
        char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
        if (GetStringInfo(command, pathBuffer, sizeof(pathBuffer)) != S_OK) return S_OK;
        const std::filesystem::path path{std::u8string(reinterpret_cast<const char8_t*>(pathBuffer))};
        if (path.empty()) {
            loadedPath_.clear();
            lyrics_ = {};
            return S_OK;
        }
        if (reloadRequested_.exchange(false)) loadedPath_.clear();  // tags were just edited
        if (path != loadedPath_) {
            loadedPath_ = path;
            LoadTrackHeading(deck);
            lyrics_ = {};
            activeLine_ = 0;
            untimedScrollActive_ = false;
            timingOffsetParameter_ = 0.5f;  // per-track correction
            recordedTimes_.clear();
            recordingNextLine_ = 0;
            lyricsLayer_.Reset();
            bannerLayer_.Reset();
            loader_.Request(path);
        }
        if (auto completed = loader_.Poll(); completed && completed->path == loadedPath_) {
            lyrics_ = std::move(completed->result.document);
            lyricsLayer_.Reset();
            if (!lyrics_.empty() && settings_.autoTag) EnsureLyricsHashtag(deck);
        }

        const auto style = MakeTextStyle(settings_);
        id<MTLDevice> device = encoder.device;
        bool drawn = false;
        if (lyrics_.empty()) {
            drawn = lyricsLayer_.UpdateMessage(device, L"...", width, height, FontScale(), VerticalPosition(), style);
        } else if (!lyrics_.synchronized) {
            drawn = UpdateUntimedRibbon(device, style);
        } else {
            double elapsedMs = 0.0;
            std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", deck);
            if (GetInfo(command, &elapsedMs) != S_OK) return S_OK;
            drawn = UpdateTimed(device, style, AdjustLyricsTime(static_cast<std::int64_t>(elapsedMs), TimingOffsetMs()));
        }
        if (drawn) lyricsLayer_.Draw(encoder);
        if (settings_.showHeading && !trackHeading_.empty() &&
            bannerLayer_.UpdateBanner(device, trackHeading_, width, height, style))
            bannerLayer_.Draw(encoder);
        return S_OK;
    }

private:
    // ---- Shared settings ---------------------------------------------------------------
    void ApplySettingsToParameters() {
        wholeLineParameter_ = settings_.wholeLine;
        showHeadingParameter_ = settings_.showHeading;
        countdownGapParameter_ = static_cast<float>(settings_.countdownGapSeconds - 3) / 7.0f;
    }

    int SettingsRevision() {
        double revision = 0.0;
        if (GetInfo("get_var '$lrc_settings_rev'", &revision) != S_OK) return seenSettingsRevision_;
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
            // First frame: VirtualDJ has restored this plugin's own saved parameters by now;
            // the shared settings win so Master and Deck always agree.
            settings_ = LoadSettings();
            ApplySettingsToParameters();
            seenSettingsRevision_ = SettingsRevision();
            settingsSynced_ = true;
            return;
        }
        const int revision = SettingsRevision();
        if (revision == seenSettingsRevision_) return;
        seenSettingsRevision_ = revision;
        settings_ = LoadSettings();
        ApplySettingsToParameters();
        lyricsLayer_.Reset();
        bannerLayer_.Reset();
    }

    float FontScale() const { return static_cast<float>(settings_.fontPercent) / 100.0f; }
    float VerticalPosition() const { return static_cast<float>(settings_.verticalPercent) / 100.0f; }
    int CountdownGapSeconds() const {
        return 3 + static_cast<int>(std::clamp(countdownGapParameter_, 0.0f, 1.0f) * 7.0f + 0.5f);
    }
    int TimingOffsetMs() const {
        const int raw = static_cast<int>((std::clamp(timingOffsetParameter_, 0.0f, 1.0f) - 0.5f) * 10000.0f);
        return static_cast<int>(std::lround(raw / 10.0)) * 10;
    }

    // ---- Deck selection ----------------------------------------------------------------
#ifdef LRC_DECK_VARIANT
    int OwnDeck() {
        double deck = 0.0;
        if (GetInfo("get_deck", &deck) == S_OK && deck >= 1.0 && deck <= 99.0) return static_cast<int>(deck);
        return 0;
    }
#else
    int VisibleVideoDeck() {
        double balance = 0.0, left = 1.0, right = 2.0;
        if (GetInfo("get_leftdeck", &left) != S_OK) left = 1.0;
        if (GetInfo("get_rightdeck", &right) != S_OK) right = 2.0;
        const char* query = settings_.useUpfaders ? "get_crossfader_result" : "video_crossfader";
        if (GetInfo(query, &balance) != S_OK) return selector_.Current();
        return selector_.Select(balance, static_cast<int>(left), static_cast<int>(right));
    }
#endif

    // ---- Rendering (same timeline rules as Plugin.cpp UpdateVisible) ------------------
    bool UpdateTimed(id<MTLDevice> device, const MacTextStyle& style, std::int64_t now) {
        struct DisplayLine { std::wstring text; std::int64_t timeMs{}; bool pause{}; };
        constexpr std::int64_t scrollDuration = 650;
        std::vector<DisplayLine> timeline;
        timeline.reserve(lyrics_.lines.size() * 2 + 1);
        const auto firstLyricTime = lyrics_.lines.front().timeMs;
        const int gap = CountdownGapSeconds();
        if (ShowLyricCountdown(firstLyricTime, gap))
            timeline.push_back({std::to_wstring(LyricCountdown(firstLyricTime)), 0, true});
        for (std::size_t i = 0; i < lyrics_.lines.size(); ++i) {
            const auto& lyric = lyrics_.lines[i];
            timeline.push_back({lyric.text, lyric.timeMs, false});
            if (i + 1 >= lyrics_.lines.size()) continue;
            const auto nextTime = lyrics_.lines[i + 1].timeMs;
            const auto highlight = std::min(EstimateLyricHighlightMs(lyric.text),
                std::max<std::int64_t>(500, nextTime - lyric.timeMs - scrollDuration));
            const auto pauseStart = lyric.timeMs + highlight + 700;
            const auto pauseDuration = nextTime - pauseStart;
            if (ShowLyricCountdown(pauseDuration, gap))
                timeline.push_back({std::to_wstring(LyricCountdown(pauseDuration)), pauseStart, true});
        }
        const auto it = std::upper_bound(timeline.begin(), timeline.end(), now,
            [](std::int64_t value, const DisplayLine& line) { return value < line.timeMs; });
        const auto active = it == timeline.begin() ? 0u
            : static_cast<std::size_t>(std::distance(timeline.begin(), it) - 1);
        const auto start = timeline[active].timeMs;
        const auto next = active + 1 < timeline.size() ? timeline[active + 1].timeMs : start + 5000;
        const auto interval = std::max<std::int64_t>(1, next - start);

        std::vector<std::wstring> visible;
        std::vector<bool> subdued;
        visible.reserve(timeline.size());
        subdued.reserve(timeline.size());
        bool countdownVisible = false;
        for (std::size_t i = 0; i < timeline.size(); ++i) {
            if (timeline[i].pause) {
                const int position = i < active ? -1 : i > active ? 1 : 0;
                visible.push_back(LyricPauseDisplayText(timeline[i].text, position, next - now));
                if (i == active) countdownVisible = !visible.back().empty();
            } else {
                visible.push_back(timeline[i].text);
            }
            subdued.push_back(timeline[i].pause && i != active);
        }
        float highlightProgress = countdownVisible ? 1.0f : 0.0f;
        if (!timeline[active].pause) {
            const auto highlightDuration = std::min(EstimateLyricHighlightMs(timeline[active].text),
                std::max<std::int64_t>(500, interval - scrollDuration));
            highlightProgress = LyricHighlightProgress(now, start, highlightDuration, settings_.wholeLine);
        }
        const auto scrollProgress = timeline[active].pause
            ? UnitProgress(now, next - scrollDuration, scrollDuration)
            : UnitProgress(now, start, interval);
        return lyricsLayer_.UpdateTimed(device, visible, active, highlightProgress, scrollProgress, width, height,
                                        FontScale(), VerticalPosition(), subdued, style,
                                        static_cast<std::size_t>(settings_.timedLines));
    }

    void BeginUntimedScroll(std::size_t target) {
        if (lyrics_.lines.empty()) return;
        target = std::min(target, lyrics_.lines.size() - 1);
        if (target == activeLine_) return;
        scrollFromLine_ = activeLine_;
        activeLine_ = target;
        untimedScrollStarted_ = std::chrono::steady_clock::now();
        untimedScrollActive_ = target > scrollFromLine_;
    }

    bool UpdateUntimedRibbon(id<MTLDevice> device, const MacTextStyle& style) {
        auto renderActive = activeLine_;
        float scroll = 0.0f;
        if (untimedScrollActive_) {
            scroll = std::clamp(std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                                             untimedScrollStarted_).count() / 0.45f, 0.0f, 1.0f);
            if (scroll < 1.0f) renderActive = scrollFromLine_;
            else { untimedScrollActive_ = false; scroll = 0.0f; }
        }
        std::vector<std::wstring> visible;
        visible.reserve(lyrics_.lines.size());
        for (const auto& line : lyrics_.lines) visible.push_back(line.text);
        return lyricsLayer_.UpdateTimed(device, visible, renderActive, 1.0f, scroll, width, height, FontScale(),
                                        VerticalPosition(), {}, style,
                                        static_cast<std::size_t>(settings_.untimedLines));
    }

    void LoadTrackHeading(int deck) {
        char command[128]{}, value[4096]{};
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'artist'", deck);
        const auto artist = GetStringInfo(command, value, sizeof(value)) == S_OK ? WideFromUtf8(value) : std::wstring{};
        value[0] = '\0';
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'title'", deck);
        const auto title = GetStringInfo(command, value, sizeof(value)) == S_OK ? WideFromUtf8(value) : std::wstring{};
        trackHeading_ = artist.empty() ? title : title.empty() ? artist : artist + L" - " + title;
    }

    void EnsureLyricsHashtag(int deck) {
        char command[256]{}, user1[4096]{};
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'user 1'", deck);
        if (GetStringInfo(command, user1, sizeof(user1)) != S_OK) return;
        std::string value{user1};
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const char* wanted = lyrics_.synchronized ? "#sylt" : "# - uslt";
        for (const char* obsolete : {"#lrc", "#uslt", "#-uslt", lyrics_.synchronized ? "# - uslt" : "#sylt"}) {
            if (value.find(obsolete) == std::string::npos) continue;
            std::snprintf(command, sizeof(command), "deck %d loaded_song_hashtag 'user 1' '%s'", deck, obsolete);
            SendCommand(command);
        }
        if (value.find(wanted) != std::string::npos) return;
        std::snprintf(command, sizeof(command), "deck %d loaded_song_hashtag 'user 1' '%s'", deck, wanted);
        SendCommand(command);
    }

    // ---- LrcHelper (next to the plugin bundles; LRC Deck shares the VideoOverlay copy) ---
    static std::filesystem::path HelperExecutable() {
        Dl_info info{};
        if (!dladdr(reinterpret_cast<const void*>(&HelperExecutable), &info) || !info.dli_fname) return {};
        // .../<Category>/LRCMaster.bundle/Contents/MacOS/LRCMaster
        const auto category = std::filesystem::path(info.dli_fname).parent_path().parent_path()
                                  .parent_path().parent_path();
        for (const auto& directory : {category, category.parent_path() / "VideoOverlay"}) {
            std::error_code error;
            const auto candidate = directory / "LrcHelper";
            if (std::filesystem::is_regular_file(candidate, error)) return candidate;
        }
        return {};
    }

    void OpenLyricsWindow(const std::filesystem::path& audio, double durationSeconds) {
        std::string extension = audio.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (audio.empty() || extension != ".mp3") {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"LRC lyrics";
            alert.informativeText = @"Load an MP3 on the deck, or select an MP3 song row in the browser first.";
            [alert runModal];
            return;
        }
        MacLyricsRequest request;
        request.audioPath = audio;
        request.helper = HelperExecutable();
        request.durationSeconds = durationSeconds;
        if (ShowMacLyricsWindow(request)) reloadRequested_ = true;
    }

    // ---- Record timing (plain lyrics -> timed tags, written after the track is unloaded) -
    void AdvanceUntimedLine() {
        if (lyrics_.synchronized || lyrics_.lines.empty()) return;
        if (!recordTiming_) {
            if (activeLine_ + 1 < lyrics_.lines.size()) BeginUntimedScroll(activeLine_ + 1);
            return;
        }
        if (recordedTimes_.size() != lyrics_.lines.size()) {
            recordedTimes_.assign(lyrics_.lines.size(), -1);
            recordingNextLine_ = 0;
        }
        if (recordingNextLine_ >= lyrics_.lines.size() || currentDeck_ <= 0) return;
        double elapsed = 0.0;
        char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", currentDeck_);
        if (GetInfo(command, &elapsed) != S_OK) return;
        const auto target = recordingNextLine_++;
        if (target != activeLine_) BeginUntimedScroll(target);
        recordedTimes_[target] = static_cast<std::int64_t>(elapsed);
        if (recordingNextLine_ == lyrics_.lines.size()) QueueTimingRecording();
    }

    void QueueTimingRecording() {
        std::string extension = loadedPath_.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        recordTiming_ = false;
        if (extension != ".mp3") return;
        const char* tmp = std::getenv("TMPDIR");
        pendingTimingPath_ = std::filesystem::path(tmp && *tmp ? tmp : "/tmp") /
            ("EmbeddedLyrics-" + std::to_string(std::hash<std::string>{}(loadedPath_.string())) + ".timing");
        std::ofstream output(pendingTimingPath_, std::ios::binary | std::ios::trunc);
        for (std::size_t i = 0; i < lyrics_.lines.size(); ++i)
            output << recordedTimes_[i] << '\t' << LrclibUtf8(lyrics_.lines[i].text) << '\n';
        output.close();
        pendingAudioPath_ = loadedPath_;
        pendingTimingWrite_ = true;
    }

    bool TrackLoadedAnywhere(const std::filesystem::path& path) {
        double decks = 2.0;
        if (GetInfo("get_decks", &decks) != S_OK || decks < 1.0) decks = 2.0;
        for (int deck = 1; deck <= static_cast<int>(decks); ++deck) {
            char command[64]{}, value[4096]{};
            std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
            if (GetStringInfo(command, value, sizeof(value)) == S_OK &&
                std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value))) == path)
                return true;
        }
        return false;
    }

    // "Start here": the vocals start now -> move every timestamp by the same amount so the first
    // line starts at the current position; shown at once and saved to the MP3 tag by LrcHelper.
    void StartLyricsHere() {
        if (loadedPath_.empty() || lyrics_.empty() || !lyrics_.synchronized || currentDeck_ <= 0) return;
        char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", currentDeck_);
        double elapsed = 0.0;
        if (GetInfo(command, &elapsed) != S_OK) return;
        std::wstring text;
        for (const auto& line : lyrics_.lines) {
            const auto ms = std::max<std::int64_t>(0, line.timeMs);
            wchar_t stamp[32]{};
            std::swprintf(stamp, 32, L"[%02lld:%02lld.%02lld]", static_cast<long long>(ms / 60000),
                          static_cast<long long>((ms / 1000) % 60), static_cast<long long>((ms % 1000) / 10));
            text += stamp + line.text + L"\n";
        }
        std::int64_t delta = 0;
        if (!ShiftLrcTimestamps(text, static_cast<std::int64_t>(elapsed), &delta)) return;
        for (auto& line : lyrics_.lines) {
            line.timeMs = std::max<std::int64_t>(0, line.timeMs + delta);
            for (auto& token : line.tokens) token.timeMs = std::max<std::int64_t>(0, token.timeMs + delta);
        }
        timingOffsetParameter_ = 0.5f;   // the shift replaces any temporary offset
        lyricsLayer_.Reset();
        const auto helper = HelperExecutable();
        if (helper.empty()) return;
        std::string extension = loadedPath_.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".mp3") return;
        const char* tmp = std::getenv("TMPDIR");
        const auto textPath = std::filesystem::path(tmp && *tmp ? tmp : "/tmp") /
            ("EmbeddedLyrics-start-" + std::to_string(std::hash<std::string>{}(loadedPath_.string())) + ".txt");
        {
            std::ofstream output(textPath, std::ios::binary | std::ios::trunc);
            output << LrclibUtf8(text);
        }
        const auto audio = loadedPath_;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            NSTask* task = [[NSTask alloc] init];
            task.executableURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:helper.c_str()]];
            task.arguments = @[@"--write-editor-text", [NSString stringWithUTF8String:audio.c_str()],
                               [NSString stringWithUTF8String:textPath.c_str()], @"timed"];
            NSError* error = nil;
            if ([task launchAndReturnError:&error]) [task waitUntilExit];
        });
    }

    void TryCommitPendingRecording() {
        if (!pendingTimingWrite_ || TrackLoadedAnywhere(pendingAudioPath_)) return;
        pendingTimingWrite_ = false;
        const auto helper = HelperExecutable();
        if (helper.empty()) return;
        const auto audio = pendingAudioPath_, timing = pendingTimingPath_;
        // Never block the render thread on the helper.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            NSTask* task = [[NSTask alloc] init];
            task.executableURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:helper.c_str()]];
            task.arguments = @[@"--write-recording", [NSString stringWithUTF8String:audio.c_str()],
                               [NSString stringWithUTF8String:timing.c_str()]];
            NSError* error = nil;
            if ([task launchAndReturnError:&error]) [task waitUntilExit];
        });
    }

    // ---- Advanced dialog (same options as Windows; no live preview yet) ---------------
    void OpenAdvancedDialog() {
        @autoreleasepool {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"LRC Advanced settings";
            alert.informativeText = @"Shared by LRC Master and LRC Deck.";
            [alert addButtonWithTitle:@"Apply"];
            [alert addButtonWithTitle:@"Cancel"];
            NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 560, 400)];
            const LrcSettings s = settings_;
            Label(view, NSMakeRect(0, 372, 120, 20), @"Font");
            NSPopUpButton* font = Popup(view, NSMakeRect(130, 368, 150, 26), kFontNames, std::size(kFontNames), s.font);
            Label(view, NSMakeRect(290, 372, 100, 20), @"Backdrop");
            NSPopUpButton* backdrop = Popup(view, NSMakeRect(390, 368, 160, 26), kBackdropNames, std::size(kBackdropNames), s.backdrop);
            Label(view, NSMakeRect(0, 340, 120, 20), @"Strength");
            NSPopUpButton* strength = Popup(view, NSMakeRect(130, 336, 150, 26), kStrengthNames, std::size(kStrengthNames), s.strength);
            Label(view, NSMakeRect(0, 300, 120, 20), @"Text color");
            NSPopUpButton* text = Popup(view, NSMakeRect(130, 296, 150, 26), kPaletteNames, std::size(kPaletteNames), s.textColor);
            Label(view, NSMakeRect(290, 300, 100, 20), @"Highlight");
            NSPopUpButton* highlight = Popup(view, NSMakeRect(390, 296, 160, 26), kPaletteNames, std::size(kPaletteNames), s.highlightColor);
            Label(view, NSMakeRect(0, 268, 120, 20), @"Read / waiting");
            NSPopUpButton* read = Popup(view, NSMakeRect(130, 264, 150, 26), kPaletteNames, std::size(kPaletteNames), s.readColor);
            NSButton* background = Check(view, NSMakeRect(0, 228, 200, 22), @"Solid background", s.backgroundEnabled);
            NSPopUpButton* backgroundColor = Popup(view, NSMakeRect(210, 226, 150, 26), kBackgroundNames,
                                                   std::size(kBackgroundNames), s.backgroundColor);
            NSTextField* timedLabel = Label(view, NSMakeRect(0, 188, 230, 20), @"");
            NSSlider* timed = Slider(view, NSMakeRect(240, 186, 310, 24), 1, 12, s.timedLines);
            NSTextField* untimedLabel = Label(view, NSMakeRect(0, 156, 230, 20), @"");
            NSSlider* untimed = Slider(view, NSMakeRect(240, 154, 310, 24), 1, 12, s.untimedLines);
            NSTextField* sizeLabel = Label(view, NSMakeRect(0, 124, 230, 20), @"");
            NSSlider* size = Slider(view, NSMakeRect(240, 122, 310, 24), 50, 200, s.fontPercent);
            NSTextField* positionLabel = Label(view, NSMakeRect(0, 92, 230, 20), @"");
            NSSlider* position = Slider(view, NSMakeRect(240, 90, 310, 24), 10, 90, s.verticalPercent);
            NSButton* upfaders = Check(view, NSMakeRect(0, 52, 540, 22), @"Follow audio upfaders instead of the video crossfader", s.useUpfaders);
            NSButton* autoTag = Check(view, NSMakeRect(0, 26, 540, 22), @"Add #sylt / # - uslt to VirtualDJ User 1", s.autoTag);
            NSButton* record = Check(view, NSMakeRect(0, 0, 540, 22), @"Record timing: Next line stamps plain lyrics, saved after unload", recordTiming_);
            auto refresh = [=] {
                timedLabel.stringValue = [NSString stringWithFormat:@"Timed lines: %d", static_cast<int>(timed.integerValue)];
                untimedLabel.stringValue = [NSString stringWithFormat:@"Untimed lines: %d", static_cast<int>(untimed.integerValue)];
                sizeLabel.stringValue = [NSString stringWithFormat:@"Font size: %d%%", static_cast<int>(size.integerValue)];
                positionLabel.stringValue = [NSString stringWithFormat:@"Vertical position: %d%%", static_cast<int>(position.integerValue)];
            };
            refresh();
            // Slider targets are weak: keep the relays alive for the lifetime of the dialog.
            LRCVDJActionRelay* relay = [[LRCVDJActionRelay alloc] init];
            relay.block = ^{ refresh(); };
            for (NSSlider* slider in @[timed, untimed, size, position]) {
                slider.continuous = YES;
                slider.target = relay;
                slider.action = @selector(fire:);
            }
            alert.accessoryView = view;
            const bool apply = [alert runModal] == NSAlertFirstButtonReturn;
            for (NSSlider* slider in @[timed, untimed, size, position]) slider.target = nil;
            relay = nil;
            if (!apply) return;
            LrcSettings updated = settings_;
            updated.font = static_cast<int>(font.indexOfSelectedItem);
            updated.backdrop = static_cast<int>(backdrop.indexOfSelectedItem);
            updated.strength = static_cast<int>(strength.indexOfSelectedItem);
            updated.textColor = static_cast<int>(text.indexOfSelectedItem);
            updated.highlightColor = static_cast<int>(highlight.indexOfSelectedItem);
            updated.readColor = static_cast<int>(read.indexOfSelectedItem);
            updated.backgroundEnabled = background.state == NSControlStateValueOn;
            updated.backgroundColor = static_cast<int>(backgroundColor.indexOfSelectedItem);
            updated.timedLines = static_cast<int>(timed.integerValue);
            updated.untimedLines = static_cast<int>(untimed.integerValue);
            updated.fontPercent = static_cast<int>(size.integerValue);
            updated.verticalPercent = static_cast<int>(position.integerValue);
            updated.useUpfaders = upfaders.state == NSControlStateValueOn;
            updated.autoTag = autoTag.state == NSControlStateValueOn;
            const bool recording = record.state == NSControlStateValueOn;
            if (recording != recordTiming_) {
                recordTiming_ = recording;
                recordedTimes_.assign(lyrics_.lines.size(), -1);
                recordingNextLine_ = 0;
                if (recordTiming_ && !lyrics_.synchronized) activeLine_ = 0;
            }
            settings_ = updated;
            SaveSettings(settings_);
            PublishSettingsChange();
            lyricsLayer_.Reset();
            bannerLayer_.Reset();
        }
    }

    std::recursive_mutex mutex_;
    LrcSettings settings_;
    bool settingsSynced_{};
    int seenSettingsRevision_{};
    MacTextLayer lyricsLayer_;
    MacTextLayer bannerLayer_;
    AsyncLyricsLoader loader_;
    MasterDeckSelector selector_;
    std::filesystem::path loadedPath_;
    std::wstring trackHeading_;
    LyricsDocument lyrics_;
    std::size_t activeLine_{};
    std::size_t scrollFromLine_{};
    bool untimedScrollActive_{};
    std::chrono::steady_clock::time_point untimedScrollStarted_{};
    int currentDeck_{};
    std::atomic<bool> reloadRequested_{false};
    bool recordTiming_{};
    std::vector<std::int64_t> recordedTimes_;
    std::size_t recordingNextLine_{};
    bool pendingTimingWrite_{};
    std::filesystem::path pendingAudioPath_, pendingTimingPath_;
    int editTextButton_{}, nextLineButton_{}, previousLineButton_{}, advancedButton_{}, editBrowsedButton_{};
    int startHereButton_{};
    int wholeLineParameter_{}, showHeadingParameter_{1};
    float countdownGapParameter_{2.0f / 7.0f};
    float timingOffsetParameter_{0.5f};
};

extern "C" VDJ_EXPORT HRESULT VDJ_API DllGetClassObject(const GUID& classId, const GUID& interfaceId, void** object) {
    if (!object) return -1;
    *object = nullptr;
    if (std::memcmp(&classId, &CLSID_VdjPlugin8, sizeof(GUID)) != 0 ||
        std::memcmp(&interfaceId, &IID_IVdjPluginVideoFx8, sizeof(GUID)) != 0)
        return CLASS_E_CLASSNOTAVAILABLE;
    *object = new MacLyricsPlugin();
    return S_OK;
}
#endif
