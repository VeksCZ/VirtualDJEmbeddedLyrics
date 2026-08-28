#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

class BlackoutRenderer {
public:
    bool Initialize(ID3D11Device* device);
    void Reset();
    bool Draw();

private:
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;
};
#endif
