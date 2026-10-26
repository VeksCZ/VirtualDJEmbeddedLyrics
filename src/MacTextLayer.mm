#include "MacTextLayer.hpp"
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr const char* kFontNames[] = {"Arial", "Segoe UI", "Verdana", "Tahoma", "Trebuchet MS", "Calibri"};

NSString* ToNSString(const std::wstring& value) {
    if (value.empty()) return @"";
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size() * sizeof(wchar_t)
                                  encoding:NSUTF32LittleEndianStringEncoding] ?: @"";
}

NSColor* Color(const MacRgb& c, double alpha = 1.0) {
    return [NSColor colorWithCalibratedRed:c.red green:c.green blue:c.blue alpha:alpha];
}

// Bold variant of the chosen family; families missing on macOS (Segoe UI, Calibri) fall back
// to the bold system font so the choice never breaks rendering.
NSFont* LyricsFont(int family, CGFloat size) {
    const int index = std::clamp(family, 0, static_cast<int>(std::size(kFontNames)) - 1);
    NSFont* font = [NSFont fontWithName:[NSString stringWithUTF8String:kFontNames[index]] size:size];
    if (font) font = [[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSBoldFontMask];
    return font ?: [NSFont boldSystemFontOfSize:size];
}

NSDictionary* Attributes(const MacTextStyle& style, NSFont* font, NSColor* color, CGFloat fontSize) {
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
    paragraph.alignment = NSTextAlignmentCenter;
    paragraph.lineBreakMode = NSLineBreakByWordWrapping;
    NSMutableDictionary* attributes = [@{NSFontAttributeName: font,
                                         NSForegroundColorAttributeName: color,
                                         NSParagraphStyleAttributeName: paragraph} mutableCopy];
    const int strength = std::clamp(style.strength, 0, 2);
    if (style.backdrop == 0 || style.backdrop == 2) {
        static constexpr double kStroke[] = {-2.0, -3.5, -5.5};  // % of the font size
        attributes[NSStrokeColorAttributeName] = NSColor.blackColor;
        attributes[NSStrokeWidthAttributeName] = @(kStroke[strength]);
    }
    if (style.backdrop == 1 || style.backdrop == 2) {
        static constexpr double kBlur[] = {0.05, 0.09, 0.14};
        NSShadow* shadow = [[NSShadow alloc] init];
        shadow.shadowColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.95];
        shadow.shadowBlurRadius = std::max<CGFloat>(2.0, fontSize * kBlur[strength]);
        shadow.shadowOffset = NSMakeSize(0.0, -std::max<CGFloat>(1.0, fontSize * 0.04));
        attributes[NSShadowAttributeName] = shadow;
    }
    return attributes;
}

struct Canvas {
    int width{}, height{};
    std::size_t rowBytes{};
    std::vector<std::uint8_t> pixels;
    CGContextRef context{};
    NSGraphicsContext* graphics{};

