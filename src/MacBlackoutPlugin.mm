#ifdef __APPLE__
#import <Metal/Metal.h>

#include "vdjVideo8.h"

#include <cstring>

#ifndef LRC_PLUGIN_VERSION
#define LRC_PLUGIN_VERSION "0.0.0-dev"
#endif

class MacBlackoutPlugin final : public IVdjPluginVideoFx8 {
public:
    HRESULT VDJ_API OnGetPluginInfo(TVdjPluginInfo8* info) override {
        info->PluginName = "LRC BlackOut";
        info->Author = "Slava / OpenAI";
        info->Description = "Pure black video curtain for macOS Metal";
        info->Version = LRC_PLUGIN_VERSION;
        info->Flags = VDJFLAG_PROCESSFIRST | VDJFLAG_VIDEO_MASTERONLY | VDJFLAG_VIDEO_OVERLAY;
        info->Bitmap = nullptr;
        return S_OK;
    }

    ULONG VDJ_API Release() override { delete this; return 0; }
    HRESULT VDJ_API OnDeviceClose() override { pipeline_ = nil; device_ = nil; return S_OK; }

    HRESULT VDJ_API OnDraw() override {
        void* encoderPointer = nullptr;
        if (GetDevice(VdjVideoEngineMetal, &encoderPointer) != S_OK || !encoderPointer) return S_FALSE;
        id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)encoderPointer;
        if (!EnsurePipeline(encoder.device)) return S_FALSE;
        [encoder setRenderPipelineState:pipeline_];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        return S_OK;
    }

private:
    bool EnsurePipeline(id<MTLDevice> device) {
        if (pipeline_ && device_ == device) return true;
        NSError* error = nil;
        NSString* source = @"#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "vertex float4 vertex_main(uint id [[vertex_id]]) {\n"
            " float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; return float4(p[id],0,1);}\n"
            "fragment float4 fragment_main(){return float4(0,0,0,1);}";
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) return false;
        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = [library newFunctionWithName:@"vertex_main"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pipeline_ = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline_) return false;
        device_ = device;
        return true;
    }

    id<MTLDevice> __strong device_ = nil;
    id<MTLRenderPipelineState> __strong pipeline_ = nil;
};

extern "C" VDJ_EXPORT HRESULT VDJ_API DllGetClassObject(
    const GUID& classId, const GUID& interfaceId, void** object) {
    if (!object) return -1;
    *object = nullptr;
    if (std::memcmp(&classId, &CLSID_VdjPlugin8, sizeof(GUID)) != 0 ||
        std::memcmp(&interfaceId, &IID_IVdjPluginVideoFx8, sizeof(GUID)) != 0)
        return CLASS_E_CLASSNOTAVAILABLE;
    *object = new MacBlackoutPlugin();
    return S_OK;
}
#endif
