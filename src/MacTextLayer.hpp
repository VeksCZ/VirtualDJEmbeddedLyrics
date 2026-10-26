#pragma once
#ifdef __APPLE__
#import <Metal/Metal.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// macOS counterpart of TextTexture (Windows): renders the lyric timeline, a one-off message
// or the "Artist - Title" banner into a BGRA bitmap with CoreText/AppKit and draws it as a
// full-frame Metal quad. Rendering is cached by a key, so unchanged frames cost nothing.
// Like on Windows, every lyric line is drawn once (on a background queue) and frames are only
// composed from those cached line bitmaps, so highlighting/scrolling never runs AppKit text
// drawing on VirtualDJ's render thread.
struct MacRgb { double red{1}, green{1}, blue{1}; };

struct MacTextStyle {
    int font{};            // index into the same font list as Windows (Arial, Segoe UI, ...)
    int backdrop{};        // 0 outline, 1 shadow, 2 outline + shadow
    int strength{1};       // 0 thin, 1 normal, 2 strong
    MacRgb text{1, 1, 1};
    MacRgb highlight{1, 0.82, 0};
    MacRgb read{0.59, 0.59, 0.59};
    bool background{};
    MacRgb backgroundColor{0, 0, 0};
};

class MacTextLayer {
public:
    void Reset();
    // Same contract as TextTexture::UpdateTimed: `lines` is the whole candidate timeline,
    // `targetLines` the configured number of fully visible lines (fade band is derived from it).
    bool UpdateTimed(id<MTLDevice> device, const std::vector<std::wstring>& lines, std::size_t activeLine,
                     float highlightProgress, float scrollProgress, int width, int height,
                     float fontScale, float verticalPosition, const std::vector<bool>& subdued,
                     const MacTextStyle& style, std::size_t targetLines);
    bool UpdateMessage(id<MTLDevice> device, const std::wstring& message, int width, int height,
                       float fontScale, float verticalPosition, const MacTextStyle& style);
    bool UpdateBanner(id<MTLDevice> device, const std::wstring& text, int width, int height,
                      const MacTextStyle& style);
    bool Draw(id<MTLRenderCommandEncoder> encoder);

    MacTextLayer();
    ~MacTextLayer();
    MacTextLayer(const MacTextLayer&) = delete;
    MacTextLayer& operator=(const MacTextLayer&) = delete;

private:
    struct Lines;                       // line cache + background renderer (MacTextLayer.mm)
    std::shared_ptr<Lines> lines_;
    bool missingLines_{};
    std::vector<std::uint8_t> frame_;
    bool Upload(id<MTLDevice> device, const std::vector<std::uint8_t>& pixels, int width, int height,
                std::wstring key);

    id<MTLDevice> __strong device_ = nil;
    id<MTLDevice> __strong pipelineDevice_ = nil;
    id<MTLTexture> __strong texture_ = nil;
    id<MTLRenderPipelineState> __strong pipeline_ = nil;
    std::wstring cacheKey_;
};
#endif