    Canvas(int w, int h) : width(w), height(h), rowBytes(static_cast<std::size_t>(w) * 4),
                           pixels(rowBytes * static_cast<std::size_t>(h), 0) {
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        context = CGBitmapContextCreate(pixels.data(), w, h, 8, rowBytes, space,
            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little |
                                      static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst)));
        CGColorSpaceRelease(space);
        if (context) {
            graphics = [NSGraphicsContext graphicsContextWithCGContext:context flipped:YES];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:graphics];
            // Flip so y grows downwards like the Windows layout code.
            CGContextTranslateCTM(context, 0, h);
            CGContextScaleCTM(context, 1, -1);
        }
    }
    ~Canvas() {
        if (context) {
            [NSGraphicsContext restoreGraphicsState];
            CGContextRelease(context);
        }
    }
    bool ok() const { return context != nullptr; }

    // Premultiplied pixels: scale all four channels.
    void FadeRow(int row, double factor) {
        if (row < 0 || row >= height || factor >= 0.999) return;
        auto* p = pixels.data() + static_cast<std::size_t>(row) * rowBytes;
        const auto f = static_cast<int>(std::clamp(factor, 0.0, 1.0) * 256.0);
        for (std::size_t i = 0; i < rowBytes; ++i) p[i] = static_cast<std::uint8_t>((p[i] * f) >> 8);
    }
    // Solid plate under the (already faded) text: only the text fades, never the plate.
    void Underlay(const MacRgb& color) {
        const auto b = static_cast<int>(std::lround(color.blue * 255));
        const auto g = static_cast<int>(std::lround(color.green * 255));
        const auto r = static_cast<int>(std::lround(color.red * 255));
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            const int inverse = 255 - pixels[i + 3];
            pixels[i] = static_cast<std::uint8_t>(pixels[i] + (b * inverse + 127) / 255);
            pixels[i + 1] = static_cast<std::uint8_t>(pixels[i + 1] + (g * inverse + 127) / 255);
            pixels[i + 2] = static_cast<std::uint8_t>(pixels[i + 2] + (r * inverse + 127) / 255);
            pixels[i + 3] = 255;
        }
    }
};

CGFloat BaseFontSize(int height, float fontScale) {
    return std::max<CGFloat>(18.0, std::max<CGFloat>(36.0, static_cast<CGFloat>(height) / 13.0) *
                                       std::clamp(fontScale, 0.5f, 2.0f));
}

std::wstring Quantized(float value, int steps) {
    return std::to_wstring(static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * steps)));
}

std::wstring StyleKey(const MacTextStyle& s) {
    auto c = [](const MacRgb& v) {
        return std::to_wstring(std::lround(v.red * 255)) + L"," + std::to_wstring(std::lround(v.green * 255)) +
               L"," + std::to_wstring(std::lround(v.blue * 255));
    };
    return std::to_wstring(s.font) + L"|" + std::to_wstring(s.backdrop) + L"|" + std::to_wstring(s.strength) +
           L"|" + c(s.text) + L"|" + c(s.highlight) + L"|" + c(s.read) + L"|" + std::to_wstring(s.background) +
           L"|" + c(s.backgroundColor);
}

// ---- per-line cache -------------------------------------------------------------------
// One lyric line drawn with its outline/shadow into a premultiplied BGRA bitmap, plus the
// rectangles of its wrapped rows (for the progressive highlight clip).
struct LineBitmap {
    int width{}, height{};
    std::vector<std::uint8_t> pixels;
    struct Row { int top{}, bottom{}, left{}, right{}; NSUInteger start{}, end{}; };
    std::vector<Row> rows;
    NSUInteger length{};
};

struct LineJob {
    std::wstring key, text;
    MacTextStyle style;
    MacRgb color;
    CGFloat fontSize{}, maxWidth{}, textHeight{};
    int pad{};
    unsigned generation{};
};

