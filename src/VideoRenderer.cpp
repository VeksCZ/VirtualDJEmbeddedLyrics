#include "VideoRenderer.hpp"

#ifdef _WIN32
#include <d3dcompiler.h>

namespace {
struct Vertex { float x, y, u, v; };

constexpr char ShaderSource[] = R"(
Texture2D lyricsTexture : register(t0);
SamplerState lyricsSampler : register(s0);
struct VSInput { float2 position : POSITION; float2 uv : TEXCOORD0; };
struct PSInput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
PSInput VSMain(VSInput input) {
    PSInput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    return output;
}
float4 PSMain(PSInput input) : SV_TARGET {
    return lyricsTexture.Sample(lyricsSampler, input.uv);
}
)";
}

bool VideoRenderer::Initialize(ID3D11Device* device) {
    Reset();
    if (!device) return false;
    device_ = device;
    device_->GetImmediateContext(&context_);
    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    if (FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource), nullptr, nullptr, nullptr,
                          "VSMain", "vs_4_0", 0, 0, &vsBlob, &errors)) ||
        FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource), nullptr, nullptr, nullptr,
                          "PSMain", "ps_4_0", 0, 0, &psBlob, &errors))) return false;
    if (FAILED(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader_)) ||
        FAILED(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader_))) return false;
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device_->CreateInputLayout(elements, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout_))) return false;
    const Vertex vertices[] = {
        {-1,  1, 0, 0}, { 1,  1, 1, 0}, {-1, -1, 0, 1},
        {-1, -1, 0, 1}, { 1,  1, 1, 0}, { 1, -1, 1, 1},
    };
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA bufferData{vertices, 0, 0};
    if (FAILED(device_->CreateBuffer(&bufferDesc, &bufferData, &vertexBuffer_))) return false;
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&samplerDesc, &sampler_))) return false;
    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&blendDesc, &blendState_))) return false;
    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device_->CreateDepthStencilState(&depthDesc, &depthState_))) return false;
    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    return SUCCEEDED(device_->CreateRasterizerState(&rasterDesc, &rasterizerState_));
}

void VideoRenderer::Reset() {
    rasterizerState_.Reset(); depthState_.Reset(); blendState_.Reset(); sampler_.Reset(); vertexBuffer_.Reset(); inputLayout_.Reset();
    pixelShader_.Reset(); vertexShader_.Reset(); context_.Reset(); device_.Reset();
}

bool VideoRenderer::Draw(ID3D11ShaderResourceView* texture) {
    if (!context_ || !texture) return false;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> oldLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> oldVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> oldPs;
    Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepth;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> oldBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> oldSrv;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> oldSampler;
    UINT oldStride = 0, oldOffset = 0;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
    FLOAT oldFactor[4]{}; UINT oldMask = 0, oldStencilRef = 0;
    context_->IAGetInputLayout(&oldLayout);
    context_->IAGetVertexBuffers(0, 1, &oldBuffer, &oldStride, &oldOffset);
    context_->IAGetPrimitiveTopology(&oldTopology);
    context_->VSGetShader(&oldVs, nullptr, nullptr);
    context_->PSGetShader(&oldPs, nullptr, nullptr);
    context_->PSGetShaderResources(0, 1, &oldSrv);
    context_->PSGetSamplers(0, 1, &oldSampler);
    context_->OMGetBlendState(&oldBlend, oldFactor, &oldMask);
    context_->OMGetDepthStencilState(&oldDepth, &oldStencilRef);
    context_->RSGetState(&oldRasterizer);
    const UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* buffer = vertexBuffer_.Get();
    ID3D11ShaderResourceView* srv = texture;
    ID3D11SamplerState* sampler = sampler_.Get();
    const FLOAT blendFactor[4]{};
    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, &sampler);
    context_->OMSetBlendState(blendState_.Get(), blendFactor, 0xffffffff);
    context_->OMSetDepthStencilState(depthState_.Get(), 0);
    context_->RSSetState(rasterizerState_.Get());
    context_->Draw(6, 0);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSrv);
    buffer = oldBuffer.Get(); srv = oldSrv.Get(); sampler = oldSampler.Get();
    context_->IASetInputLayout(oldLayout.Get());
    context_->IASetVertexBuffers(0, 1, &buffer, &oldStride, &oldOffset);
    context_->IASetPrimitiveTopology(oldTopology);
    context_->VSSetShader(oldVs.Get(), nullptr, 0);
    context_->PSSetShader(oldPs.Get(), nullptr, 0);
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, &sampler);
    context_->OMSetBlendState(oldBlend.Get(), oldFactor, oldMask);
    context_->OMSetDepthStencilState(oldDepth.Get(), oldStencilRef);
    context_->RSSetState(oldRasterizer.Get());
    return true;
}
#endif
