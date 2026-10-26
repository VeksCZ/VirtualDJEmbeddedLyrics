#include "TextTexture.hpp"

#ifdef _WIN32
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <numeric>
#include <sstream>
#include <vector>

namespace {
void DestroyCanvas(HDC dc, HBITMAP bitmap, HGDIOBJ oldBitmap) {
    if (dc && oldBitmap && oldBitmap != HGDI_ERROR) SelectObject(dc, oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
}

void DrawStyledText(HDC dc, int x, int y, const std::wstring& text, COLORREF color,
                    int backdropStyle, int backdropStrength) {
    const int radius = std::clamp(backdropStrength, 0, 2) + 2;
    SetTextColor(dc, RGB(1, 1, 1));
    if (backdropStyle == 1 || backdropStyle == 2) {
        const int offset = radius + 2;
        for (int ox = -1; ox <= 1; ++ox)
            for (int oy = -1; oy <= 1; ++oy)
                TextOutW(dc, x + offset + ox, y + offset + oy,
                         text.c_str(), static_cast<int>(text.size()));
    }
    if (backdropStyle == 0 || backdropStyle == 2) {
        const int inner = (radius - 1) * (radius - 1);
        const int outer = radius * radius + radius;
        for (int ox = -radius; ox <= radius; ++ox)
            for (int oy = -radius; oy <= radius; ++oy) {
                const int distance = ox * ox + oy * oy;
                if (distance >= inner && distance <= outer)
                    TextOutW(dc, x + ox, y + oy, text.c_str(), static_cast<int>(text.size()));
            }
    }
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
}

const wchar_t* FontName(int fontFamily) {
    static constexpr const wchar_t* names[] = {
        L"Arial", L"Segoe UI", L"Verdana", L"Tahoma", L"Trebuchet MS", L"Calibri"
    };
    return names[std::clamp(fontFamily, 0, 5)];
}

int TextWidth(HDC dc, const std::wstring& text) {
    SIZE extent{};
    return GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent) ? extent.cx : 0;
}

std::vector<std::wstring> WrapText(HDC dc, const std::wstring& text, int maxWidth) {
    std::vector<std::wstring> result;
    std::wistringstream words{text};
    std::wstring word, current;
    while (words >> word) {
        const auto candidate = current.empty() ? word : current + L' ' + word;
        if (TextWidth(dc, candidate) <= maxWidth) {
            current = candidate;
            continue;
        }
        if (!current.empty()) { result.push_back(std::move(current)); current.clear(); }
        while (TextWidth(dc, word) > maxWidth && word.size() > 1) {
            std::size_t count = 1;
            while (count < word.size() && TextWidth(dc, word.substr(0, count + 1)) <= maxWidth) ++count;
            result.push_back(word.substr(0, count));
            word.erase(0, count);
        }
        current = std::move(word);
    }
    if (!current.empty()) result.push_back(std::move(current));
    if (result.empty()) result.emplace_back();
    return result;
}

void FinalizeAlpha(void* pixels, std::size_t count) {
    auto* values = static_cast<std::uint32_t*>(pixels);
    for (std::size_t i = 0; i < count; ++i) {
        const auto rgb = values[i] & 0x00ffffffu;
        if (!rgb) { values[i] = 0; continue; }
        const auto b = rgb & 0xffu, g = (rgb >> 8) & 0xffu, r = (rgb >> 16) & 0xffu;
        const auto alpha = std::max<std::uint32_t>(150, std::max(r, std::max(g, b)));
        values[i] = rgb | (alpha << 24);
    }
}

std::uint32_t DibColor(std::uint32_t colorRef) {
    return ((colorRef & 0x000000ffu) << 16) |
           (colorRef & 0x0000ff00u) |
           ((colorRef & 0x00ff0000u) >> 16);
}
}

bool TextTexture::Initialize(ID3D11Device* device) {
    device_ = device;
    return device_ != nullptr;
}

void TextTexture::Reset() {
    texture_.Reset();
    view_.Reset();
    cacheKey_.clear();
    dynamic_ = false;
    textureWidth_ = textureHeight_ = 0;
    ReleaseStyle();
}

