#ifdef _WIN32
#include "Lyrics.hpp"
#include "BlackoutRenderer.hpp"
#include "MasterDeckSelector.hpp"
#include "TextTexture.hpp"
#include "VideoRenderer.hpp"
#include "vdjVideo8.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

class EmbeddedLyricsBasicPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnLoad() override {
        if (FAILED(DeclareParameterButton(&nextPageButton_, 1, "Next lyrics page", "Next")) ||
            FAILED(DeclareParameterButton(&previousPageButton_, 2, "Previous lyrics page", "Prev"))
#ifdef EMBEDDED_LYRICS_MASTER
            || FAILED(DeclareParameterSwitch(&useVolumeFadersParameter_, 3,
                                             "Upfaders", "Upfaders", false))
#endif
            ) return E_FAIL;
        return S_OK;
    }

    HRESULT VDJ_API OnParameter(int id) override {
        if (id == 1 && nextPageButton_) {
            const auto pages = std::max<std::size_t>(1, (lyrics_.lines.size() + pageSize_ - 1) / pageSize_);
            page_ = std::min(page_ + 1, pages - 1);
            nextPageButton_ = 0;
        } else if (id == 2 && previousPageButton_) {
            if (page_ > 0) --page_;
            previousPageButton_ = 0;
        }
        return S_OK;
    }

    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
#ifdef EMBEDDED_LYRICS_MASTER
        info->PluginName = "LRC Master Basic";
#else
        info->PluginName = "LRC Deck Basic";
#endif
        info->Author = "Slava / OpenAI";
        info->Description = "Minimal embedded/LRC/TXT lyrics overlay";
        info->Version = "0.2.0-basic";
#ifdef EMBEDDED_LYRICS_MASTER
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_OUTPUTRESOLUTION |
                      VDJFLAG_VIDEO_MASTERONLY | VDJFLAG_VIDEO_OVERLAY;
#else
        info->Flags = VDJFLAG_VIDEO_VISUALISATION;
#endif
        info->Bitmap = nullptr;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }

    HRESULT VDJ_API OnDeviceInit() override {
        if (FAILED(GetDevice(VdjVideoEngineDirectX11, reinterpret_cast<void**>(&device_))) || !device_) return E_FAIL;
        return texture_.Initialize(device_) && renderer_.Initialize(device_)
#ifndef EMBEDDED_LYRICS_MASTER
            && backgroundRenderer_.Initialize(device_)
#endif
            ? S_OK : E_FAIL;
    }

    HRESULT VDJ_API OnDeviceClose() override {
#ifndef EMBEDDED_LYRICS_MASTER
        backgroundRenderer_.Reset();
#endif
        renderer_.Reset();
        texture_.Reset();
        device_ = nullptr;
        return S_OK;
    }

    HRESULT VDJ_API OnDraw() override {
#ifndef EMBEDDED_LYRICS_MASTER
        backgroundRenderer_.Draw();
#endif
#ifdef EMBEDDED_LYRICS_MASTER
        const int deck = VisibleVideoDeck();
#else
        const int deck = PluginDeck();
#endif
        if (deck <= 0) return S_OK;
        char pathBuffer[4096]{};
        char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
        if (FAILED(GetStringInfo(command, pathBuffer, sizeof(pathBuffer)))) return S_OK;
        const std::filesystem::path path{std::u8string{reinterpret_cast<const char8_t*>(pathBuffer)}};
        if (path.empty()) {
            loadedPath_.clear();
            lyrics_ = {};
            texture_.Reset();
            return S_OK;
        }
        if (path != loadedPath_) {
            loadedPath_ = path;
            texture_.Reset();
            lyrics_ = LoadLyricsForTrack(path).document;
            page_ = 0;
        }
        if (lyrics_.empty()) return S_OK;
        if (!lyrics_.synchronized) {
            std::vector<std::wstring> lines;
            lines.reserve(lyrics_.lines.size());
            for (const auto& line : lyrics_.lines) lines.push_back(line.text);
            texture_.UpdatePage(lines, page_, pageSize_, static_cast<std::size_t>(-1), width, height);
            renderer_.Draw(texture_.View());
            return S_OK;
        }
        double elapsedMs = 0.0;
        std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", deck);
        if (FAILED(GetInfo(command, &elapsedMs))) return S_OK;
        UpdateVisible(static_cast<std::int64_t>(elapsedMs));
        renderer_.Draw(texture_.View());
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
        GetInfo("get_leftdeck", &leftDeck);
        GetInfo("get_rightdeck", &rightDeck);
        const char* balanceQuery = useVolumeFadersParameter_
            ? "get_crossfader_result" : "video_crossfader";
        if (FAILED(GetInfo(balanceQuery, &balance))) return masterDeckSelector_.Current();
        return masterDeckSelector_.Select(balance, static_cast<int>(leftDeck), static_cast<int>(rightDeck));
    }
#endif

    void UpdateVisible(std::int64_t now) {
        const auto it = std::upper_bound(lyrics_.lines.begin(), lyrics_.lines.end(), now,
            [](std::int64_t value, const LyricLine& line) { return value < line.timeMs; });
        const auto index = it == lyrics_.lines.begin() ? 0u
            : static_cast<std::size_t>(std::distance(lyrics_.lines.begin(), it) - 1);
        const auto end = index + 1 < lyrics_.lines.size() ? lyrics_.lines[index + 1].timeMs
                                                         : lyrics_.lines[index].timeMs + 5000;
        const auto duration = std::max<std::int64_t>(1, end - lyrics_.lines[index].timeMs);
        const auto progress = static_cast<float>(now - lyrics_.lines[index].timeMs) / static_cast<float>(duration);
        texture_.Update(lyrics_.lines[index].text,
            index + 1 < lyrics_.lines.size() ? lyrics_.lines[index + 1].text : L"",
            progress, width, height);
    }

    ID3D11Device* device_{};
#ifndef EMBEDDED_LYRICS_MASTER
    BlackoutRenderer backgroundRenderer_;
#endif
    TextTexture texture_;
    VideoRenderer renderer_;
    std::filesystem::path loadedPath_;
    LyricsDocument lyrics_;
    int nextPageButton_{};
    int previousPageButton_{};
    std::size_t page_{};
    static constexpr std::size_t pageSize_ = 10;
#ifdef EMBEDDED_LYRICS_MASTER
    MasterDeckSelector masterDeckSelector_;
    int useVolumeFadersParameter_{};
#endif
};

STDAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, LPVOID* object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (std::memcmp(&classId, &CLSID_VdjPlugin8, sizeof(GUID)) != 0 ||
        std::memcmp(&interfaceId, &IID_IVdjPluginVideoFx8, sizeof(GUID)) != 0) return CLASS_E_CLASSNOTAVAILABLE;
    *object = new EmbeddedLyricsBasicPlugin();
    return S_OK;
}
#endif
