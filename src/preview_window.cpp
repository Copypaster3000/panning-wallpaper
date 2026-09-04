#include "preview_window.h"

#include <d2d1helper.h>

#include <format>

namespace panning_wallpaper {
namespace {

constexpr wchar_t kPreviewWindowClassName[] =
    L"PanningWallpaper.SettingsPreview";

[[nodiscard]] std::wstring FormatHresultError(
    std::wstring_view operation,
    HRESULT result) {
    return std::format(
        L"{} failed with HRESULT 0x{:08X}.",
        operation,
        static_cast<unsigned long>(result));
}

}  // namespace

PreviewWindow::~PreviewWindow() {
    if (window_ != nullptr && IsWindow(window_)) {
        DestroyWindow(window_);
    }
    ReleaseDeviceResources();
    emptyTextFormat_.Reset();
    textFactory_.Reset();
    drawingFactory_.Reset();
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kPreviewWindowClassName, instance_);
    }
}

bool PreviewWindow::Initialize(
    HINSTANCE instance,
    HWND parent,
    int controlId,
    std::wstring& error) {
    instance_ = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kPreviewWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        error = std::format(
            L"The preview window class could not be registered (Win32 error {}).",
            GetLastError());
        return false;
    }
    classRegistered_ = true;

    HRESULT result = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        drawingFactory_.GetAddressOf());
    if (FAILED(result)) {
        error = FormatHresultError(L"Preview drawing initialization", result);
        return false;
    }

    result = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(textFactory_.GetAddressOf()));
    if (FAILED(result)) {
        error = FormatHresultError(L"Preview text initialization", result);
        return false;
    }

    result = textFactory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0F,
        L"",
        &emptyTextFormat_);
    if (FAILED(result)) {
        error = FormatHresultError(L"Preview text-format creation", result);
        return false;
    }
    emptyTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    emptyTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    window_ = CreateWindowExW(
        0,
        kPreviewWindowClassName,
        L"Wallpaper preview",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        1,
        1,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        instance_,
        this);
    if (window_ == nullptr) {
        error = std::format(
            L"The preview control could not be created (Win32 error {}).",
            GetLastError());
        return false;
    }

    return true;
}

void PreviewWindow::SetImage(DecodedImage image) {
    image_ = std::move(image);
    bitmapBrush_.Reset();
    bitmap_.Reset();
    if (renderTarget_) {
        const HRESULT result = CreateBitmapResources();
        if (FAILED(result)) {
            ReleaseDeviceResources();
        }
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void PreviewWindow::SetConfiguration(
    const PanningConfiguration& configuration) noexcept {
    configuration_ = configuration;
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void PreviewWindow::SetPalette(const UiPalette& palette) noexcept {
    palette_ = palette;
    if (renderTarget_ && FAILED(CreatePaletteResources())) {
        ReleaseDeviceResources();
    }
    if (window_ != nullptr) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

bool PreviewWindow::HasMeaningfulFraming() const noexcept {
    if (window_ == nullptr) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(window_, &client)) {
        return false;
    }
    return HasFramingEffect(
        configuration_,
        static_cast<std::uint32_t>(client.right - client.left),
        static_cast<std::uint32_t>(client.bottom - client.top),
        image_.width,
        image_.height);
}

HWND PreviewWindow::Window() const noexcept {
    return window_;
}

LRESULT CALLBACK PreviewWindow::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    PreviewWindow* preview = reinterpret_cast<PreviewWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(
            secondaryParameter);
        preview = static_cast<PreviewWindow*>(create->lpCreateParams);
        preview->window_ = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(preview));
    }

    if (preview != nullptr) {
        return preview->HandleMessage(
            window, message, parameter, secondaryParameter);
    }
    return DefWindowProcW(window, message, parameter, secondaryParameter);
}

LRESULT PreviewWindow::HandleMessage(
    HWND window,
    UINT message,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_SIZE:
        if (renderTarget_ && parameter != SIZE_MINIMIZED) {
            const D2D1_SIZE_U size{
                static_cast<UINT>(LOWORD(secondaryParameter)),
                static_cast<UINT>(HIWORD(secondaryParameter)),
            };
            renderTarget_->Resize(size);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED:
        if (renderTarget_) {
            const float dpi = static_cast<float>(HIWORD(parameter));
            renderTarget_->SetDpi(dpi, dpi);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_NCDESTROY:
        ReleaseDeviceResources();
        window_ = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, parameter, secondaryParameter);
}

HRESULT PreviewWindow::EnsureDeviceResources() {
    if (renderTarget_) {
        return S_OK;
    }

    RECT client{};
    if (!GetClientRect(window_, &client)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const D2D1_SIZE_U size{
        static_cast<UINT>(client.right - client.left),
        static_cast<UINT>(client.bottom - client.top),
    };

    const float dpi = static_cast<float>(GetDpiForWindow(window_));
    HRESULT result = drawingFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_IGNORE),
            dpi,
            dpi),
        D2D1::HwndRenderTargetProperties(window_, size),
        &renderTarget_);
    if (FAILED(result)) {
        return result;
    }

    result = CreatePaletteResources();
    if (SUCCEEDED(result)) {
        result = CreateBitmapResources();
    }
    if (FAILED(result)) {
        ReleaseDeviceResources();
    }
    return result;
}

HRESULT PreviewWindow::CreatePaletteResources() {
    textBrush_.Reset();
    borderBrush_.Reset();
    placeholderBrush_.Reset();
    backgroundBrush_.Reset();
    if (!renderTarget_) {
        return S_OK;
    }

    HRESULT result = renderTarget_->CreateSolidColorBrush(
        D2D1::ColorF(palette_.previewBackground), &backgroundBrush_);
    if (SUCCEEDED(result)) {
        result = renderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(palette_.previewPlaceholder), &placeholderBrush_);
    }
    if (SUCCEEDED(result)) {
        result = renderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(palette_.border), &borderBrush_);
    }
    if (SUCCEEDED(result)) {
        result = renderTarget_->CreateSolidColorBrush(
            D2D1::ColorF(palette_.secondaryText), &textBrush_);
    }
    return result;
}

