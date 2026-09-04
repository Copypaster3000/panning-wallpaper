#pragma once

#include "image_decoder.h"
#include "panning.h"
#include "ui_palette.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

namespace panning_wallpaper {

class PreviewWindow final {
public:
    PreviewWindow() = default;
    ~PreviewWindow();

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    [[nodiscard]] bool Initialize(
        HINSTANCE instance,
        HWND parent,
        int controlId,
        std::wstring& error);
    void SetImage(DecodedImage image);
    void SetConfiguration(const PanningConfiguration& configuration) noexcept;
    void SetPalette(const UiPalette& palette) noexcept;
    [[nodiscard]] bool HasMeaningfulFraming() const noexcept;
    [[nodiscard]] HWND Window() const noexcept;

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM parameter,
        LPARAM secondaryParameter);
    [[nodiscard]] LRESULT HandleMessage(
        HWND window,
        UINT message,
        WPARAM parameter,
        LPARAM secondaryParameter);
    [[nodiscard]] HRESULT EnsureDeviceResources();
    [[nodiscard]] HRESULT CreatePaletteResources();
    [[nodiscard]] HRESULT CreateBitmapResources();
    void ReleaseDeviceResources() noexcept;
    void Paint();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    bool classRegistered_ = false;
    DecodedImage image_;
    PanningConfiguration configuration_;
    UiPalette palette_ = kLightPalette;
    Microsoft::WRL::ComPtr<ID2D1Factory> drawingFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> textFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> emptyTextFormat_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> bitmapBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backgroundBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> placeholderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
};

}  // namespace panning_wallpaper
