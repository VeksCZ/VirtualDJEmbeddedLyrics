// Measures how long TextTexture takes to produce a lyrics frame (the work LRC Master does on
// every frame while a line is highlighted or scrolled), and can dump frames for comparison.
// Usage: texture_bench [--dump DIR]
#include "TextTexture.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
bool Dump(ID3D11Device* device, ID3D11ShaderResourceView* view, const std::string& path) {
    if (!view) return false;
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    std::ofstream out(path, std::ios::binary);
    for (UINT row = 0; row < desc.Height; ++row)
        out.write(static_cast<const char*>(mapped.pData) + row * mapped.RowPitch, desc.Width * 4);
    context->Unmap(staging.Get(), 0);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    const char* dumpDir = argc > 2 && std::strcmp(argv[1], "--dump") == 0 ? argv[2] : nullptr;
    const int width = 1920, height = 1080;
    ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, nullptr, nullptr))) {
        std::puts("D3D11 device failed");
        return 1;
    }
    std::vector<std::wstring> lines;
    for (int i = 0; i < 60; ++i)
        lines.push_back(i % 7 == 3
            ? L"> 5 <"
            : L"There's a whisper in the night, oh-oh, and it calls my name, and it calls my name again " + std::to_wstring(i));
    std::vector<bool> subdued(lines.size(), false);
    subdued[17] = true;

    struct Case { const char* name; int font, backdrop, strength; bool background; std::size_t target; };
    const Case cases[] = {
        {"Arial outline normal, 7 lines", 0, 0, 1, false, 7},
        {"Verdana shadow normal, 4 lines", 2, 1, 1, false, 4},
        {"Verdana shadow normal, 4 lines + background", 2, 1, 1, true, 4},
        {"Verdana outline+shadow strong, 4 lines + background", 2, 2, 2, true, 4},
    };
    int caseIndex = 0;
    for (const auto& c : cases) {
        TextTexture texture;
        texture.Initialize(device.Get());
        constexpr int frames = 120;
        const auto start = std::chrono::steady_clock::now();
        double worst = 0.0; int worstFrame = 0;
        for (int f = 0; f < frames; ++f) {
            const auto frameStart = std::chrono::steady_clock::now();
            // A new active line every 40 frames, like a real song (each line ~1.3 s at 30 fps).
            const std::size_t active = 15 + f / 40;
            const float progress = static_cast<float>(f % 40) / 40.0f;
            texture.UpdateTimed(lines, active, progress, progress, width, height, 1.08f, 0.5f, subdued,
                                0x00ffffffu, 0x0000d2ffu, 0x00969696u, c.font, c.backdrop, c.strength,
                                c.background, 0x00402010u, c.target);
            const double took = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - frameStart).count();
            if (f > 0 && took > worst) { worst = took; worstFrame = f; }
            if (took > 15.0) std::printf("    frame %d: %.1f ms\n", f, took);
            std::this_thread::sleep_until(frameStart + std::chrono::milliseconds(33));
            if (dumpDir && f % 13 == 0) {
                const std::string path = std::string(dumpDir) + "/case" + std::to_string(caseIndex) +
                                         "_f" + std::to_string(f) + ".raw";
                if (!Dump(device.Get(), texture.View(), path)) std::printf("dump failed: %s\n", path.c_str());
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / frames;
        std::printf("%6.1f ms/frame avg, %6.1f ms worst (frame %d)  %s (%dx%d)\n", ms, worst, worstFrame, c.name, width, height);
        ++caseIndex;
    }
    return 0;
}
