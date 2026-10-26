#pragma once

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>


class TextTexture {
public:
    TextTexture() = default;
    TextTexture(const TextTexture&) = delete;
    TextTexture& operator=(const TextTexture&) = delete;
    ~TextTexture();
    bool Initialize(ID3D11Device* device);
    void Reset();
    bool Update(const std::wstring& current, const std::wstring& next,
                float progress, int width, int height, float fontScale = 1.0f,
                float verticalPosition = 0.5f);
    // `lines` is a candidate list around activeLine, wider than what actually needs to be drawn --
    // pass as much surrounding context as you reasonably have (e.g. a whole song's timeline).
    // targetLines is the fixed, user-configured number of fully-opaque lines; the anchor and the
    // top/bottom fade band are derived from targetLines/height/fontScale alone, never from which
    // particular candidate lines end up on screen or how they wrap, so neither can shift when a
    // line happens to wrap to two rows. How many candidates actually get drawn is then computed
    // from their real (possibly wrapped) heights so the drawn block still fully covers the frame.
    // targetLines of 0 means "use lines.size()", matching the old fixed-window behavior.
    bool UpdateTimed(const std::vector<std::wstring>& lines, std::size_t activeLine,
                     float highlightProgress, float scrollProgress, int width, int height,
                     float fontScale = 1.0f, float verticalPosition = 0.5f,
                     const std::vector<bool>& subduedLines = {},
                     std::uint32_t textColor = 0x00ffffffu,
                     std::uint32_t highlightColor = 0x0000d2ffu,
                     std::uint32_t readColor = 0x00969696u,
                     int fontFamily = 0, int backdropStyle = 0,
                     int backdropStrength = 1, bool backgroundEnabled = false,
                     std::uint32_t backgroundColor = 0x00000000u,
                     std::size_t targetLines = 0);

    bool UpdateMessage(const std::wstring& message, int width, int height,
                       float fontScale = 1.0f, float verticalPosition = 0.5f,
                       bool backgroundEnabled = false,
                       std::uint32_t backgroundColor = 0x00000000u);

    // A single, non-scrolling line (e.g. "Artist - Title") anchored near the top of the frame,
    // independent of the scrolling lyrics texture. The font size is chosen so the whole line
    // always fits within the frame width; it never wraps to a second line.
    bool UpdateBanner(const std::wstring& text, int width, int height,
                      std::uint32_t textColor = 0x00ffffffu, int fontFamily = 0,
                      int backdropStyle = 0, int backdropStrength = 1);
    ID3D11ShaderResourceView* View() const noexcept { return view_.Get(); }

private:
    struct WrappedLine { std::vector<std::wstring> rows; std::vector<int> widths; };
    // One lyrics line (all its wrapped rows) rendered once with outline/shadow, alpha finalized.
    struct LineBitmap { int width{}, height{}, originX{}, originY{}; std::vector<std::uint32_t> pixels; };
    using LinePtr = std::shared_ptr<const LineBitmap>;
    // Everything needed to render one line without touching this object's GDI state, so the
    // prefetch worker can do it off the render thread.
    struct LineJob {
        std::wstring key, text;
        std::vector<std::wstring> rows;
        std::uint32_t color{};
        int fontSize{}, spacing{}, maxTextWidth{}, fontFamily{}, backdropStyle{}, backdropStrength{};
        unsigned generation{};
    };
    static LineBitmap RenderLineBitmap(const LineJob& job);
    LineJob MakeJob(const std::wstring& text, const WrappedLine& wrapped, std::uint32_t color) const;
    void Prefetch(LineJob job);
    void WorkerLoop();
    bool PrepareStyle(int height, int fontSize, int maxTextWidth, int fontFamily,
                      int backdropStyle, int backdropStrength);
    void ReleaseStyle();
    const WrappedLine& Wrap(const std::wstring& text, int maxTextWidth);
    LinePtr RenderLine(const std::wstring& text, const WrappedLine& wrapped, std::uint32_t color);

    HDC measureDc_{};
    HFONT font_{};
    HGDIOBJ measureOldFont_{};
    std::wstring styleKey_;
    std::unordered_map<std::wstring, WrappedLine> wrapCache_;
    // Style of the current lines (set by PrepareStyle), used to build LineJobs.
    int fontSize_{}, spacing_{}, maxTextWidth_{}, fontFamily_{}, backdropStyle_{}, backdropStrength_{};
    std::mutex cacheMutex_;               // guards lineCache_, jobs_, queued_, generation_, stop_
    std::condition_variable wake_;
    std::unordered_map<std::wstring, LinePtr> lineCache_;
    std::deque<LineJob> jobs_;
    std::unordered_set<std::wstring> queued_;
    unsigned generation_{};
    bool stop_{};
    std::thread worker_;
    std::vector<std::uint32_t> frame_;
    bool dynamic_{};
    bool missingLines_{};   // last frame left out lines still being rendered
    int textureWidth_{}, textureHeight_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    std::wstring cacheKey_;
};
#endif