TextTexture::~TextTexture() {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        stop_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    ReleaseStyle();
}

bool TextTexture::Update(const std::wstring& current, const std::wstring& next,
                         float progress, int width, int height, float fontScale, float verticalPosition) {
    std::vector<std::wstring> lines{current};
    if (!next.empty()) lines.push_back(next);
    return UpdateTimed(lines, 0, progress, progress, width, height, fontScale, verticalPosition);
}

// The expensive part of a lyrics frame is GDI: every row is drawn 10-40 times for its outline
// and shadow. That work only depends on a line's text, color and the style, so each line is
// rendered once into its own small bitmap (LineBitmap) and reused; a frame is then just these
// bitmaps copied into place, with the scroll offset, the highlight clip, the fade band and the
// background applied while copying. The result is the same picture the old one-pass renderer
// produced, at a fraction of the cost, and the texture is a reused DYNAMIC one instead of a
// new IMMUTABLE texture per frame.
bool TextTexture::PrepareStyle(int height, int fontSize, int maxTextWidth, int fontFamily,
                               int backdropStyle, int backdropStrength) {
    const std::wstring styleKey = std::to_wstring(fontSize) + L":" + std::to_wstring(maxTextWidth) + L":" +
        std::to_wstring(fontFamily) + L":" + std::to_wstring(backdropStyle) + L":" +
        std::to_wstring(backdropStrength) + L":" + std::to_wstring(height);
    if (styleKey == styleKey_ && measureDc_ && font_) return true;
    ReleaseStyle();
    font_ = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, FontName(fontFamily));
    measureDc_ = CreateCompatibleDC(nullptr);
    if (!font_ || !measureDc_) { ReleaseStyle(); return false; }
    measureOldFont_ = SelectObject(measureDc_, font_);
    if (!measureOldFont_ || measureOldFont_ == HGDI_ERROR) { measureOldFont_ = nullptr; ReleaseStyle(); return false; }
    styleKey_ = styleKey;
    fontSize_ = fontSize; spacing_ = fontSize * 6 / 5; maxTextWidth_ = maxTextWidth;
    fontFamily_ = fontFamily; backdropStyle_ = backdropStyle; backdropStrength_ = backdropStrength;
    return true;
}

void TextTexture::ReleaseStyle() {
    if (measureDc_) {
        if (measureOldFont_) SelectObject(measureDc_, measureOldFont_);
        DeleteDC(measureDc_);
    }
    if (font_) DeleteObject(font_);
    measureDc_ = nullptr; measureOldFont_ = nullptr; font_ = nullptr;
    styleKey_.clear();
    wrapCache_.clear();
    std::lock_guard<std::mutex> lock(cacheMutex_);
    ++generation_;      // results of jobs already running for the old style are dropped
    jobs_.clear();
    queued_.clear();
    lineCache_.clear();
}

const TextTexture::WrappedLine& TextTexture::Wrap(const std::wstring& text, int maxTextWidth) {
    if (auto found = wrapCache_.find(text); found != wrapCache_.end()) return found->second;
    WrappedLine wrapped;
    wrapped.rows = WrapText(measureDc_, text, maxTextWidth);
    for (const auto& row : wrapped.rows) wrapped.widths.push_back(TextWidth(measureDc_, row));
    return wrapCache_.emplace(text, std::move(wrapped)).first->second;
}