LineBitmap RenderLineBitmap(const LineJob& job) {
    LineBitmap line;
    @autoreleasepool {
        line.width = static_cast<int>(std::ceil(job.maxWidth)) + 2 * job.pad;
        line.height = static_cast<int>(std::ceil(job.textHeight)) + 2 * job.pad;
        Canvas canvas(line.width, line.height);
        if (!canvas.ok()) return {};
        NSString* text = ToNSString(job.text);
        NSFont* font = LyricsFont(job.style.font, job.fontSize);
        NSAttributedString* string = [[NSAttributedString alloc]
            initWithString:text attributes:Attributes(job.style, font, Color(job.color), job.fontSize)];
        const NSRect rect = NSMakeRect(job.pad, job.pad, job.maxWidth, job.textHeight);
        [string drawWithRect:rect
                     options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading];
        // Row geometry (same wrapping as the drawing above) for the highlight clip.
        NSTextStorage* storage = [[NSTextStorage alloc] initWithAttributedString:string];
        NSLayoutManager* layout = [[NSLayoutManager alloc] init];
        NSTextContainer* container = [[NSTextContainer alloc] initWithSize:NSMakeSize(job.maxWidth, CGFLOAT_MAX)];
        container.lineFragmentPadding = 0;
        [layout addTextContainer:container];
        [storage addLayoutManager:layout];
        [layout ensureLayoutForTextContainer:container];
        line.length = text.length;
        NSUInteger glyph = 0;
        const NSUInteger glyphs = layout.numberOfGlyphs;
        while (glyph < glyphs) {
            NSRange range{};
            const NSRect fragment = [layout lineFragmentUsedRectForGlyphAtIndex:glyph effectiveRange:&range];
            const NSRange chars = [layout characterRangeForGlyphRange:range actualGlyphRange:nullptr];
            LineBitmap::Row row;
            row.top = static_cast<int>(std::floor(rect.origin.y + fragment.origin.y)) - job.pad / 2;
            row.bottom = static_cast<int>(std::ceil(rect.origin.y + NSMaxY(fragment))) + job.pad / 2;
            // Centred paragraph: the used rect is already positioned within the container.
            row.left = static_cast<int>(std::floor(rect.origin.x + fragment.origin.x)) - job.pad / 2;
            row.right = static_cast<int>(std::ceil(rect.origin.x + NSMaxX(fragment))) + job.pad / 2;
            row.start = chars.location;
            row.end = chars.location + chars.length;
            line.rows.push_back(row);
            if (NSMaxRange(range) <= glyph) break;
            glyph = NSMaxRange(range);
        }
        line.pixels = std::move(canvas.pixels);
        canvas.pixels.clear();
    }
    return line;
}

}  // namespace

// Shared with the background queue: blocks keep it alive, so destroying the layer while a
// line is still being drawn is safe.
struct MacTextLayer::Lines {
    std::mutex mutex;
    std::unordered_map<std::wstring, std::shared_ptr<const LineBitmap>> cache;
    std::unordered_set<std::wstring> queued;
    std::unordered_map<std::wstring, CGFloat> heights;   // measured text heights (render thread only)
    std::wstring styleKey;
    unsigned generation{};
    dispatch_queue_t queue = dispatch_queue_create("lrc.lines", DISPATCH_QUEUE_SERIAL);

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        ++generation;
        cache.clear();
        queued.clear();
        heights.clear();
        styleKey.clear();
    }

    static void Prefetch(const std::shared_ptr<Lines>& self, LineJob job) {
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            if (self->cache.count(job.key) || self->queued.count(job.key)) return;
            job.generation = self->generation;
            self->queued.insert(job.key);
        }
        std::weak_ptr<Lines> weak = self;
        auto shared = std::make_shared<LineJob>(std::move(job));
        dispatch_async(self->queue, ^{
            auto line = std::make_shared<const LineBitmap>(RenderLineBitmap(*shared));
            if (auto lines = weak.lock()) {
                std::lock_guard<std::mutex> lock(lines->mutex);
                lines->queued.erase(shared->key);
                if (shared->generation == lines->generation && !line->pixels.empty())
                    lines->cache[shared->key] = std::move(line);
            }
        });
    }

    // Cached bitmap, or nullptr after queueing it (never drawn on the render thread).
    static std::shared_ptr<const LineBitmap> Get(const std::shared_ptr<Lines>& self, const LineJob& job) {
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            if (auto found = self->cache.find(job.key); found != self->cache.end()) return found->second;
            if (self->cache.size() > 256) self->cache.clear();
        }
        Prefetch(self, job);
        return nullptr;
    }
};

namespace {
std::wstring ColorKey(const MacRgb& v) {
    return std::to_wstring(std::lround(v.red * 255)) + L"," + std::to_wstring(std::lround(v.green * 255)) +
           L"," + std::to_wstring(std::lround(v.blue * 255));
}
}  // namespace

