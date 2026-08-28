#ifdef _WIN32
#include "BlackoutRenderer.hpp"
#include "VideoRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <iostream>

using Microsoft::WRL::ComPtr;

namespace {
bool CreateWarpDevice(ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context) {
    return SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context));
}

bool CreateTarget(ID3D11Device* device, UINT width, UINT height, ComPtr<ID3D11Texture2D>& texture,
                  ComPtr<ID3D11RenderTargetView>& target) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)) &&
           SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &target));
}

bool PixelIs(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
             std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData) +
        (desc.Height / 2) * mapped.RowPitch + (desc.Width / 2) * 4;
    const bool matches = pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == a;
    context->Unmap(staging.Get(), 0);
    return matches;
}
}

int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if (!CreateWarpDevice(device, context)) { std::cerr << "WARP device creation failed\n"; return 1; }
    ComPtr<ID3D11Texture2D> output;
    ComPtr<ID3D11RenderTargetView> target;
    if (!CreateTarget(device.Get(), 64, 64, output, target)) return 2;
    ID3D11RenderTargetView* targetPointer = target.Get();
    context->OMSetRenderTargets(1, &targetPointer, nullptr);
    D3D11_VIEWPORT viewport{0, 0, 64, 64, 0, 1};
    context->RSSetViewports(1, &viewport);

    constexpr float red[4]{1, 0, 0, 1};
    context->ClearRenderTargetView(target.Get(), red);
    BlackoutRenderer blackout;
    if (!blackout.Initialize(device.Get()) || !blackout.Draw() ||
        !PixelIs(device.Get(), context.Get(), output.Get(), 0, 0, 0, 255)) {
        std::cerr << "Blackout WARP render failed\n"; return 3;
    }

    constexpr std::uint32_t white = 0xffffffffu;
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = textureDesc.Height = textureDesc.MipLevels = textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE; textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{&white, 4, 0};
    ComPtr<ID3D11Texture2D> overlay;
    ComPtr<ID3D11ShaderResourceView> overlayView;
    if (FAILED(device->CreateTexture2D(&textureDesc, &data, &overlay)) ||
        FAILED(device->CreateShaderResourceView(overlay.Get(), nullptr, &overlayView))) return 4;
    VideoRenderer video;
    if (!video.Initialize(device.Get()) || !video.Draw(overlayView.Get()) ||
        !PixelIs(device.Get(), context.Get(), output.Get(), 255, 255, 255, 255)) {
        std::cerr << "Overlay WARP render failed\n"; return 5;
    }
    return 0;
}
#else
int main() { return 0; }
#endif
