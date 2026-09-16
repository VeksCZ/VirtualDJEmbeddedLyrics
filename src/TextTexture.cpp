#include "TextTexture.hpp"

#ifdef _WIN32
#include <windows.h>
#include <algorithm>
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
}

bool TextTexture::Update(const std::wstring& current, const std::wstring& next,
                         float progress, int width, int height, float fontScale, float verticalPosition) {
    std::vector<std::wstring> lines{current};
    if (!next.empty()) lines.push_back(next);
    return UpdateTimed(lines, 0, progress, progress, width, height, fontScale, verticalPosition);
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
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_CENTER | TA_BASELINE);

    // Font size follows the user's setting (fontScale) directly; it is not shrunk to force lines
    // onto a single row -- a line is free to wrap to two. The cap against targetLines is just a
    // sanity bound (don't let an absurd number of configured lines demand an absurd font), not an
    // attempt to guarantee no wrapping.
    const int requestedSize = std::max(18, static_cast<int>(std::max(36, height / 13) * fontScale));
    const int fontSize = std::min(requestedSize,
        std::max(8, static_cast<int>(height * 0.97f / (static_cast<float>(targetLines) * 1.2f))));
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, FontName(fontFamily));
    if (!font) { DestroyCanvas(dc, bitmap, oldBitmap); return false; }
    const auto oldFont = SelectObject(dc, font);
    if (!oldFont || oldFont == HGDI_ERROR) {
        DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap); return false;
    }

    const int maxTextWidth = width * 9 / 10;
    const int spacing = fontSize * 6 / 5;
    std::vector<std::vector<std::wstring>> wrappedLines;
    wrappedLines.reserve(lines.size());
    for (const auto& line : lines) wrappedLines.push_back(WrapText(dc, line, maxTextWidth));

    // The anchor and the fade band (further down) are derived only from targetLines/height/
    // fontSize/verticalPosition -- fixed settings -- never from which candidate lines happen to
    // be on screen or how many rows they wrap to. That is what keeps both perfectly stable frame
    // to frame: a line wrapping to two rows changes how many candidate lines are needed to fill
    // the frame (below), not where the anchor or the mask sit.
    const std::size_t previousCount = targetLines > 0 ? (targetLines - 1) / 2 : 0;
    const std::size_t inclusiveAfterCount = targetLines > previousCount ? targetLines - previousCount : 1;
    const int reservedBefore = static_cast<int>(previousCount) * spacing;
    const int reservedAfter = static_cast<int>(inclusiveAfterCount) * spacing;
    const int minAnchor = reservedBefore + fontSize;
    const int maxAnchor = std::max(minAnchor, height - reservedAfter + fontSize / 2);
    const int anchor = std::clamp(static_cast<int>(height * verticalPosition) + fontSize / 2,
                                  minAnchor, maxAnchor);

    const int activeHeight = std::max(1, static_cast<int>(wrappedLines[activeLine].size())) * spacing;

    // Now find how many of the candidate lines are actually needed to fill the frame around the
    // anchor, using each one's real (possibly wrapped) row count -- a two-row line simply "costs"
    // two rows of the fill instead of shifting anything else. The "after" side asks for
    // activeHeight more than the frame strictly needs at rest: over the course of the active
    // line's own display, content scrolls up by up to activeHeight, so without this margin the
    // trailing lines already on screen would run out before the next line-change refills them,
    // leaving a gap at the bottom. With the margin there's always a line already present to
    // scroll into place, so the fade band below can stay completely still (see below) instead of
    // chasing the scroll to avoid that gap.
    std::size_t visibleStart = activeLine, visibleEnd = activeLine + 1;
    for (int covered = 0; visibleStart > 0 && covered < anchor;) {
        --visibleStart;
        covered += static_cast<int>(wrappedLines[visibleStart].size()) * spacing;
    }
    for (int covered = activeHeight;
         visibleEnd < wrappedLines.size() && covered < height - anchor + activeHeight; ++visibleEnd) {
        covered += static_cast<int>(wrappedLines[visibleEnd].size()) * spacing;
    }

    int y = anchor - static_cast<int>(scrollProgress * activeHeight + 0.5f);
    for (std::size_t i = visibleStart; i < activeLine; ++i)
        y -= std::max(1, static_cast<int>(wrappedLines[i].size())) * spacing;

    const int center = width / 2;
    int activeFirstY = 0;
    for (std::size_t logical = visibleStart; logical < visibleEnd; ++logical) {
        if (logical == activeLine) activeFirstY = y;
        const bool subdued = logical < subduedLines.size() && subduedLines[logical] &&
                             logical != activeLine;
        const COLORREF color = static_cast<COLORREF>(logical < activeLine || subdued ? readColor : textColor);
        for (const auto& visual : wrappedLines[logical]) {
            DrawStyledText(dc, center, y, visual, color, backdropStyle, backdropStrength);
            y += spacing;
        }
    }

    int completedWidth = static_cast<int>(highlightProgress * std::accumulate(
        wrappedLines[activeLine].begin(), wrappedLines[activeLine].end(), 0,
        [dc](int total, const std::wstring& line) { return total + TextWidth(dc, line); }));
    int highlightY = activeFirstY;
    for (const auto& visual : wrappedLines[activeLine]) {
        const int lineWidth = TextWidth(dc, visual);
        const int paintedWidth = std::clamp(completedWidth, 0, lineWidth);
        if (paintedWidth > 0) {
            HRGN clip = CreateRectRgn(center - lineWidth / 2 - 3, highlightY - fontSize - 3,
                                      center - lineWidth / 2 + paintedWidth + 3,
                                      highlightY + fontSize / 3 + 3);
            if (!clip || SelectClipRgn(dc, clip) == ERROR) {
                if (clip) DeleteObject(clip);
                SelectObject(dc, oldFont); DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap);
                return false;
            }
            DrawStyledText(dc, center, highlightY, visual, static_cast<COLORREF>(highlightColor),
                           backdropStyle, backdropStrength);
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);
        }
        completedWidth -= lineWidth;
        highlightY += spacing;
    }

    FinalizeAlpha(pixels, static_cast<std::size_t>(width) * height);
    auto* pixelValues = static_cast<std::uint32_t*>(pixels);
    // The fade band is placed purely from reservedBefore/reservedAfter/anchor -- the same fixed,
    // settings-only values used for the anchor above -- with no scrollProgress term at all, so it
    // is pinned to the same two rows for as long as the settings don't change: it cannot drift
    // with the content and cannot jump at a line change. Earlier this band *did* track scroll
    // (subtracting scrollProgress * activeHeight, matching the text's own offset), which kept it
    // glued to the text within one line's scroll -- but activeHeight is only that one line's own
    // height, so the instant the active line changed and activeHeight changed with it, that
    // tracked offset jumped too. The reason the band can simply stay still instead is the extra
    // activeHeight of margin the "after" search above already asks for: there is always a line
    // already on screen ready to scroll up into a fixed band, so nothing needs to chase the
    // scroll to avoid a gap.
    // Never ask for more fade width than the frame actually has (only possible on an
    // extremely small output), so the clamps below always have a valid, non-inverted range.
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
    // With a background plate enabled, the plate itself must fade out at the top/bottom edges
    // too -- not just the text -- or the plate reads as a hard-edged opaque box with no "mask"
    // at all, since a solid plate hides the transparency fade that would otherwise reveal video
    // underneath. So the per-row fade factor drives the *output* alpha directly in that case,
    // uniformly across the row (plate and text together), instead of only scaling the text's own
    // alpha and then forcing the plate back to fully opaque afterwards.
    std::uint32_t backgroundRed = 0, backgroundGreen = 0, backgroundBlue = 0;
    if (backgroundEnabled) {
        const auto background = DibColor(backgroundColor);
        backgroundRed = (background >> 16) & 0xffu;
        backgroundGreen = (background >> 8) & 0xffu;
        backgroundBlue = background & 0xffu;
    }
    for (int row = 0; row < height; ++row) {
        float fade = 1.0f;
        if (row < topOpaque) fade = std::clamp((row - topZero) / (topOpaque - topZero), 0.0f, 1.0f);
        else if (row > bottomOpaque) fade = std::clamp((bottomZero - row) / (bottomZero - bottomOpaque), 0.0f, 1.0f);
        if (!backgroundEnabled) {
            if (fade >= 0.999f) continue;
            for (int column = 0; column < width; ++column) {
                auto& pixel = pixelValues[static_cast<std::size_t>(row) * width + column];
                const auto alpha = static_cast<std::uint32_t>(((pixel >> 24) & 0xffu) * fade + 0.5f);
                pixel = (pixel & 0x00ffffffu) | (alpha << 24);
            }
            continue;
        }
        const std::uint32_t outAlpha = static_cast<std::uint32_t>(255.0f * fade + 0.5f) << 24;
        for (int column = 0; column < width; ++column) {
            auto& pixel = pixelValues[static_cast<std::size_t>(row) * width + column];
            const auto source = pixel;
            const auto alpha = (source >> 24) & 0xffu;
            const auto inverse = 255u - alpha;
            const auto red = (((source >> 16) & 0xffu) * alpha + backgroundRed * inverse + 127u) / 255u;
            const auto green = (((source >> 8) & 0xffu) * alpha + backgroundGreen * inverse + 127u) / 255u;
            const auto blue = ((source & 0xffu) * alpha + backgroundBlue * inverse + 127u) / 255u;
            pixel = outAlpha | (red << 16) | (green << 8) | blue;
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels, static_cast<UINT>(width * 4), 0};
    texture_.Reset(); view_.Reset();
    const auto hrTexture = device_->CreateTexture2D(&desc, &initial, &texture_);
    const auto hrView = SUCCEEDED(hrTexture)
        ? device_->CreateShaderResourceView(texture_.Get(), nullptr, &view_) : hrTexture;
    SelectObject(dc, oldFont); DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap);
    if (FAILED(hrView)) return false;
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
    if (FAILED(hrView)) return false;
    cacheKey_ = key;
    return true;
}
#endif
