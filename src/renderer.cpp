#include "renderer.h"

#include <d3dcompiler.h>

#include <array>
#include <cstring>
#include <string_view>

namespace {

constexpr std::string_view kVertexShaderSource = R"(
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 textureCoordinate : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    float2 coordinate = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(
        coordinate * float2(2.0, -2.0) + float2(-1.0, 1.0),
        0.0,
        1.0);
    output.textureCoordinate = coordinate;
    return output;
}
)";

constexpr std::string_view kPixelShaderSource = R"(
cbuffer FrameConstants : register(b0)
{
    float horizontalOffset;
    float horizontalScale;
    float2 padding;
};

Texture2D wallpaperTexture : register(t0);
SamplerState wallpaperSampler : register(s0);

float4 main(float4 position : SV_POSITION, float2 textureCoordinate : TEXCOORD0)
    : SV_TARGET
{
    float2 sampleCoordinate = float2(
        textureCoordinate.x * horizontalScale + horizontalOffset,
        textureCoordinate.y);
    return wallpaperTexture.Sample(wallpaperSampler, sampleCoordinate);
}
)";

struct alignas(16) FrameConstants {
    float horizontalOffset;
    float horizontalScale;
    float padding[2];
};

static_assert(sizeof(FrameConstants) == 16);

[[nodiscard]] HRESULT CompileShader(
    std::string_view source,
    const char* target,
    Microsoft::WRL::ComPtr<ID3DBlob>& bytecode,
    std::wstring& errorDetail) {
    Microsoft::WRL::ComPtr<ID3DBlob> compilerErrors;
    const HRESULT result = D3DCompile(
        source.data(),
        source.size(),
        nullptr,
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &bytecode,
        &compilerErrors);

    if (FAILED(result) && compilerErrors) {
        const auto* message = static_cast<const char*>(compilerErrors->GetBufferPointer());
        const std::size_t length = compilerErrors->GetBufferSize();
        errorDetail.assign(message, message + length);
    }

    return result;
}

}  // namespace