MacTextLayer::MacTextLayer() : lines_(std::make_shared<Lines>()) {}
MacTextLayer::~MacTextLayer() = default;

void MacTextLayer::Reset() {
    texture_ = nil;
    pipeline_ = nil;
    device_ = nil;
    pipelineDevice_ = nil;
    cacheKey_.clear();
    missingLines_ = false;
    if (lines_) lines_->Clear();
}

bool MacTextLayer::UpdateTimed(id<MTLDevice> device, const std::vector<std::wstring>& lines,
                               std::size_t activeLine, float highlightProgress, float scrollProgress,
                               int width, int height, float fontScale, float verticalPosition,
                               const std::vector<bool>& subdued, const MacTextStyle& style,
                               std::size_t targetLines) {
    if (!device || lines.empty() || activeLine >= lines.size() || width <= 0 || height <= 0) return false;
    if (targetLines == 0) targetLines = lines.size();
    // Quantize animation so the frame is only re-composed when something visibly changes.
    std::wstring key = L"T|" + std::to_wstring(activeLine) + L"|" + Quantized(highlightProgress, 48) + L"|" +
                       Quantized(scrollProgress, 40) + L"|" + std::to_wstring(width) + L"x" +
                       std::to_wstring(height) + L"|" + std::to_wstring(fontScale) + L"|" +
                       std::to_wstring(verticalPosition) + L"|" + std::to_wstring(targetLines) + L"|" +
                       StyleKey(style);
    const std::size_t from = activeLine > targetLines + 2 ? activeLine - targetLines - 2 : 0;
    const std::size_t to = std::min(lines.size(), activeLine + targetLines + 3);
    for (std::size_t i = from; i < to; ++i)
        key += L"\n" + std::to_wstring(i < subdued.size() && subdued[i]) + lines[i];
    if (device_ == device && texture_ && key == cacheKey_ && !missingLines_) return true;
    missingLines_ = false;

    @autoreleasepool {
        const auto H = static_cast<CGFloat>(height);
        // Many configured lines shrink the font so they still fit the frame.
        CGFloat fontSize = BaseFontSize(height, fontScale);
        fontSize = std::min(fontSize, H * 0.85 / (static_cast<CGFloat>(targetLines) * 1.25));
        fontSize = std::max<CGFloat>(fontSize, 12.0);
        const CGFloat spacing = fontSize * 1.25;
        const CGFloat maxWidth = static_cast<CGFloat>(width) * 0.9;
        const CGFloat anchor = H * std::clamp(verticalPosition, 0.1f, 0.9f);
        const int pad = static_cast<int>(std::ceil(fontSize * 0.35)) + 8;   // outline + shadow room

        const std::wstring styleKey = StyleKey(style) + L"|" + std::to_wstring(fontSize) + L"|" +
                                      std::to_wstring(maxWidth);
        if (lines_->styleKey != styleKey) { lines_->Clear(); lines_->styleKey = styleKey; }

        // Text heights are measured once per text (AppKit measuring is not free either).
        NSFont* font = nil;
        NSDictionary* measure = nil;
        std::vector<CGFloat> heights(lines.size(), spacing);
        for (std::size_t i = from; i < to; ++i) {
            auto found = lines_->heights.find(lines[i]);
            if (found == lines_->heights.end()) {
                if (!font) {
                    font = LyricsFont(style.font, fontSize);
                    measure = Attributes(style, font, NSColor.whiteColor, fontSize);
                }
                const NSRect bounds = [ToNSString(lines[i]) boundingRectWithSize:NSMakeSize(maxWidth, CGFLOAT_MAX)
                    options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                    attributes:measure];
                found = lines_->heights.emplace(lines[i],
                    std::max(spacing, std::ceil(bounds.size.height) + fontSize * 0.25)).first;
            }
            heights[i] = found->second;
        }
        // Active line centred on the anchor, moving up towards the next line while scrolling.
        const CGFloat next = activeLine + 1 < lines.size() ? heights[activeLine + 1] : heights[activeLine];
        const CGFloat shift = std::clamp(scrollProgress, 0.0f, 1.0f) * (heights[activeLine] + next) * 0.5;
        std::vector<CGFloat> tops(lines.size(), 0.0);
        tops[activeLine] = anchor - heights[activeLine] * 0.5 - shift;
        for (std::size_t i = activeLine; i > from; --i) tops[i - 1] = tops[i] - heights[i - 1];
        for (std::size_t i = activeLine + 1; i < to; ++i) tops[i] = tops[i - 1] + heights[i - 1];

        const auto job = [&](std::size_t i, const MacRgb& color) {
            LineJob j;
            j.key = ColorKey(color) + L"|" + lines[i];
            j.text = lines[i];
            j.style = style;
            j.color = color;
            j.fontSize = fontSize;
            j.maxWidth = maxWidth;
            j.textHeight = heights[i] - fontSize * 0.25;
            j.pad = pad;
            return j;
        };

        // Fade band derived from the configured line count only (stable while lines wrap).
        const CGFloat half = static_cast<CGFloat>(targetLines) * spacing * 0.5;
        const CGFloat band = spacing * 1.5;
        std::vector<int> fade(static_cast<std::size_t>(height), 256);
        for (int row = 0; row < height; ++row) {
            const CGFloat y = row + 0.5;
            double factor = 1.0;
            if (y < anchor - half) factor = std::clamp((y - (anchor - half - band)) / band, 0.0, 1.0);
            else if (y > anchor + half) factor = std::clamp(((anchor + half + band) - y) / band, 0.0, 1.0);
            fade[static_cast<std::size_t>(row)] = factor >= 0.999 ? 256 : static_cast<int>(factor * 256.0);
        }
        const int bgB = static_cast<int>(std::lround(style.backgroundColor.blue * 255));
        const int bgG = static_cast<int>(std::lround(style.backgroundColor.green * 255));
        const int bgR = static_cast<int>(std::lround(style.backgroundColor.red * 255));
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
        frame_.assign(rowBytes * static_cast<std::size_t>(height), 0);
        if (style.background) {
            for (std::size_t p = 0; p < frame_.size(); p += 4) {
                frame_[p] = static_cast<std::uint8_t>(bgB); frame_[p + 1] = static_cast<std::uint8_t>(bgG);
                frame_[p + 2] = static_cast<std::uint8_t>(bgR); frame_[p + 3] = 255;
            }
        }
        // Copy a cached line at (left, top) into the frame within the clip, fading the text
        // (premultiplied: all four channels) and putting it over the plate when enabled.
        const auto blit = [&](const LineBitmap& line, int left, int top, int clipL, int clipT, int clipR, int clipB) {
            const int x0 = std::max({0, left, left + clipL}), x1 = std::min({width, left + line.width, left + clipR});
            const int y0 = std::max({0, top, top + clipT}), y1 = std::min({height, top + line.height, top + clipB});
            for (int y = y0; y < y1; ++y) {
                const int f = fade[static_cast<std::size_t>(y)];
                const auto* src = line.pixels.data() + static_cast<std::size_t>(y - top) * line.width * 4;
                auto* dst = frame_.data() + static_cast<std::size_t>(y) * rowBytes;
                for (int x = x0; x < x1; ++x) {
                    const auto* sp = src + static_cast<std::size_t>(x - left) * 4;
                    if (!(sp[0] | sp[1] | sp[2] | sp[3])) continue;
                    int b = sp[0], g = sp[1], r = sp[2], a = sp[3];
                    if (f < 256) { b = (b * f) >> 8; g = (g * f) >> 8; r = (r * f) >> 8; a = (a * f) >> 8; }
                    auto* dp = dst + static_cast<std::size_t>(x) * 4;
                    if (style.background) {
                        const int inverse = 255 - a;
                        dp[0] = static_cast<std::uint8_t>(std::min(255, b + (bgB * inverse + 127) / 255));
                        dp[1] = static_cast<std::uint8_t>(std::min(255, g + (bgG * inverse + 127) / 255));
                        dp[2] = static_cast<std::uint8_t>(std::min(255, r + (bgR * inverse + 127) / 255));
                        dp[3] = 255;
                    } else {
                        dp[0] = static_cast<std::uint8_t>(b); dp[1] = static_cast<std::uint8_t>(g);
                        dp[2] = static_cast<std::uint8_t>(r); dp[3] = static_cast<std::uint8_t>(a);
                    }
                }
            }
        };
        const int lineLeft = static_cast<int>(std::lround((static_cast<CGFloat>(width) - maxWidth) * 0.5)) - pad;
        const bool activeQuiet = activeLine < subdued.size() && subdued[activeLine];
        for (std::size_t i = from; i < to; ++i) {
            if (tops[i] > H || tops[i] + heights[i] < 0) continue;
            const bool quiet = i < subdued.size() && subdued[i];
            const MacRgb& color = i < activeLine || quiet ? style.read : style.text;
            const int top = static_cast<int>(std::lround(tops[i] + fontSize * 0.125)) - pad;
            if (auto line = Lines::Get(lines_, job(i, color))) blit(*line, lineLeft, top, 0, 0, line->width, line->height);
            else missingLines_ = true;
            if (i != activeLine || quiet) continue;
            // Progressive highlight by character share (whole line when progress is 1): the
            // highlight-coloured copy of the line, clipped per wrapped row.
            const auto lit = static_cast<NSUInteger>(std::lround(std::clamp(highlightProgress, 0.0f, 1.0f) *
                                                                  static_cast<float>(lines[i].size())));
            if (lit == 0) continue;
            auto lightLine = Lines::Get(lines_, job(i, style.highlight));
            if (!lightLine) { missingLines_ = true; continue; }
            const double share = lightLine->length && !lines[i].empty()
                ? static_cast<double>(lightLine->length) / static_cast<double>(lines[i].size()) : 1.0;
            const auto litChars = static_cast<NSUInteger>(std::lround(static_cast<double>(lit) * share));
            for (const auto& row : lightLine->rows) {
                if (litChars <= row.start) break;
                int right = row.right;
                if (litChars < row.end && row.end > row.start)
                    right = row.left + static_cast<int>(std::lround(
                        static_cast<double>(row.right - row.left) *
                        static_cast<double>(litChars - row.start) / static_cast<double>(row.end - row.start)));
                blit(*lightLine, lineLeft, top, row.left, row.top, right, row.bottom);
            }
        }
        (void)activeQuiet;

        // Prepare, off the render thread, what the next line change needs.
        if (activeLine + 1 < lines.size()) {
            const std::size_t nextActive = activeLine + 1;
            for (std::size_t i = from; i < to; ++i) {
                const bool quiet = i < subdued.size() && subdued[i] && i != nextActive;
                Lines::Prefetch(lines_, job(i, i < nextActive || quiet ? style.read : style.text));
            }
            if (!(nextActive < subdued.size() && subdued[nextActive]))
                Lines::Prefetch(lines_, job(nextActive, style.highlight));
        }
        if (!activeQuiet) Lines::Prefetch(lines_, job(activeLine, style.highlight));
        return Upload(device, frame_, width, height, std::move(key));
    }
}

