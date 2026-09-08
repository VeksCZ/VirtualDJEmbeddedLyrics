#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>

#include "AsyncLyricsLoader.hpp"
#include "LyricsTiming.hpp"
#include "MasterDeckSelector.hpp"
#include "vdjVideo8.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef LRC_PLUGIN_VERSION
#define LRC_PLUGIN_VERSION "0.0.0-dev"
#endif

namespace {
NSString* ToNSString(const std::wstring& value) {
    if (value.empty()) return @"";
    return [[NSString alloc] initWithBytes:value.data()
                                    length:value.size() * sizeof(wchar_t)
                                  encoding:NSUTF32LittleEndianStringEncoding];
}

class MetalTextOverlay {
public:
    void Reset() {
        texture_ = nil;
        pipeline_ = nil;
        device_ = nil;
        cacheKey_.clear();
    }

    bool Update(id<MTLDevice> device, const std::vector<std::wstring>& lines,
                std::size_t activeLine, int width, int height,
                float fontScale, float verticalPosition) {
        if (!device || lines.empty() || activeLine >= lines.size() || width <= 0 || height <= 0)
            return false;
        std::wstring key = std::to_wstring(activeLine) + L":" + std::to_wstring(width) + L"x" +
                           std::to_wstring(height) + L":" + std::to_wstring(fontScale) + L":" +
                           std::to_wstring(verticalPosition);
        for (const auto& line : lines) key += L"\n" + line;
        if (device_ == device && texture_ && key == cacheKey_) return true;

        @autoreleasepool {
            const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
            std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(height), 0);
            CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(
                pixels.data(), width, height, 8, rowBytes, colorSpace,
                kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
            CGColorSpaceRelease(colorSpace);
            if (!context) return false;

            NSGraphicsContext* graphics = [NSGraphicsContext graphicsContextWithCGContext:context flipped:YES];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:graphics];

            const CGFloat fontSize = std::max<CGFloat>(18.0,
                std::max<CGFloat>(36.0, static_cast<CGFloat>(height) / 13.0) *
                std::clamp(fontScale, 0.5f, 2.0f));
            NSFont* font = [NSFont boldSystemFontOfSize:fontSize];
            NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc] init];
            paragraph.alignment = NSTextAlignmentCenter;
            paragraph.lineBreakMode = NSLineBreakByWordWrapping;
            const CGFloat maxWidth = static_cast<CGFloat>(width) * 0.9;

            std::vector<CGFloat> heights;
            heights.reserve(lines.size());
            CGFloat totalHeight = 0.0;
            for (const auto& line : lines) {
                NSDictionary* attributes = @{NSFontAttributeName: font,
                                              NSParagraphStyleAttributeName: paragraph};
                const NSRect bounds = [ToNSString(line)
                    boundingRectWithSize:NSMakeSize(maxWidth, CGFLOAT_MAX)
                    options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                    attributes:attributes];
                const CGFloat lineHeight = std::max<CGFloat>(fontSize * 1.2, std::ceil(bounds.size.height));
                heights.push_back(lineHeight);
                totalHeight += lineHeight;
            }

            CGFloat activeOffset = 0.0;
            for (std::size_t index = 0; index < activeLine; ++index) activeOffset += heights[index];
            CGFloat y = static_cast<CGFloat>(height) * std::clamp(verticalPosition, 0.1f, 0.9f)
                        - activeOffset - heights[activeLine] * 0.5;
            const CGFloat minimumY = 8.0;
            const CGFloat maximumY = std::max(minimumY, static_cast<CGFloat>(height) - totalHeight - 8.0);
            y = std::clamp(y, minimumY, maximumY);

            for (std::size_t index = 0; index < lines.size(); ++index) {
                NSColor* color = index < activeLine
                    ? [NSColor colorWithCalibratedWhite:0.58 alpha:1.0]
                    : index == activeLine
                        ? [NSColor colorWithCalibratedRed:1.0 green:0.82 blue:0.0 alpha:1.0]
                        : NSColor.whiteColor;
                NSShadow* shadow = [[NSShadow alloc] init];
                shadow.shadowColor = [NSColor colorWithCalibratedWhite:0.0 alpha:0.9];
                shadow.shadowBlurRadius = std::max<CGFloat>(2.0, fontSize / 12.0);
                shadow.shadowOffset = NSMakeSize(0.0, 2.0);
                NSDictionary* attributes = @{
                    NSFontAttributeName: font,
                    NSForegroundColorAttributeName: color,
                    NSStrokeColorAttributeName: NSColor.blackColor,
                    NSStrokeWidthAttributeName: @(-3.0),
                    NSParagraphStyleAttributeName: paragraph,
                    NSShadowAttributeName: shadow,
                };
                [ToNSString(lines[index]) drawWithRect:NSMakeRect(
                    (static_cast<CGFloat>(width) - maxWidth) * 0.5, y, maxWidth, heights[index])
                    options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                    attributes:attributes];
                y += heights[index];
            }

