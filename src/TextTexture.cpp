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
    SetTextColor(dc, RGB(1, 1, 1));
    for (int ox = -3; ox <= 3; ox += 3)
        for (int oy = -3; oy <= 3; oy += 3)
            if (ox || oy) TextOutW(dc, x + ox, y + oy, text.c_str(), static_cast<int>(text.size()));
    SetTextColor(dc, color);
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
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
    return UpdateTimed(lines, progress, width, height, fontScale, verticalPosition);
}

bool TextTexture::UpdateTimed(const std::vector<std::wstring>& lines, float progress,
                              int width, int height, float fontScale, float verticalPosition) {
    if (!device_ || width <= 0 || height <= 0) return false;
    if (lines.empty()) return false;
    progress = std::clamp(progress, 0.0f, 1.0f);
    fontScale = std::clamp(fontScale, 0.5f, 2.0f);
    verticalPosition = std::clamp(verticalPosition, 0.1f, 0.9f);
    std::wstring key;
    for (const auto& line : lines) key += line + L'\n';
    key += L":" + std::to_wstring(width) + L"x" + std::to_wstring(height) + L":" +
           std::to_wstring(static_cast<int>(progress * 100)) + L":" +
           std::to_wstring(static_cast<int>(fontScale * 100));
    const auto positionedKey = key + L":" + std::to_wstring(static_cast<int>(verticalPosition * 100));
    if (positionedKey == cacheKey_ && view_) return true;

    // A 32-bit top-down DIB gives GDI a fast CPU surface. It is uploaded only
    // when the visible text or highlighting changes, not for every video frame.
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
    int fontSize = std::max(18, static_cast<int>(std::max(36, height / 13) * fontScale));
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Arial");
    if (!font) { DestroyCanvas(dc, bitmap, oldBitmap); return false; }
    auto oldFont = SelectObject(dc, font);
    if (!oldFont || oldFont == HGDI_ERROR) {
        DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap); return false;
    }
    const int maxTextWidth = width * 9 / 10;
    const auto currentLines = WrapText(dc, lines.front(), maxTextWidth);
    std::vector<std::wstring> followingLines;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        auto wrapped = WrapText(dc, lines[i], maxTextWidth);
        followingLines.insert(followingLines.end(),
                              std::make_move_iterator(wrapped.begin()), std::make_move_iterator(wrapped.end()));
    }
    const int center = width / 2;
    const int spacing = fontSize * 6 / 5;
    // Keep the logical block anchored independently of word wrapping. Moving it
    // by the complete height of the current (possibly wrapped) lyric makes the
    // next lyric arrive at exactly the same position where it starts on the
    // following timestamp, avoiding a visible jump between lines.
    const int logicalBlockHeight = std::max(1, static_cast<int>(lines.size())) * spacing;
    const int scrollDistance = static_cast<int>(currentLines.size()) * spacing;
    int y = static_cast<int>(height * verticalPosition) - logicalBlockHeight / 2 + fontSize;
    y -= static_cast<int>(progress * scrollDistance + 0.5f);
    const int firstLineY = y;
    for (const auto& line : currentLines) {
        DrawOutlinedText(dc, center, y, line, RGB(255, 255, 255));
        y += spacing;
    }
    if (!followingLines.empty()) {
        for (const auto& line : followingLines) {
            DrawOutlinedText(dc, center, y, line, RGB(220, 220, 220));
            y += spacing;
        }
    }

    int completedWidth = static_cast<int>(progress * std::accumulate(currentLines.begin(), currentLines.end(), 0,
        [dc](int total, const std::wstring& line) { return total + TextWidth(dc, line); }));
    int currentY = firstLineY;
    for (const auto& line : currentLines) {
        const int lineWidth = TextWidth(dc, line);
        const int paintedWidth = std::clamp(completedWidth, 0, lineWidth);
        if (paintedWidth > 0) {
            HRGN clip = CreateRectRgn(center - lineWidth / 2 - 3, currentY - fontSize - 3,
                                      center - lineWidth / 2 + paintedWidth + 3, currentY + fontSize / 3 + 3);
            if (!clip || SelectClipRgn(dc, clip) == ERROR) {
                if (clip) DeleteObject(clip);
                SelectObject(dc, oldFont); DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap);
                return false;
            }
            DrawOutlinedText(dc, center, currentY, line, RGB(255, 210, 0));
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);
        }
        completedWidth -= lineWidth;
        currentY += spacing;
    }

    FinalizeAlpha(pixels, static_cast<std::size_t>(width) * height);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels, static_cast<UINT>(width * 4), 0};
    texture_.Reset();
    view_.Reset();
    const auto hrTexture = device_->CreateTexture2D(&desc, &initial, &texture_);
    const auto hrView = SUCCEEDED(hrTexture) ? device_->CreateShaderResourceView(texture_.Get(), nullptr, &view_) : hrTexture;

    SelectObject(dc, oldFont);
    DeleteObject(font);
    DestroyCanvas(dc, bitmap, oldBitmap);
    if (FAILED(hrView)) return false;
    cacheKey_ = positionedKey;
    return true;
}

