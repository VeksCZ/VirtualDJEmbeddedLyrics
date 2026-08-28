#ifdef _WIN32
#include "Lyrics.hpp"
#include "LyricsTiming.hpp"
#include "AsyncLyricsLoader.hpp"
#include "BlackoutRenderer.hpp"
#include "Diagnostics.hpp"
#include "MasterDeckSelector.hpp"
#include "TextTexture.hpp"
#include "VideoRenderer.hpp"
#include "vdjVideo8.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <functional>
#include <shellapi.h>

namespace {
int moduleAnchor;
std::filesystem::path PluginDirectory() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleAnchor), &module);
    std::wstring path(32768, L'\0');
    path.resize(GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())));
    return std::filesystem::path(path).parent_path();
}
}

class EmbeddedLyricsPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnLoad() override {
        if (FAILED(DeclareParameterButton(&nextLineButton_, 1, "Next line / tap timestamp", "Next")) ||
            FAILED(DeclareParameterButton(&previousLineButton_, 2, "Previous line", "Prev")) ||
            FAILED(DeclareParameterSlider(&fontSizeParameter_, 3, "Font size", "Size", 1.0f / 3.0f)) ||
            FAILED(DeclareParameterSwitch(&recordTimingParameter_, 4, "Record timing to embedded tags", "Record timing", false)) ||
            FAILED(DeclareParameterSlider(&verticalPositionParameter_, 6, "Vertical position", "Position", 0.5f)) ||
            FAILED(DeclareParameterButton(&editTextButton_, 7, "Edit lyrics TXT", "Edit TXT")) ||
            FAILED(DeclareParameterSlider(&pageLinesParameter_, 8, "Untimed lines", "Untimed lines", 2.0f / 7.0f)) ||
            FAILED(DeclareParameterSlider(&timedLinesParameter_, 9, "Timed lines", "Timed lines", 2.0f / 7.0f))
#ifdef EMBEDDED_LYRICS_MASTER
            || FAILED(DeclareParameterSwitch(&useVolumeFadersParameter_, 10, "Use volume faders", "Upfaders", true))
#endif
            ) return E_FAIL;
        Diagnostics::Info(L"Embedded Lyrics loaded");
        return S_OK;
    }
    HRESULT VDJ_API OnParameter(int id) override {
        if (id == 1 && nextLineButton_) { AdvanceUntimedLine(); nextLineButton_ = 0; }
        else if (id == 2 && previousLineButton_) {
            if (!lyrics_.synchronized && activeLine_ > 0) BeginUntimedScroll(activeLine_ - 1);
            previousLineButton_ = 0;
        } else if (id == 4) {
            recordedTimes_.assign(lyrics_.lines.size(), -1);
            recordingNextLine_ = 0;
            if (recordTimingParameter_ && !lyrics_.synchronized) activeLine_ = 0;
        } else if (id == 7 && editTextButton_) { OpenTextEditor(); editTextButton_ = 0; }
        return S_OK;
    }
    HRESULT VDJ_API OnGetParameterString(int id, char* output, int outputSize) override {
        if (!output || outputSize <= 0) return E_NOTIMPL;
        int percent = 0;
        if (id == 3) percent = static_cast<int>(FontScale() * 100.0f + 0.5f);
        else if (id == 6) percent = static_cast<int>(VerticalPosition() * 100.0f + 0.5f);
        else if (id == 8) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%zu", PageSize());
            return S_OK;
        } else if (id == 9) {
            std::snprintf(output, static_cast<std::size_t>(outputSize), "%zu", TimedLineCount());
            return S_OK;
        } else return E_NOTIMPL;
        std::snprintf(output, static_cast<std::size_t>(outputSize), "%d%%", percent);
        return S_OK;
    }
    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
#ifdef EMBEDDED_LYRICS_MASTER
        info->PluginName = "Embedded Lyrics Master";
#else
        info->PluginName = "Embedded Lyrics Deck";
#endif
        info->Author = "Slava / OpenAI";
        info->Description = "Timed embedded/LRC lyrics and manual untimed lyrics pages";
        info->Version = "0.2.0-rc4";
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_OUTPUTRESOLUTION;
#ifdef EMBEDDED_LYRICS_MASTER
        info->Flags |= VDJFLAG_VIDEO_MASTERONLY | VDJFLAG_VIDEO_OVERLAY;