HRESULT PreviewWindow::CreateBitmapResources() {
    bitmapBrush_.Reset();
    bitmap_.Reset();
    if (!renderTarget_ || image_.pixels.empty()) {
        return S_OK;
    }

    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_IGNORE),
        96.0F,
        96.0F);
    HRESULT result = renderTarget_->CreateBitmap(
        D2D1::SizeU(image_.width, image_.height),
        image_.pixels.data(),
        image_.rowPitch,
        properties,
        &bitmap_);
    if (FAILED(result)) {
        return result;
    }

    return renderTarget_->CreateBitmapBrush(
        bitmap_.Get(),
        D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_CLAMP,
            D2D1_EXTEND_MODE_CLAMP,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR),
        &bitmapBrush_);
}

void PreviewWindow::ReleaseDeviceResources() noexcept {
    textBrush_.Reset();
    borderBrush_.Reset();
    placeholderBrush_.Reset();
    backgroundBrush_.Reset();
    bitmapBrush_.Reset();
    bitmap_.Reset();
    renderTarget_.Reset();
}

void PreviewWindow::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);

    const HRESULT resourceResult = EnsureDeviceResources();
    if (SUCCEEDED(resourceResult)) {
        const D2D1_SIZE_F size = renderTarget_->GetSize();
        renderTarget_->BeginDraw();
        renderTarget_->Clear(D2D1::ColorF(palette_.windowBackground));
        const D2D1_ROUNDED_RECT previewBounds = D2D1::RoundedRect(
            D2D1::RectF(0.5F, 0.5F, size.width - 0.5F, size.height - 0.5F),
            8.0F,
            8.0F);
        renderTarget_->FillRoundedRectangle(
            previewBounds, backgroundBrush_.Get());

        if (bitmapBrush_ && image_.width != 0 && image_.height != 0) {
            const PanTransform transform = CalculatePanTransform(
                configuration_,
                0.25,
                static_cast<std::uint32_t>(size.width),
                static_cast<std::uint32_t>(size.height),
                image_.width,
                image_.height);
            bitmapBrush_->SetExtendModeX(
                IsHorizontal(configuration_.direction)
                    ? D2D1_EXTEND_MODE_WRAP
                    : D2D1_EXTEND_MODE_CLAMP);
            bitmapBrush_->SetExtendModeY(
                IsHorizontal(configuration_.direction)
                    ? D2D1_EXTEND_MODE_CLAMP
                    : D2D1_EXTEND_MODE_WRAP);

            const float scaleX =
                size.width / (transform.scaleU * image_.width);
            const float scaleY =
                size.height / (transform.scaleV * image_.height);
            const float offsetX =
                -transform.offsetU * size.width / transform.scaleU;
            const float offsetY =
                -transform.offsetV * size.height / transform.scaleV;
            bitmapBrush_->SetTransform(D2D1::Matrix3x2F(
                scaleX, 0.0F, 0.0F, scaleY, offsetX, offsetY));
            renderTarget_->FillRoundedRectangle(
                previewBounds, bitmapBrush_.Get());
        } else {
            constexpr wchar_t message[] = L"Choose an image to preview";
            const float centerX = size.width / 2.0F;
            const float centerY = size.height / 2.0F;
            const D2D1_ROUNDED_RECT iconBounds = D2D1::RoundedRect(
                D2D1::RectF(
                    centerX - 14.0F,
                    centerY - 30.0F,
                    centerX + 14.0F,
                    centerY - 10.0F),
                2.0F,
                2.0F);
            renderTarget_->FillRoundedRectangle(
                iconBounds, placeholderBrush_.Get());
            renderTarget_->DrawRoundedRectangle(
                iconBounds, textBrush_.Get(), 1.0F);
            renderTarget_->DrawLine(
                D2D1::Point2F(centerX - 10.0F, centerY - 14.0F),
                D2D1::Point2F(centerX - 3.0F, centerY - 21.0F),
                textBrush_.Get(),
                1.0F);
            renderTarget_->DrawLine(
                D2D1::Point2F(centerX - 3.0F, centerY - 21.0F),
                D2D1::Point2F(centerX + 3.0F, centerY - 15.0F),
                textBrush_.Get(),
                1.0F);
            renderTarget_->DrawLine(
                D2D1::Point2F(centerX + 3.0F, centerY - 15.0F),
                D2D1::Point2F(centerX + 7.0F, centerY - 19.0F),
                textBrush_.Get(),
                1.0F);
            renderTarget_->DrawLine(
                D2D1::Point2F(centerX + 7.0F, centerY - 19.0F),
                D2D1::Point2F(centerX + 11.0F, centerY - 14.0F),
                textBrush_.Get(),
                1.0F);
            renderTarget_->DrawTextW(
                message,
                static_cast<UINT32>(std::size(message) - 1),
                emptyTextFormat_.Get(),
                D2D1::RectF(
                    0.0F, centerY - 2.0F, size.width, centerY + 30.0F),
                textBrush_.Get());
        }

        renderTarget_->DrawRoundedRectangle(
            previewBounds, borderBrush_.Get(), 1.0F);
        const HRESULT drawResult = renderTarget_->EndDraw();
        if (drawResult == D2DERR_RECREATE_TARGET) {
            ReleaseDeviceResources();
        }
    }

    EndPaint(window_, &paint);
}

}  // namespace panning_wallpaper