TextTexture::LineBitmap TextTexture::RenderLineBitmap(const LineJob& job) {
    LineBitmap line;
    const int pad = job.fontSize / 2 + 16;  // room for ascenders/descenders, outline and shadow
    line.width = job.maxTextWidth + 2 * pad;
    line.originX = line.width / 2;
    line.originY = job.fontSize + pad;
    const int rows = std::max(1, static_cast<int>(job.rows.size()));
    line.height = line.originY + (rows - 1) * job.spacing + job.fontSize / 2 + pad;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = line.width;
    info.bmiHeader.biHeight = -line.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return {};
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) { DestroyCanvas(dc, bitmap, nullptr); return {}; }
    const auto oldBitmap = SelectObject(dc, bitmap);
    if (!oldBitmap || oldBitmap == HGDI_ERROR) { DestroyCanvas(dc, bitmap, nullptr); return {}; }
    // Own font per call: GDI objects must not be shared between this and the prefetch thread.
    HFONT font = CreateFontW(-job.fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, FontName(job.fontFamily));
    if (!font) { DestroyCanvas(dc, bitmap, oldBitmap); return {}; }
    const std::size_t count = static_cast<std::size_t>(line.width) * line.height;
    std::fill_n(static_cast<std::uint32_t*>(pixels), count, 0u);
    const auto oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_CENTER | TA_BASELINE);
    int y = line.originY;
    for (const auto& row : job.rows) {
        DrawStyledText(dc, line.originX, y, row, static_cast<COLORREF>(job.color), job.backdropStyle,
                       job.backdropStrength);
        y += job.spacing;
    }
    GdiFlush();
    FinalizeAlpha(pixels, count);
    const auto* source = static_cast<const std::uint32_t*>(pixels);
    line.pixels.assign(source, source + count);
    if (oldFont && oldFont != HGDI_ERROR) SelectObject(dc, oldFont);
    DeleteObject(font);
    DestroyCanvas(dc, bitmap, oldBitmap);
    return line;
}

TextTexture::LineJob TextTexture::MakeJob(const std::wstring& text, const WrappedLine& wrapped,
                                          std::uint32_t color) const {
    LineJob job;
    job.key = std::to_wstring(color) + L":" + text;
    job.text = text;
    job.rows = wrapped.rows;
    job.color = color;
    job.fontSize = fontSize_; job.spacing = spacing_; job.maxTextWidth = maxTextWidth_;
    job.fontFamily = fontFamily_; job.backdropStyle = backdropStyle_; job.backdropStrength = backdropStrength_;
    return job;
}

TextTexture::LinePtr TextTexture::RenderLine(const std::wstring& text, const WrappedLine& wrapped,
                                             std::uint32_t color) {
    auto job = MakeJob(text, wrapped, color);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (auto found = lineCache_.find(job.key); found != lineCache_.end()) return found->second;
        if (lineCache_.size() > 256) lineCache_.clear();  // a song has far fewer; just a bound
    }
    // Not ready yet (a new track, a seek, a new countdown number): never draw it on VirtualDJ's
    // render thread -- queue it for the worker and leave it out of this frame. It shows up a
    // frame or two later, which is invisible next to the stutter a 20-100 ms GDI pass causes.
    Prefetch(std::move(job));
    missingLines_ = true;
    return nullptr;
}

void TextTexture::Prefetch(LineJob job) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (stop_ || lineCache_.count(job.key) || queued_.count(job.key)) return;
        job.generation = generation_;
        queued_.insert(job.key);
        jobs_.push_back(std::move(job));
        if (!worker_.joinable()) worker_ = std::thread([this] { WorkerLoop(); });
    }
    wake_.notify_one();
}

void TextTexture::WorkerLoop() {
    // Below-normal priority: it must never compete with VirtualDJ's audio or render threads.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::unique_lock<std::mutex> lock(cacheMutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
        if (stop_) return;
        auto job = std::move(jobs_.front());
        jobs_.pop_front();
        lock.unlock();
        auto line = std::make_shared<const LineBitmap>(RenderLineBitmap(job));
        lock.lock();
        queued_.erase(job.key);
        if (job.generation == generation_ && !line->pixels.empty()) lineCache_[job.key] = std::move(line);
    }
}

