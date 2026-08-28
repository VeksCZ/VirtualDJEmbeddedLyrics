#include "BlackoutRenderer.hpp"

#ifdef _WIN32
#include <d3dcompiler.h>

namespace {
constexpr char ShaderSource[] = R"(
float4 VSMain(uint vertexId : SV_VertexID) : SV_POSITION {
    float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 PSMain() : SV_TARGET { return float4(0.0, 0.0, 0.0, 1.0); }
)";
}

bool BlackoutRenderer::Initialize(ID3D11Device* device) {
    Reset();
    if (!device) return false;
    device->GetImmediateContext(&context_);
    if (!context_) return false;
    Microsoft::WRL::ComPtr<ID3DBlob> vertexCode, pixelCode, errors;
    if (FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource), nullptr, nullptr, nullptr,
                          "VSMain", "vs_4_0", 0, 0, &vertexCode, &errors)) ||
        FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource), nullptr, nullptr, nullptr,
                          "PSMain", "ps_4_0", 0, 0, &pixelCode, &errors))) return false;
    if (FAILED(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_)) ||
        FAILED(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader_))) return false;

    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blend, &blendState_))) return false;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device->CreateDepthStencilState(&depth, &depthState_))) return false;
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    return SUCCEEDED(device->CreateRasterizerState(&rasterizer, &rasterizerState_));
}

void BlackoutRenderer::Reset() {
    rasterizerState_.Reset();
    depthState_.Reset();
    blendState_.Reset();
    pixelShader_.Reset();
    vertexShader_.Reset();
    context_.Reset();
}

bool BlackoutRenderer::Draw() {
    if (!context_ || !vertexShader_ || !pixelShader_) return false;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    context_->OMGetRenderTargets(1, &target, nullptr);
    if (!target) return false;

    Microsoft::WRL::ComPtr<ID3D11InputLayout> oldLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> oldVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> oldPs;
    Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepth;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizer;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
    FLOAT oldFactor[4]{};
    UINT oldMask = 0, oldStencil = 0;
    context_->IAGetInputLayout(&oldLayout);
    context_->IAGetPrimitiveTopology(&oldTopology);
    context_->VSGetShader(&oldVs, nullptr, nullptr);
    context_->PSGetShader(&oldPs, nullptr, nullptr);
    context_->OMGetBlendState(&oldBlend, oldFactor, &oldMask);
    context_->OMGetDepthStencilState(&oldDepth, &oldStencil);
    context_->RSGetState(&oldRasterizer);

    constexpr FLOAT factor[4]{};
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context_->OMSetBlendState(blendState_.Get(), factor, 0xffffffff);
    context_->OMSetDepthStencilState(depthState_.Get(), 0);
    context_->RSSetState(rasterizerState_.Get());
    context_->Draw(3, 0);

    context_->IASetInputLayout(oldLayout.Get());
    context_->IASetPrimitiveTopology(oldTopology);
    context_->VSSetShader(oldVs.Get(), nullptr, 0);
    context_->PSSetShader(oldPs.Get(), nullptr, 0);
    context_->OMSetBlendState(oldBlend.Get(), oldFactor, oldMask);
    context_->OMSetDepthStencilState(oldDepth.Get(), oldStencil);
    context_->RSSetState(oldRasterizer.Get());
    return true;
}
#endif
