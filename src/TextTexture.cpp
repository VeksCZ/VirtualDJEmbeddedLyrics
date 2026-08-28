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

void DrawOutlinedText(HDC dc, int x, int y, const std::wstring& text, COLORREF color) {
    if ((color & 0x00ffffffu) == 0) color = RGB(1, 1, 1);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const int penWidth = std::clamp(static_cast<int>(metrics.tmHeight) / 8, 6, 14);
    HPEN outline = CreatePen(PS_SOLID, penWidth, RGB(1, 1, 1));
    HBRUSH fill = CreateSolidBrush(color);
    if (outline && fill && BeginPath(dc) &&
        TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size())) && EndPath(dc)) {
        const auto oldPen = SelectObject(dc, outline);
        const auto oldBrush = SelectObject(dc, fill);
        StrokeAndFillPath(dc);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
    } else {
        AbortPath(dc);
        SetTextColor(dc, RGB(1, 1, 1));
        for (int ox = -4; ox <= 4; ox += 2)
            for (int oy = -4; oy <= 4; oy += 2)
                if (ox || oy) TextOutW(dc, x + ox, y + oy, text.c_str(), static_cast<int>(text.size()));
        SetTextColor(dc, color);
        TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
    }
    if (fill) DeleteObject(fill);
    if (outline) DeleteObject(outline);
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
        const auto darkest = std::max(r, std::max(g, b));
        const auto alpha = darkest <= 4 ? 255u : std::max<std::uint32_t>(150, darkest);
        values[i] = rgb | (alpha << 24);
    }
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
                              float fontScale, float verticalPosition, const LyricColors& colors,
                              const std::vector<bool>& subduedLines) {
    if (!device_ || width <= 0 || height <= 0 || lines.empty() || activeLine >= lines.size()) return false;
    highlightProgress = std::clamp(highlightProgress, 0.0f, 1.0f);
    scrollProgress = std::clamp(scrollProgress, 0.0f, 1.0f);
    fontScale = std::clamp(fontScale, 0.5f, 2.0f);
    verticalPosition = std::clamp(verticalPosition, 0.1f, 0.9f);

    std::wstring key = L"timed:" + std::to_wstring(activeLine) + L':';
    for (const auto subdued : subduedLines) key += subdued ? L'1' : L'0';
    key += L':' + std::to_wstring(colors.text) + L':' +
           std::to_wstring(colors.highlight) + L':' + std::to_wstring(colors.read) + L':';
    for (const auto& line : lines) key += line + L'\n';
    key += std::to_wstring(width) + L"x" + std::to_wstring(height) + L":" +
           std::to_wstring(static_cast<int>(highlightProgress * 200)) + L":" +
           std::to_wstring(static_cast<int>(scrollProgress * 200)) + L":" +
           std::to_wstring(static_cast<int>(fontScale * 100)) + L":" +
           std::to_wstring(static_cast<int>(verticalPosition * 100));
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
    std::fill_n(static_cast<std::uint32_t*>(pixels), static_cast<std::size_t>(width) * height, 0u);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_CENTER | TA_BASELINE);

    const int fontSize = std::max(18, static_cast<int>(std::max(36, height / 13) * fontScale));
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Arial");
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

    const int activeHeight = std::max(1, static_cast<int>(wrappedLines[activeLine].size())) * spacing;
    const int anchor = static_cast<int>(height * verticalPosition) + fontSize / 2;
    int y = anchor - static_cast<int>(scrollProgress * activeHeight + 0.5f);
    for (std::size_t i = 0; i < activeLine; ++i)
        y -= std::max(1, static_cast<int>(wrappedLines[i].size())) * spacing;

    const int center = width / 2;
    int activeFirstY = 0;
    for (std::size_t logical = 0; logical < wrappedLines.size(); ++logical) {
        if (logical == activeLine) activeFirstY = y;
        const bool subdued = logical < subduedLines.size() && subduedLines[logical] &&
                             logical != activeLine;
        const COLORREF color = logical < activeLine || subdued ? colors.read : colors.text;
        for (const auto& visual : wrappedLines[logical]) {
            DrawOutlinedText(dc, center, y, visual, color);
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
            DrawOutlinedText(dc, center, highlightY, visual, colors.highlight);
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);
        }
        completedWidth -= lineWidth;
        highlightY += spacing;
    }

    FinalizeAlpha(pixels, static_cast<std::size_t>(width) * height);
    auto* pixelValues = static_cast<std::uint32_t*>(pixels);
    const float topZero = static_cast<float>(anchor) - spacing * 5.25f;
    const float topOpaque = static_cast<float>(anchor) - spacing * 2.5f;
    const float bottomOpaque = static_cast<float>(anchor) + spacing * 2.5f;
    const float bottomZero = static_cast<float>(anchor) + spacing * 4.0f;
    for (int row = 0; row < height; ++row) {
        float fade = 1.0f;
        if (row < topOpaque) fade = std::clamp((row - topZero) / (topOpaque - topZero), 0.0f, 1.0f);
        else if (row > bottomOpaque) fade = std::clamp((bottomZero - row) / (bottomZero - bottomOpaque), 0.0f, 1.0f);
        if (fade >= 0.999f) continue;
        for (int column = 0; column < width; ++column) {
            auto& pixel = pixelValues[static_cast<std::size_t>(row) * width + column];
            const auto alpha = static_cast<std::uint32_t>(((pixel >> 24) & 0xffu) * fade + 0.5f);
            pixel = (pixel & 0x00ffffffu) | (alpha << 24);
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
                                float fontScale, float verticalPosition) {
    return Update(message, L"", 0.0f, width, height, fontScale, verticalPosition);
}
#endif