bool MacTextLayer::UpdateMessage(id<MTLDevice> device, const std::wstring& message, int width, int height,
                                 float fontScale, float verticalPosition, const MacTextStyle& style) {
    return UpdateTimed(device, {message}, 0, 0.0f, 0.0f, width, height, fontScale, verticalPosition, {},
                       style, 1);
}

bool MacTextLayer::UpdateBanner(id<MTLDevice> device, const std::wstring& text, int width, int height,
                                const MacTextStyle& style) {
    if (!device || text.empty() || width <= 0 || height <= 0) return false;
    std::wstring key = L"B|" + std::to_wstring(width) + L"x" + std::to_wstring(height) + L"|" +
                       StyleKey(style) + L"|" + text;
    if (device_ == device && texture_ && key == cacheKey_) return true;
    @autoreleasepool {
        Canvas canvas(width, height);
        if (!canvas.ok()) return false;
        NSString* string = ToNSString(text);
        // Largest size that keeps the whole line on one row (never wraps).
        CGFloat size = std::max<CGFloat>(14.0, static_cast<CGFloat>(height) / 22.0);
        MacTextStyle bannerStyle = style;
        bannerStyle.background = false;
        for (int attempt = 0; attempt < 12; ++attempt) {
            const NSSize extent = [string sizeWithAttributes:@{NSFontAttributeName: LyricsFont(style.font, size)}];
            if (extent.width <= static_cast<CGFloat>(width) * 0.92 || size <= 10.0) break;
            size *= 0.88;
        }
        NSFont* font = LyricsFont(style.font, size);
        NSDictionary* attributes = Attributes(bannerStyle, font, Color(style.text), size);
        NSMutableDictionary* single = [attributes mutableCopy];
        NSMutableParagraphStyle* paragraph = [attributes[NSParagraphStyleAttributeName] mutableCopy];
        paragraph.lineBreakMode = NSLineBreakByClipping;
        single[NSParagraphStyleAttributeName] = paragraph;
        [string drawWithRect:NSMakeRect(0, static_cast<CGFloat>(height) * 0.04, width, size * 1.4)
                     options:NSStringDrawingUsesLineFragmentOrigin attributes:single];
        return Upload(device, canvas.pixels, width, height, std::move(key));
    }
}

