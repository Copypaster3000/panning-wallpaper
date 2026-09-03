#pragma once

#include "image_decoder.h"
#include "panning.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>

namespace panning_wallpaper {

class Renderer final {
public:
    [[nodiscard]] HRESULT Initialize(
        HWND window,
        const DecodedImage& image,
        const PanningConfiguration& configuration,
        std::wstring& errorDetail);
    [[nodiscard]] HRESULT Resize(UINT width, UINT height);
    [[nodiscard]] HRESULT UpdateConfiguration(
        const PanningConfiguration& configuration);
    [[nodiscard]] HRESULT RenderAndPresent(double progress);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    [[nodiscard]] HRESULT CreateRenderTarget();
    [[nodiscard]] HRESULT CreateImageTexture(const DecodedImage& image);
    [[nodiscard]] HRESULT CreatePipeline(std::wstring& errorDetail);
    [[nodiscard]] HRESULT CreateSampler(
        const PanningConfiguration& configuration,
        Microsoft::WRL::ComPtr<ID3D11SamplerState>& sampler) const;
    void UpdateViewport(UINT width, UINT height) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> imageTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> imageView_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> frameConstants_;
    UINT renderWidth_ = 0;
    UINT renderHeight_ = 0;
    UINT imageWidth_ = 0;
    UINT imageHeight_ = 0;
    PanningConfiguration configuration_;
};

}  // namespace panning_wallpaper