            [NSGraphicsContext restoreGraphicsState];
            CGContextRelease(context);

            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:width height:height mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
            if (!texture) return false;
            [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                       mipmapLevel:0 withBytes:pixels.data() bytesPerRow:rowBytes];
            device_ = device;
            texture_ = texture;
            cacheKey_ = std::move(key);
        }
        return true;
    }

    bool Draw(id<MTLRenderCommandEncoder> encoder) {
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
            attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
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

private:
    id<MTLDevice> __strong device_ = nil;
    id<MTLDevice> __strong pipelineDevice_ = nil;
    id<MTLTexture> __strong texture_ = nil;
    id<MTLRenderPipelineState> __strong pipeline_ = nil;
    std::wstring cacheKey_;
};
}

class MacLyricsPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnLoad() override {
        if (DeclareParameterSlider(&fontSize_, 1, "Font size", "Size", 1.0f / 3.0f) != S_OK ||
            DeclareParameterSlider(&timedLines_, 2, "Timed lines", "Timed lines", 2.0f / 7.0f) != S_OK ||
            DeclareParameterSlider(&pageLines_, 3, "Untimed lines", "Untimed lines", 2.0f / 7.0f) != S_OK ||
            DeclareParameterSlider(&verticalPosition_, 4, "Vertical position", "Position", 0.5f) != S_OK ||
            DeclareParameterSwitch(&useVolumeFaders_, 5, "Upfaders", "Upfaders", false) != S_OK ||
            DeclareParameterButton(&nextLine_, 7, "Next line", "Next") != S_OK ||
            DeclareParameterButton(&previousLine_, 8, "Previous line", "Prev") != S_OK ||
            DeclareParameterSwitch(&autoTagLrc_, 11, "Add #lrc to User 1", "Auto-tag #lrc", true) != S_OK)
            return -1;
        return S_OK;
    }

    HRESULT VDJ_API OnParameter(int id) override {
        if (id == 7 && nextLine_) {
            if (!lyrics_.synchronized && activeLine_ + 1 < lyrics_.lines.size()) ++activeLine_;
            nextLine_ = 0;
            overlay_.Reset();
        } else if (id == 8 && previousLine_) {
            if (!lyrics_.synchronized && activeLine_ > 0) --activeLine_;
            previousLine_ = 0;
            overlay_.Reset();
        }
        return S_OK;
    }

    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
        info->PluginName = "LRC Master";
        info->Author = "Slava / OpenAI";
        info->Description = "Timed embedded/LRC lyrics for macOS Metal";
        info->Version = LRC_PLUGIN_VERSION;
        info->Flags = VDJFLAG_PROCESSLAST | VDJFLAG_VIDEO_MASTERONLY | VDJFLAG_VIDEO_OVERLAY;
        info->Bitmap = nullptr;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }
    HRESULT VDJ_API OnDeviceClose() override { overlay_.Reset(); return S_OK; }

    HRESULT VDJ_API OnDraw() override {
        void* encoderPointer = nullptr;
        if (GetDevice(VdjVideoEngineMetal, &encoderPointer) != S_OK || !encoderPointer) return S_FALSE;
        id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPointer;
        const int deck = VisibleVideoDeck();
        if (deck <= 0) return S_OK;

        char pathBuffer[4096]{};
        char command[128]{};
        std::snprintf(command, sizeof(command), "deck %d get_filepath", deck);
        if (GetStringInfo(command, pathBuffer, sizeof(pathBuffer)) != S_OK) return S_OK;
        const std::filesystem::path path{std::u8string(
            reinterpret_cast<const char8_t*>(pathBuffer))};
        if (path.empty()) return S_OK;
        if (path != loadedPath_) {
            loadedPath_ = path;
            lyrics_ = {};
            activeLine_ = 0;
            overlay_.Reset();
            loader_.Request(path);
        }
        if (auto completed = loader_.Poll(); completed && completed->path == loadedPath_) {
            lyrics_ = std::move(completed->result.document);
            overlay_.Reset();
            if (!lyrics_.empty() && autoTagLrc_) EnsureLrcHashtag(deck);
        }

        std::vector<std::wstring> visible;
        std::size_t visibleActive = 0;
        if (lyrics_.empty()) {
            visible.push_back(L"...");
        } else if (!lyrics_.synchronized) {
            const std::size_t first = activeLine_ > 2 ? activeLine_ - 2 : 0;
            const std::size_t end = std::min(lyrics_.lines.size(), first + PageSize());
            for (std::size_t index = first; index < end; ++index)
                visible.push_back(lyrics_.lines[index].text);
            visibleActive = activeLine_ - first;
        } else {
            double elapsed = 0.0;
            std::snprintf(command, sizeof(command), "deck %d get_time 'elapsed' 'absolute'", deck);
            if (GetInfo(command, &elapsed) != S_OK) return S_OK;
            const auto now = static_cast<std::int64_t>(elapsed);
            const auto iterator = std::upper_bound(
                lyrics_.lines.begin(), lyrics_.lines.end(), now,
                [](std::int64_t value, const LyricLine& line) { return value < line.timeMs; });
            activeLine_ = iterator == lyrics_.lines.begin() ? 0
                : static_cast<std::size_t>(std::distance(lyrics_.lines.begin(), iterator) - 1);
            const std::size_t first = activeLine_ > 2 ? activeLine_ - 2 : 0;
            const std::size_t end = std::min(lyrics_.lines.size(), activeLine_ + TimedLineCount());
            for (std::size_t index = first; index < end; ++index)
                visible.push_back(lyrics_.lines[index].text);
            visibleActive = activeLine_ - first;
        }

        if (!overlay_.Update(encoder.device, visible, visibleActive, width, height,
                             FontScale(), VerticalPosition())) return S_FALSE;
        return overlay_.Draw(encoder) ? S_OK : S_FALSE;
    }