bool TextTexture::UpdateTimed(const std::vector<std::wstring>& lines, std::size_t activeLine,
                              float highlightProgress, float scrollProgress, int width, int height,
                              float fontScale, float verticalPosition,
                              const std::vector<bool>& subduedLines,
                              std::uint32_t textColor, std::uint32_t highlightColor,
                              std::uint32_t readColor, int fontFamily,
                              int backdropStyle, int backdropStrength,
                              bool backgroundEnabled, std::uint32_t backgroundColor,
                              std::size_t targetLines) {
    if (!device_ || width <= 0 || height <= 0 || lines.empty() || activeLine >= lines.size()) return false;
    if (targetLines == 0) targetLines = lines.size();
    if (lines.size() == 1) scrollProgress = 0.0f;
    highlightProgress = std::clamp(highlightProgress, 0.0f, 1.0f);
    scrollProgress = std::clamp(scrollProgress, 0.0f, 1.0f);
    fontScale = std::clamp(fontScale, 0.5f, 2.0f);
    verticalPosition = std::clamp(verticalPosition, 0.1f, 0.9f);

    std::wstring key = L"timed:" + std::to_wstring(activeLine) + L':' + std::to_wstring(targetLines) + L':';
    for (const auto subdued : subduedLines) key += subdued ? L'1' : L'0';
    key += L':';
    for (const auto& line : lines) key += line + L'\n';
    key += std::to_wstring(width) + L"x" + std::to_wstring(height) + L":" +
           std::to_wstring(static_cast<int>(highlightProgress * 200)) + L":" +
           std::to_wstring(static_cast<int>(scrollProgress * 200)) + L":" +
           std::to_wstring(static_cast<int>(fontScale * 100)) + L":" +
           std::to_wstring(static_cast<int>(verticalPosition * 100)) + L":" +
           std::to_wstring(textColor) + L":" + std::to_wstring(highlightColor) + L":" +
           std::to_wstring(readColor) + L":" + std::to_wstring(fontFamily) + L":" +
           std::to_wstring(backdropStyle) + L":" + std::to_wstring(backdropStrength) + L":" +
           std::to_wstring(backgroundEnabled) + L":" + std::to_wstring(backgroundColor);
    if (key == cacheKey_ && view_ && !missingLines_) return true;
    missingLines_ = false;

    // Font size follows the user's setting (fontScale) directly; it is not shrunk to force lines
    // onto a single row -- a line is free to wrap to two. The cap against targetLines is just a
    // sanity bound (don't let an absurd number of configured lines demand an absurd font), not an
    // attempt to guarantee no wrapping.
    const int requestedSize = std::max(18, static_cast<int>(std::max(36, height / 13) * fontScale));
    const int fontSize = std::min(requestedSize,
        std::max(8, static_cast<int>(height * 0.97f / (static_cast<float>(targetLines) * 1.2f))));
    const int maxTextWidth = width * 9 / 10;
    const int spacing = fontSize * 6 / 5;
    if (!PrepareStyle(height, fontSize, maxTextWidth, fontFamily, backdropStyle, backdropStrength)) return false;

    // Only wrap what can be on screen: lines far from the active one never matter (wrapping the
    // whole song every frame used to be a cost of its own). Every line is at least one row, so
    // a frame never needs more than height / spacing lines on either side (+ margin).
    const std::size_t span = static_cast<std::size_t>(height / std::max(1, spacing)) + 4;
    const std::size_t firstCandidate = activeLine > span ? activeLine - span : 0;
    const std::size_t lastCandidate = std::min(lines.size(), activeLine + span + 1);
    std::vector<const WrappedLine*> wrapped(lines.size(), nullptr);
    for (std::size_t i = firstCandidate; i < lastCandidate; ++i) wrapped[i] = &Wrap(lines[i], maxTextWidth);
    const auto rowsOf = [&](std::size_t i) { return static_cast<int>(wrapped[i]->rows.size()); };

    // The anchor and the fade band (further down) are derived only from targetLines/height/
    // fontSize/verticalPosition -- fixed settings -- never from which candidate lines happen to
    // be on screen or how many rows they wrap to, so both stay perfectly still frame to frame.
    const std::size_t previousCount = targetLines > 0 ? (targetLines - 1) / 2 : 0;
    const std::size_t inclusiveAfterCount = targetLines > previousCount ? targetLines - previousCount : 1;
    const int reservedBefore = static_cast<int>(previousCount) * spacing;
    const int reservedAfter = static_cast<int>(inclusiveAfterCount) * spacing;
    const int minAnchor = reservedBefore + fontSize;
    const int maxAnchor = std::max(minAnchor, height - reservedAfter + fontSize / 2);
    const int anchor = std::clamp(static_cast<int>(height * verticalPosition) + fontSize / 2,
                                  minAnchor, maxAnchor);
    const int activeHeight = std::max(1, rowsOf(activeLine)) * spacing;

    // How many candidate lines are needed to fill the frame around the anchor, using each one's
    // real (possibly wrapped) row count; the "after" side keeps activeHeight of extra margin so
    // there is always a line ready to scroll up into the fixed fade band.
    std::size_t visibleStart = activeLine, visibleEnd = activeLine + 1;
    for (int covered = 0; visibleStart > firstCandidate && covered < anchor;) {
        --visibleStart;
        covered += rowsOf(visibleStart) * spacing;
    }
    for (int covered = activeHeight;
         visibleEnd < lastCandidate && covered < height - anchor + activeHeight; ++visibleEnd) {
        covered += rowsOf(visibleEnd) * spacing;
    }

    int y = anchor - static_cast<int>(scrollProgress * activeHeight + 0.5f);
    for (std::size_t i = visibleStart; i < activeLine; ++i) y -= std::max(1, rowsOf(i)) * spacing;

    // Fade band: fixed rows, from the settings-only values above.
    const float topFadeWidth = std::min(static_cast<float>(fontSize) / 2.0f + static_cast<float>(spacing),
                                        static_cast<float>(height) / 2.0f);
    const float topZero = std::clamp(
        static_cast<float>(anchor - reservedBefore - fontSize - spacing),
        0.0f, static_cast<float>(height) - topFadeWidth);
    const float topOpaque = topZero + topFadeWidth;
    const float bottomFadeWidth = std::min(static_cast<float>(spacing), static_cast<float>(height) / 2.0f);
    const float bottomZero = std::clamp(
        static_cast<float>(anchor + reservedAfter),
        bottomFadeWidth, static_cast<float>(height));
    const float bottomOpaque = bottomZero - bottomFadeWidth;
    const auto fadeAt = [&](int row) {
        if (row < topOpaque) return std::clamp((row - topZero) / (topOpaque - topZero), 0.0f, 1.0f);
        if (row > bottomOpaque) return std::clamp((bottomZero - row) / (bottomZero - bottomOpaque), 0.0f, 1.0f);
        return 1.0f;
    };

    // With a background the plate is solid across the whole frame and only the letters fade
    // into it; without one the faded alpha is the final pixel alpha.
    std::uint32_t backgroundRed = 0, backgroundGreen = 0, backgroundBlue = 0;
    if (backgroundEnabled) {
        const auto background = DibColor(backgroundColor);
        backgroundRed = (background >> 16) & 0xffu;
        backgroundGreen = (background >> 8) & 0xffu;
        backgroundBlue = background & 0xffu;
    }
    const std::uint32_t emptyPixel = backgroundEnabled
        ? 0xff000000u | (backgroundRed << 16) | (backgroundGreen << 8) | backgroundBlue : 0u;
    frame_.assign(static_cast<std::size_t>(width) * height, emptyPixel);
    const auto finish = [&](std::uint32_t pixel, float fade) {
        if (fade < 0.999f) {
            const auto alpha = static_cast<std::uint32_t>(((pixel >> 24) & 0xffu) * fade + 0.5f);
            pixel = (pixel & 0x00ffffffu) | (alpha << 24);
        }
        if (!backgroundEnabled) return pixel;
        const auto alpha = (pixel >> 24) & 0xffu;
        const auto inverse = 255u - alpha;
        const auto red = (((pixel >> 16) & 0xffu) * alpha + backgroundRed * inverse + 127u) / 255u;
        const auto green = (((pixel >> 8) & 0xffu) * alpha + backgroundGreen * inverse + 127u) / 255u;
        const auto blue = ((pixel & 0xffu) * alpha + backgroundBlue * inverse + 127u) / 255u;
        return 0xff000000u | (red << 16) | (green << 8) | blue;
    };
    // Copies a cached line so that its first row's baseline lands at (center, baseline), only
    // within [clipLeft, clipRight) x [clipTop, clipBottom) of the frame.
    const int center = width / 2;
    const auto blit = [&](const LineBitmap& line, int baseline, int clipLeft, int clipTop,
                          int clipRight, int clipBottom) {
        const int left = center - line.originX, top = baseline - line.originY;
        const int x0 = std::max({0, left, clipLeft}), x1 = std::min({width, left + line.width, clipRight});
        const int y0 = std::max({0, top, clipTop}), y1 = std::min({height, top + line.height, clipBottom});
        for (int row = y0; row < y1; ++row) {
            const float fade = fadeAt(row);
            const auto* source = line.pixels.data() + static_cast<std::size_t>(row - top) * line.width;
            auto* target = frame_.data() + static_cast<std::size_t>(row) * width;
            for (int column = x0; column < x1; ++column)
                if (const auto pixel = source[column - left]) target[column] = finish(pixel, fade);
        }
    };

    int activeFirstY = 0;
    for (std::size_t logical = visibleStart; logical < visibleEnd; ++logical) {
        if (logical == activeLine) activeFirstY = y;
        const bool subdued = logical < subduedLines.size() && subduedLines[logical] && logical != activeLine;
        const auto color = logical < activeLine || subdued ? readColor : textColor;
        if (const auto line = RenderLine(lines[logical], *wrapped[logical], color); line && !line->pixels.empty())
            blit(*line, y, 0, 0, width, height);
        y += rowsOf(logical) * spacing;
    }

    // Progressive highlight: the highlight-colored version of the active line, clipped per row
    // to the part already sung (same clip rectangle as the old renderer used).
    const auto& active = *wrapped[activeLine];
    int completedWidth = static_cast<int>(highlightProgress *
        std::accumulate(active.widths.begin(), active.widths.end(), 0));
    const LinePtr highlight = completedWidth > 0
        ? RenderLine(lines[activeLine], active, highlightColor) : nullptr;
    int highlightY = activeFirstY;
    for (std::size_t row = 0; row < active.rows.size(); ++row) {
        const int lineWidth = active.widths[row];
        const int paintedWidth = std::clamp(completedWidth, 0, lineWidth);
        if (paintedWidth > 0 && highlight && !highlight->pixels.empty())
            blit(*highlight, activeFirstY, center - lineWidth / 2 - 3, highlightY - fontSize - 3,
                 center - lineWidth / 2 + paintedWidth + 3, highlightY + fontSize / 3 + 3);
        completedWidth -= lineWidth;
        highlightY += spacing;
    }

    // Prepare what the next line change will need (the line becoming active in highlight color,
    // the current one in the "read" color, the lines scrolling in from below) on the worker
    // thread, so the render thread never has to draw a line itself while the song plays.
    if (wrapped[activeLine]) Prefetch(MakeJob(lines[activeLine], *wrapped[activeLine], highlightColor));
    if (activeLine + 1 < lines.size()) {
        const std::size_t nextActive = activeLine + 1;
        const std::size_t prefetchEnd = std::min(lastCandidate, visibleEnd + 2);
        for (std::size_t i = visibleStart; i < prefetchEnd; ++i) {
            if (!wrapped[i]) continue;
            const bool subdued = i < subduedLines.size() && subduedLines[i] && i != nextActive;
            Prefetch(MakeJob(lines[i], *wrapped[i], i < nextActive || subdued ? readColor : textColor));
        }
        if (wrapped[nextActive]) Prefetch(MakeJob(lines[nextActive], *wrapped[nextActive], highlightColor));
    }

    if (!dynamic_ || textureWidth_ != width || textureHeight_ != height || !texture_) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        texture_.Reset(); view_.Reset();
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture_)) ||
            FAILED(device_->CreateShaderResourceView(texture_.Get(), nullptr, &view_))) {
            texture_.Reset(); view_.Reset(); cacheKey_.clear();
            return false;
        }
        dynamic_ = true; textureWidth_ = width; textureHeight_ = height;
    }
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (!context || FAILED(context->Map(texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        cacheKey_.clear();
        return false;
    }
    for (int row = 0; row < height; ++row)
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch,
                    frame_.data() + static_cast<std::size_t>(row) * width, static_cast<std::size_t>(width) * 4);
    context->Unmap(texture_.Get(), 0);
    cacheKey_ = key;
    return true;
}

