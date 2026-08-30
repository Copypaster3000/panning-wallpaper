#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace panning_wallpaper {

class Renderer final {
public:
    [[nodiscard]] HRESULT Initialize(HWND window);
    [[nodiscard]] HRESULT Resize(UINT width, UINT height);
    [[nodiscard]] HRESULT RenderAndPresent();
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    [[nodiscard]] HRESULT CreateRenderTarget();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
};

}  // namespace panning_wallpaper