private:
    int VisibleVideoDeck() {
        double balance = 0.0, left = 1.0, right = 2.0;
        GetInfo("get_leftdeck", &left);
        GetInfo("get_rightdeck", &right);
        const char* query = useVolumeFaders_ ? "get_crossfader_result" : "video_crossfader";
        if (GetInfo(query, &balance) != S_OK) return selector_.Current();
        return selector_.Select(balance, static_cast<int>(left), static_cast<int>(right));
    }

    void EnsureLrcHashtag(int deck) {
        char command[256]{}, user1[4096]{};
        std::snprintf(command, sizeof(command), "deck %d get_loaded_song 'user 1'", deck);
        if (GetStringInfo(command, user1, sizeof(user1)) != S_OK) return;
        std::string value{user1};
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value.find("#lrc") != std::string::npos) return;
        std::snprintf(command, sizeof(command), "deck %d loaded_song_hashtag 'user 1' '#lrc'", deck);
        SendCommand(command);
    }

    float FontScale() const { return 0.5f + std::clamp(fontSize_, 0.0f, 1.0f) * 1.5f; }
    float VerticalPosition() const { return 0.1f + std::clamp(verticalPosition_, 0.0f, 1.0f) * 0.8f; }
    std::size_t PageSize() const { return 5 + static_cast<std::size_t>(std::clamp(pageLines_, 0.0f, 1.0f) * 7.0f + 0.5f); }
    std::size_t TimedLineCount() const { return 5 + static_cast<std::size_t>(std::clamp(timedLines_, 0.0f, 1.0f) * 7.0f + 0.5f); }

    MetalTextOverlay overlay_;
    AsyncLyricsLoader loader_;
    MasterDeckSelector selector_;
    std::filesystem::path loadedPath_;
    LyricsDocument lyrics_;
    std::size_t activeLine_{};
    float fontSize_{1.0f / 3.0f};
    float timedLines_{2.0f / 7.0f};
    float pageLines_{2.0f / 7.0f};
    float verticalPosition_{0.5f};
    int useVolumeFaders_{};
    int nextLine_{};
    int previousLine_{};
    int autoTagLrc_{1};
};

extern "C" VDJ_EXPORT HRESULT VDJ_API DllGetClassObject(
    const GUID& classId, const GUID& interfaceId, void** object) {
    if (!object) return -1;
    *object = nullptr;
    if (std::memcmp(&classId, &CLSID_VdjPlugin8, sizeof(GUID)) != 0 ||
        std::memcmp(&interfaceId, &IID_IVdjPluginVideoFx8, sizeof(GUID)) != 0)
        return CLASS_E_CLASSNOTAVAILABLE;
    *object = new MacLyricsPlugin();
    return S_OK;
}
#endif