#else
        info->Flags |= VDJFLAG_VIDEO_VISUALISATION;
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
        if (!texture_.Initialize(device_) || !renderer_.Initialize(device_)
#ifndef EMBEDDED_LYRICS_MASTER
            || !backgroundRenderer_.Initialize(device_)
#endif
            ) {
            Diagnostics::Error(L"DirectX 11 lyrics renderer initialization failed");
            return E_FAIL;
        }
        Diagnostics::Info(L"DirectX 11 lyrics renderer initialized");
        return S_OK;
    }
    HRESULT VDJ_API OnDeviceClose() override {
#ifndef EMBEDDED_LYRICS_MASTER
        backgroundRenderer_.Reset();
#endif
        renderer_.Reset(); texture_.Reset(); device_ = nullptr; return S_OK;
    }
    HRESULT VDJ_API OnDraw() override {
        TryCommitPendingRecording();
#ifndef EMBEDDED_LYRICS_MASTER
        if (!backgroundRenderer_.Draw()) Diagnostics::Error(L"Failed to render audio-only background");
#endif
#ifdef EMBEDDED_LYRICS_MASTER
        const auto deck = VisibleVideoDeck();
#else
        const auto deck = PluginDeck();
#endif
        currentDeck_ = deck;
        if (deck <= 0) {
#ifndef EMBEDDED_LYRICS_MASTER
            if (!texture_.UpdateMessage(L"Zapni Deck verzi ve Video FX konkretniho decku",
                                        width, height, FontScale(), VerticalPosition()) ||
                !renderer_.Draw(texture_.View())) {
                Diagnostics::Error(L"Failed to render deck-placement hint");
            }
#endif
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
            lyricsLoadFinished_ = false;
            texture_.Reset();
            return S_OK;
        }
        if (!path.empty() && path != loadedPath_) {
            loadedPath_ = path;
            texture_.Reset();
            lyrics_ = {};
            lyricsLoadFinished_ = false;
            activeLine_ = 0;
            untimedScrollActive_ = false;
            recordTimingParameter_ = 0;
            recordedTimes_.clear();
            recordingNextLine_ = 0;
            CaptureTextTimestamp();
            loader_.Request(path);
            Diagnostics::Info(L"Queued asynchronous lyrics load: " + path.wstring());
        }
        if (auto completed = loader_.Poll(); completed && completed->path == loadedPath_) {
            lyrics_ = std::move(completed->result.document);
            lyricsLoadFinished_ = true;
            if (lyrics_.empty()) {
                Diagnostics::Error(L"No lyrics found for: " + loadedPath_.wstring() + L"; " + completed->result.error);
            } else {
                Diagnostics::Info(L"Lyrics loaded: " + loadedPath_.wstring());
            }
        }
        CheckTextChanges();
        if (lyrics_.empty()) {
            if (lyricsLoadFinished_ &&
                (!texture_.UpdateMessage(L"...", width, height, FontScale(), VerticalPosition()) ||
                 !renderer_.Draw(texture_.View()))) {
                Diagnostics::Error(L"Failed to render missing-lyrics indication");
            }
            return S_OK;
        }
        if (!lyrics_.synchronized) {
            if (!UpdateUntimedRibbon() || !renderer_.Draw(texture_.View()))
                Diagnostics::Error(L"Failed to render untimed lyrics ribbon");
            return S_OK;
        }
        double elapsedMs = 0.0;
        std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", deck);
        if (FAILED(GetInfo(command, &elapsedMs))) {
            Diagnostics::Error(L"VirtualDJ elapsed-time query failed");
            return S_OK;
        }
        UpdateVisible(static_cast<std::int64_t>(elapsedMs));
        if (!renderer_.Draw(texture_.View())) Diagnostics::Error(L"Failed to render synchronized lyrics");
        return S_OK;
    }

private:
#ifndef EMBEDDED_LYRICS_MASTER
    int PluginDeck() {
        double deck = 0.0;
        if (SUCCEEDED(GetInfo("get_deck", &deck)) && deck > 0.0)
            return static_cast<int>(deck);
        if (SUCCEEDED(GetInfo("get_plugindeck", &deck)) && deck > 0.0)
            return static_cast<int>(deck);
        if (SUCCEEDED(GetInfo("get_activedeck", &deck)) && deck > 0.0)
            return static_cast<int>(deck);
        return 0;
    }
