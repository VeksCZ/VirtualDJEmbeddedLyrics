#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

class TextTexture {
public:
    bool Initialize(ID3D11Device* device);
    void Reset();
    bool Update(const std::wstring& current, const std::wstring& next,
                float progress, int width, int height, float fontScale = 1.0f,
                float verticalPosition = 0.5f);
    bool UpdateTimed(const std::vector<std::wstring>& lines, std::size_t activeLine,
                     float progress, int width, int height, float fontScale = 1.0f,
                     float verticalPosition = 0.5f);
    bool UpdatePage(const std::vector<std::wstring>& lines, std::size_t page,
                    std::size_t pageSize, std::size_t activeLine, int width, int height,
                    float fontScale = 1.0f, float verticalPosition = 0.5f);
    bool UpdateMessage(const std::wstring& message, int width, int height,
                       float fontScale = 1.0f, float verticalPosition = 0.5f);
    ID3D11ShaderResourceView* View() const noexcept { return view_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    std::wstring cacheKey_;
};
#endif