bool MacTextLayer::Upload(id<MTLDevice> device, const std::vector<std::uint8_t>& pixels, int width, int height,
                          std::wstring key) {
    if (!texture_ || device_ != device || static_cast<int>(texture_.width) != width ||
        static_cast<int>(texture_.height) != height) {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        texture_ = [device newTextureWithDescriptor:descriptor];
        if (!texture_) return false;
        device_ = device;
    }
    [texture_ replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0
                  withBytes:pixels.data() bytesPerRow:static_cast<NSUInteger>(width) * 4];
    cacheKey_ = std::move(key);
    return true;
}

bool MacTextLayer::Draw(id<MTLRenderCommandEncoder> encoder) {
    if (!encoder || !texture_) return false;
    id<MTLDevice> device = encoder.device;
    if (!pipeline_ || pipelineDevice_ != device) {
        NSError* error = nil;
        NSString* source = @"#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "struct Out { float4 position [[position]]; float2 uv; };\n"
            "vertex Out vertex_main(uint id [[vertex_id]]) {\n"
            "  float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)};\n"
            "  float2 u[3] = {float2(0,1),float2(2,1),float2(0,-1)};\n"
            "  Out o; o.position=float4(p[id],0,1); o.uv=u[id]; return o; }\n"
            "fragment float4 fragment_main(Out in [[stage_in]], texture2d<float> image [[texture(0)]]) {\n"
            "  constexpr sampler s(address::clamp_to_zero, filter::linear); return image.sample(s,in.uv); }";
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) return false;
        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = [library newFunctionWithName:@"vertex_main"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
        MTLRenderPipelineColorAttachmentDescriptor* attachment = descriptor.colorAttachments[0];
        attachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
        attachment.blendingEnabled = YES;
        // The bitmap is premultiplied, so source colour is already scaled by its alpha.
        attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipeline_ = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline_) return false;
        pipelineDevice_ = device;
    }
    [encoder setRenderPipelineState:pipeline_];
    [encoder setFragmentTexture:texture_ atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    return true;
}
#endif