#else
    int VisibleVideoDeck() {
        double balance = 0.0, leftDeck = 1.0, rightDeck = 2.0;
        if (FAILED(GetInfo("get_leftdeck", &leftDeck))) leftDeck = 1.0;
        if (FAILED(GetInfo("get_rightdeck", &rightDeck))) rightDeck = 2.0;
        const char* balanceQuery = useVolumeFadersParameter_
            ? "get_crossfader_result" : "video_crossfader";
        if (FAILED(GetInfo(balanceQuery, &balance))) return masterDeckSelector_.Current();
        return masterDeckSelector_.Select(balance, static_cast<int>(leftDeck), static_cast<int>(rightDeck));
    }
#endif

    void UpdateVisible(std::int64_t now) {
        if (lyrics_.lines.empty()) return;

        struct DisplayLine {
            std::wstring text;
            std::int64_t timeMs{};
            bool pause{};
        };
        constexpr std::int64_t scrollDuration = 650;
        std::vector<DisplayLine> timeline;
        timeline.reserve(lyrics_.lines.size() * 2);
        for (std::size_t i = 0; i < lyrics_.lines.size(); ++i) {
            const auto& lyric = lyrics_.lines[i];
            timeline.push_back({lyric.text, lyric.timeMs, false});
            if (i + 1 >= lyrics_.lines.size()) continue;
            const auto nextTime = lyrics_.lines[i + 1].timeMs;
            const auto highlight = std::min(EstimateLyricHighlightMs(lyric.text),
                std::max<std::int64_t>(500, nextTime - lyric.timeMs - scrollDuration));
            const auto pauseStart = lyric.timeMs + highlight + 700;
            if (nextTime - pauseStart > 2500) timeline.push_back({L"", pauseStart, true});
        }

        const auto it = std::upper_bound(timeline.begin(), timeline.end(), now,
            [](std::int64_t value, const DisplayLine& line) { return value < line.timeMs; });
        const auto active = it == timeline.begin() ? 0u
            : static_cast<std::size_t>(std::distance(timeline.begin(), it) - 1);
        const auto start = timeline[active].timeMs;
        const auto next = active + 1 < timeline.size() ? timeline[active + 1].timeMs : start + 5000;
        const auto interval = std::max<std::int64_t>(1, next - start);

        constexpr std::size_t previousLineCount = 3;
        const auto visibleStart = active > previousLineCount ? active - previousLineCount : 0u;
        const auto visibleEnd = std::min(timeline.size(), active + TimedLineCount());
        std::vector<std::wstring> visibleLines;
        visibleLines.reserve(visibleEnd - visibleStart);
        bool countdownVisible = false;
        for (auto i = visibleStart; i < visibleEnd; ++i) {
            if (timeline[i].pause && i == active) {
                const int maximumSeconds = interval >= 10000 ? 10 : 5;
                const auto countdown = LyricCountdown(next - now, maximumSeconds);
                countdownVisible = countdown >= 1;
                visibleLines.push_back(countdownVisible ? std::to_wstring(countdown) : L"");
            } else {
                visibleLines.push_back(timeline[i].text);
            }
        }

        const auto activeOffset = active - visibleStart;
        float highlightProgress = countdownVisible ? 1.0f : 0.0f;
        if (!timeline[active].pause) {
            const auto highlightDuration = std::min(EstimateLyricHighlightMs(timeline[active].text),
                std::max<std::int64_t>(500, interval - scrollDuration));
            highlightProgress = UnitProgress(now, start, highlightDuration);
        }
        const auto scrollProgress = timeline[active].pause
            ? UnitProgress(now, next - scrollDuration, scrollDuration)
            : UnitProgress(now, start, interval);
        if (!texture_.UpdateTimed(visibleLines, activeOffset, highlightProgress, scrollProgress,
                                  width, height, FontScale(), VerticalPosition()))
            Diagnostics::Error(L"Failed to update lyrics texture");
    }

    float FontScale() const noexcept {
        return 0.5f + std::clamp(fontSizeParameter_, 0.0f, 1.0f) * 1.5f;
    }

    float VerticalPosition() const noexcept {
        return 0.1f + std::clamp(verticalPositionParameter_, 0.0f, 1.0f) * 0.8f;
    }

    std::size_t PageSize() const noexcept {
        return 5 + static_cast<std::size_t>(std::clamp(pageLinesParameter_, 0.0f, 1.0f) * 7.0f + 0.5f);
    }
    std::size_t TimedLineCount() const noexcept {
        return 5 + static_cast<std::size_t>(std::clamp(timedLinesParameter_, 0.0f, 1.0f) * 7.0f + 0.5f);
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
        const auto first = renderActive > 3 ? renderActive - 3 : 0u;
        const auto end = std::min(lyrics_.lines.size(), renderActive + PageSize());
        std::vector<std::wstring> visible; visible.reserve(end - first);
        for (auto i = first; i < end; ++i) visible.push_back(lyrics_.lines[i].text);
        return texture_.UpdateTimed(visible, renderActive - first, 1.0f, scroll,
                                    width, height, FontScale(), VerticalPosition());
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
    static std::string Utf8(const std::wstring& value) {
        if (value.empty()) return {};
        const auto size = WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        std::string result(static_cast<std::size_t>(size),'\0');
        WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);
        return result;
    }
    void QueueTimingRecording() {
        auto extension = loadedPath_.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
        if (extension != L".mp3") return;
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
    void TryCommitPendingRecording() {
        if (!pendingTimingWrite_ || TrackLoadedAnywhere(pendingAudioPath_)) return;
        const auto script = PluginDirectory() / L"EmbeddedLyricsTagWriter.py";
        std::wstring command = L"py.exe \""+script.wstring()+L"\" --write-recording \""+pendingAudioPath_.wstring()+L"\" \""+pendingTimingPath_.wstring()+L"\"";
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return;
        CloseHandle(process.hThread); CloseHandle(process.hProcess); pendingTimingWrite_=false;
        Diagnostics::Info(L"Queued embedded SYLT and SYNCEDLYRICS write");
    }

    std::filesystem::path TextPath() const {
        auto path = loadedPath_;
        path.replace_extension(L".txt");
        return path;
    }

    void CaptureTextTimestamp() {
        std::error_code error;
        const auto timestamp = std::filesystem::last_write_time(TextPath(), error);
        textWriteTime_ = error ? std::optional<std::filesystem::file_time_type>{} : timestamp;
        nextTextCheck_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }

    void CheckTextChanges() {
        if (loadedPath_.empty() || std::chrono::steady_clock::now() < nextTextCheck_) return;
        nextTextCheck_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        std::error_code error;
        const auto timestamp = std::filesystem::last_write_time(TextPath(), error);
        const std::optional<std::filesystem::file_time_type> current = error
            ? std::optional<std::filesystem::file_time_type>{} : timestamp;
        if (current == textWriteTime_) return;
        textWriteTime_ = current;
        loader_.Request(loadedPath_);
        Diagnostics::Info(L"TXT change detected; queued lyrics reload");
    }

    void OpenTextEditor() {
        if (loadedPath_.empty()) {
            Diagnostics::Error(L"Cannot edit TXT because no track is loaded");
            return;
        }
        const auto path = TextPath();
        if (!std::filesystem::exists(path)) {
            std::ofstream create{path, std::ios::binary};
            if (!create) {
                Diagnostics::Error(L"Cannot create lyrics TXT: " + path.wstring());
                return;
            }
        }
        const std::wstring parameters = L"\"" + path.wstring() + L"\"";
        const auto result = reinterpret_cast<std::intptr_t>(
            ShellExecuteW(nullptr, L"open", L"notepad.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL));
        if (result <= 32) Diagnostics::Error(L"Cannot open lyrics TXT editor: " + path.wstring());
    }

    ID3D11Device* device_{};
#ifndef EMBEDDED_LYRICS_MASTER
    BlackoutRenderer backgroundRenderer_;
#endif
    TextTexture texture_;
    VideoRenderer renderer_;
    AsyncLyricsLoader loader_;
    std::filesystem::path loadedPath_;
    LyricsDocument lyrics_;
    bool lyricsLoadFinished_{};
    int nextLineButton_{}; int previousLineButton_{};
    float fontSizeParameter_{1.0f / 3.0f}; int recordTimingParameter_{};
    float verticalPositionParameter_{0.5f}; int editTextButton_{};
    float pageLinesParameter_{2.0f / 7.0f}; float timedLinesParameter_{2.0f / 7.0f};
    std::size_t activeLine_{}; std::size_t scrollFromLine_{}; bool untimedScrollActive_{};
    std::chrono::steady_clock::time_point untimedScrollStarted_{};
    std::vector<std::int64_t> recordedTimes_; std::size_t recordingNextLine_{}; int currentDeck_{};
    bool pendingTimingWrite_{}; std::filesystem::path pendingAudioPath_, pendingTimingPath_;
    std::optional<std::filesystem::file_time_type> textWriteTime_;
    std::chrono::steady_clock::time_point nextTextCheck_{};
#ifdef EMBEDDED_LYRICS_MASTER
    MasterDeckSelector masterDeckSelector_;
    int useVolumeFadersParameter_{1};
#endif
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