bool TextTexture::UpdateMessage(const std::wstring& message, int width, int height,
                                float fontScale, float verticalPosition,
                                bool backgroundEnabled, std::uint32_t backgroundColor) {
    std::vector<std::wstring> lines{message};
    return UpdateTimed(lines, 0, 0.0f, 0.0f, width, height, fontScale,
                       verticalPosition, {}, 0x00ffffffu, 0x0000d2ffu,
                       0x00969696u, 0, 0, 1, backgroundEnabled, backgroundColor);
}

bool TextTexture::UpdateBanner(const std::wstring& text, int width, int height,
                               std::uint32_t textColor, int fontFamily,
                               int backdropStyle, int backdropStrength) {
    if (!device_ || width <= 0 || height <= 0) return false;

    std::wstring key = L"banner:" + text + L":" + std::to_wstring(width) + L"x" +
        std::to_wstring(height) + L":" + std::to_wstring(textColor) + L":" +
        std::to_wstring(fontFamily) + L":" + std::to_wstring(backdropStyle) + L":" +
        std::to_wstring(backdropStrength);
    if (key == cacheKey_ && view_) return true;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) { DestroyCanvas(dc, bitmap, nullptr); return false; }
    const auto oldBitmap = SelectObject(dc, bitmap);
    if (!oldBitmap || oldBitmap == HGDI_ERROR) { DestroyCanvas(dc, bitmap, nullptr); return false; }
    std::fill_n(static_cast<std::uint32_t*>(pixels),
                static_cast<std::size_t>(width) * height, 0u);

    if (!text.empty()) {
        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_CENTER | TA_BASELINE);

        // Start from a size proportional to the frame height, then shrink to fit the width so
        // one line ("Artist - Title") never wraps and always stays on screen regardless of how
        // wide or narrow the output is.
        const int maxTextWidth = width * 92 / 100;
        int fontSize = std::clamp(height / 10, 14, 60);
        HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, FontName(fontFamily));
        if (!font) { DestroyCanvas(dc, bitmap, oldBitmap); return false; }
        auto oldFont = SelectObject(dc, font);
        if (!oldFont || oldFont == HGDI_ERROR) {
            DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap); return false;
        }

        const auto resize = [&](int newSize) -> bool {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            font = CreateFontW(-newSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, FontName(fontFamily));
            if (!font) return false;
            oldFont = SelectObject(dc, font);
            fontSize = newSize;
            return oldFont && oldFont != HGDI_ERROR;
        };

        int measured = TextWidth(dc, text);
        if (measured > maxTextWidth && measured > 0) {
            const int estimate = std::max(10,
                static_cast<int>(fontSize * (static_cast<float>(maxTextWidth) / measured)));
            if (estimate < fontSize && !resize(estimate)) {
                DestroyCanvas(dc, bitmap, oldBitmap); return false;
            }
            measured = TextWidth(dc, text);
        }
        while (measured > maxTextWidth && fontSize > 10) {
            if (!resize(fontSize - 1)) { DestroyCanvas(dc, bitmap, oldBitmap); return false; }
            measured = TextWidth(dc, text);
        }

        const int y = fontSize + std::max(2, height / 80);
        DrawStyledText(dc, width / 2, y, text, static_cast<COLORREF>(textColor),
                       backdropStyle, backdropStrength);
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    FinalizeAlpha(pixels, static_cast<std::size_t>(width) * height);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels, static_cast<UINT>(width * 4), 0};
    texture_.Reset(); view_.Reset();
    const auto hrTexture = device_->CreateTexture2D(&desc, &initial, &texture_);
    const auto hrView = SUCCEEDED(hrTexture)
        ? device_->CreateShaderResourceView(texture_.Get(), nullptr, &view_) : hrTexture;
    DestroyCanvas(dc, bitmap, oldBitmap);
    dynamic_ = false;
    if (FAILED(hrView)) return false;
    cacheKey_ = key;
    return true;
}
#endif