namespace panning_wallpaper {

HRESULT Renderer::Initialize(
    HWND window,
    const DecodedImage& image,
    std::wstring& errorDetail) {
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
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requestedFeatureLevels.data(),
        static_cast<UINT>(requestedFeatureLevels.size()),
        D3D11_SDK_VERSION,
        &device_,
        nullptr,
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

    result = CreateRenderTarget();
    if (FAILED(result)) {
        return result;
    }

    result = CreateImageTexture(image);
    if (FAILED(result)) {
        return result;
    }

    result = CreatePipeline(errorDetail);
    if (FAILED(result)) {
        return result;
    }

    UpdateViewport(width, height);
    return S_OK;
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

    const HRESULT createResult = CreateRenderTarget();
    if (FAILED(createResult)) {
        return createResult;
    }

    UpdateViewport(width, height);
    return S_OK;
}

HRESULT Renderer::RenderAndPresent(float horizontalOffset) {
    if (!renderTarget_ || !imageView_ || !frameConstants_ ||
        renderWidth_ == 0 || renderHeight_ == 0 ||
        imageWidth_ == 0 || imageHeight_ == 0) {
        return E_UNEXPECTED;
    }

    const double scaledImageWidth =
        static_cast<double>(imageWidth_) * renderHeight_ / imageHeight_;
    const FrameConstants constants{
        horizontalOffset,
        static_cast<float>(renderWidth_ / scaledImageWidth),
        {0.0F, 0.0F},
    };

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = deviceContext_->Map(
        frameConstants_.Get(),
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &mapped);
    if (FAILED(result)) {
        return result;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    deviceContext_->Unmap(frameConstants_.Get(), 0);

    ID3D11RenderTargetView* const targets[] = {renderTarget_.Get()};
    deviceContext_->OMSetRenderTargets(1, targets, nullptr);
    deviceContext_->Draw(3, 0);

    return swapChain_->Present(1, 0);
}

void Renderer::Shutdown() noexcept {
    if (deviceContext_) {
        deviceContext_->OMSetRenderTargets(0, nullptr, nullptr);
        deviceContext_->ClearState();
    }

    renderTarget_.Reset();
    frameConstants_.Reset();
    sampler_.Reset();
    pixelShader_.Reset();
    vertexShader_.Reset();
    imageView_.Reset();
    imageTexture_.Reset();
    swapChain_.Reset();
    deviceContext_.Reset();
    device_.Reset();
}

bool Renderer::IsInitialized() const noexcept {
    return swapChain_ != nullptr && renderTarget_ != nullptr &&
           imageView_ != nullptr && vertexShader_ != nullptr &&
           pixelShader_ != nullptr && frameConstants_ != nullptr;
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

HRESULT Renderer::CreateImageTexture(const DecodedImage& image) {
    if (image.width == 0 || image.height == 0 || image.rowPitch == 0 ||
        image.pixels.empty()) {
        return E_INVALIDARG;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = image.width;
    description.Height = image.height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = image.pixels.data();
    initialData.SysMemPitch = image.rowPitch;

    HRESULT result = device_->CreateTexture2D(
        &description, &initialData, &imageTexture_);
    if (FAILED(result)) {
        return result;
    }

    result = device_->CreateShaderResourceView(
        imageTexture_.Get(), nullptr, &imageView_);
    if (SUCCEEDED(result)) {
        imageWidth_ = image.width;
        imageHeight_ = image.height;
    }
    return result;
}

HRESULT Renderer::CreatePipeline(std::wstring& errorDetail) {
    Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
    HRESULT result = CompileShader(
        kVertexShaderSource, "vs_4_0", vertexBytecode, errorDetail);
    if (FAILED(result)) {
        return result;
    }

    result = device_->CreateVertexShader(
        vertexBytecode->GetBufferPointer(),
        vertexBytecode->GetBufferSize(),
        nullptr,
        &vertexShader_);
    if (FAILED(result)) {
        return result;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
    result = CompileShader(
        kPixelShaderSource, "ps_4_0", pixelBytecode, errorDetail);
    if (FAILED(result)) {
        return result;
    }

    result = device_->CreatePixelShader(
        pixelBytecode->GetBufferPointer(),
        pixelBytecode->GetBufferSize(),
        nullptr,
        &pixelShader_);
    if (FAILED(result)) {
        return result;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    result = device_->CreateSamplerState(&samplerDescription, &sampler_);
    if (FAILED(result)) {
        return result;
    }

    D3D11_BUFFER_DESC constantBufferDescription{};
    constantBufferDescription.ByteWidth = static_cast<UINT>(sizeof(FrameConstants));
    constantBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
    constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device_->CreateBuffer(
        &constantBufferDescription, nullptr, &frameConstants_);
    if (FAILED(result)) {
        return result;
    }

    deviceContext_->IASetInputLayout(nullptr);
    deviceContext_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    deviceContext_->VSSetShader(vertexShader_.Get(), nullptr, 0);
    deviceContext_->PSSetShader(pixelShader_.Get(), nullptr, 0);

    ID3D11Buffer* const constantBuffers[] = {frameConstants_.Get()};
    deviceContext_->PSSetConstantBuffers(0, 1, constantBuffers);
    ID3D11ShaderResourceView* const imageViews[] = {imageView_.Get()};
    deviceContext_->PSSetShaderResources(0, 1, imageViews);
    ID3D11SamplerState* const samplers[] = {sampler_.Get()};
    deviceContext_->PSSetSamplers(0, 1, samplers);
    return S_OK;
}

void Renderer::UpdateViewport(UINT width, UINT height) noexcept {
    renderWidth_ = width;
    renderHeight_ = height;

    const D3D11_VIEWPORT viewport{
        0.0F,
        0.0F,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0F,
        1.0F,
    };
    deviceContext_->RSSetViewports(1, &viewport);
}

}  // namespace panning_wallpaper