bool TextTexture::UpdatePage(const std::vector<std::wstring>& lines, std::size_t page,
                             std::size_t pageSize, std::size_t activeLine, int width, int height,
                             float fontScale, float verticalPosition) {
    if (!device_ || width <= 0 || height <= 0) return false;
    fontScale = std::clamp(fontScale, 0.5f, 2.0f);
    verticalPosition = std::clamp(verticalPosition, 0.1f, 0.9f);
    std::wstringstream keyBuilder;
    keyBuilder << L"page:" << page << L':' << width << L'x' << height << L':'
               << static_cast<int>(fontScale * 100);
    keyBuilder << L':' << activeLine << L':' << static_cast<int>(verticalPosition * 100);
    const auto begin = std::min(page * pageSize, lines.size());
    const auto end = std::min(begin + pageSize, lines.size());
    for (auto i = begin; i < end; ++i) keyBuilder << L'\n' << lines[i];
    const auto key = keyBuilder.str();
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
    int fontSize = std::max(16, static_cast<int>(std::max(30, height / 19) * fontScale));
    HFONT font = CreateFontW(-fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Arial");
    if (!font) { DestroyCanvas(dc, bitmap, oldBitmap); return false; }
    const auto oldFont = SelectObject(dc, font);
    if (!oldFont || oldFont == HGDI_ERROR) {
        DeleteObject(font); DestroyCanvas(dc, bitmap, oldBitmap); return false;
    }
    struct WrappedLine { std::wstring text; bool active; };
    std::vector<WrappedLine> wrappedLines;
    for (auto i = begin; i < end; ++i) {
        auto wrapped = WrapText(dc, lines[i], width * 9 / 10);
        for (auto& line : wrapped) wrappedLines.push_back({std::move(line), i == activeLine});
    }
    const int spacing = fontSize * 6 / 5;
    const int totalHeight = static_cast<int>(wrappedLines.size()) * spacing;
    int y = static_cast<int>(height * verticalPosition) - totalHeight / 2 + fontSize;
    y = std::clamp(y, fontSize, std::max(fontSize, height - totalHeight + fontSize));
    for (const auto& line : wrappedLines) {
        DrawOutlinedText(dc, width / 2, y, line.text, line.active ? RGB(255, 210, 0) : RGB(255, 255, 255));
        y += spacing;
    }
    FinalizeAlpha(pixels, static_cast<std::size_t>(width) * height);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels, static_cast<UINT>(width * 4), 0};
    texture_.Reset(); view_.Reset();
    const auto hrTexture = device_->CreateTexture2D(&desc, &initial, &texture_);
    const auto hrView = SUCCEEDED(hrTexture) ? device_->CreateShaderResourceView(texture_.Get(), nullptr, &view_) : hrTexture;
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
