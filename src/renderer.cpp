#include "renderer.h"

#include <array>

namespace panning_wallpaper {

HRESULT Renderer::Initialize(HWND window) {
    RECT clientRectangle{};
    if (!GetClientRect(window, &clientRectangle)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    const UINT width = static_cast<UINT>(clientRectangle.right - clientRectangle.left);
    const UINT height = static_cast<UINT>(clientRectangle.bottom - clientRectangle.top);
    if (width == 0 || height == 0) {
        return E_INVALIDARG;
    }

    constexpr std::array requestedFeatureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requestedFeatureLevels.data(),
        static_cast<UINT>(requestedFeatureLevels.size()),
        D3D11_SDK_VERSION,
        &device_,
        &createdFeatureLevel,
        &deviceContext_);
    if (FAILED(result)) {
        return result;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    result = device_.As(&dxgiDevice);
    if (FAILED(result)) {
        return result;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    result = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(result)) {
        return result;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    result = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        return result;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width;
    description.Height = height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    result = factory->CreateSwapChainForHwnd(
        device_.Get(), window, &description, nullptr, nullptr, &swapChain_);
    if (FAILED(result)) {
        return result;
    }

    result = factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(result)) {
        return result;
    }

    return CreateRenderTarget();
}

HRESULT Renderer::Resize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return S_OK;
    }

    deviceContext_->OMSetRenderTargets(0, nullptr, nullptr);
    renderTarget_.Reset();

    const HRESULT result =
        swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result)) {
        return result;
    }

    return CreateRenderTarget();
}

HRESULT Renderer::RenderAndPresent() {
    if (!renderTarget_) {
        return E_UNEXPECTED;
    }

    constexpr std::array testColor{0.02F, 0.28F, 0.42F, 1.0F};
    ID3D11RenderTargetView* const targets[] = {renderTarget_.Get()};
    deviceContext_->OMSetRenderTargets(1, targets, nullptr);
    deviceContext_->ClearRenderTargetView(renderTarget_.Get(), testColor.data());

    return swapChain_->Present(1, 0);
}

void Renderer::Shutdown() noexcept {
    if (deviceContext_) {
        deviceContext_->OMSetRenderTargets(0, nullptr, nullptr);
        deviceContext_->ClearState();
    }

    renderTarget_.Reset();
    swapChain_.Reset();
    deviceContext_.Reset();
    device_.Reset();
}

bool Renderer::IsInitialized() const noexcept {
    return swapChain_ != nullptr && renderTarget_ != nullptr;
}

HRESULT Renderer::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(result)) {
        return result;
    }

    result = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget_);
    return result;
}

}  // namespace panning_wallpaper
