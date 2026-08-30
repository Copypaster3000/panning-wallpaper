#include "application.h"

#include "desktop_host.h"
#include "image_decoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>

namespace panning_wallpaper {
namespace {

constexpr wchar_t kWindowClassName[] = L"PanningWallpaper.RenderSurface";
constexpr auto kFrameInterval = std::chrono::nanoseconds{1'000'000'000 / 30};
constexpr double kLoopDurationSeconds = 90.0;

[[nodiscard]] std::wstring FormatSystemError(std::wstring_view operation) {
    return std::format(
        L"{} failed with Win32 error {}.", operation, GetLastError());
}

}  // namespace

Application::~Application() {
    if (window_ != nullptr && IsWindow(window_)) {
        DestroyWindow(window_);
    }
    renderer_.Shutdown();

    if (instance_ != nullptr) {
        UnregisterClassW(kWindowClassName, instance_);
    }
}

bool Application::Initialize(
    HINSTANCE instance,
    std::wstring_view imagePath,
    std::wstring& error) {
    instance_ = instance;

    DecodedImage image;
    if (!DecodeImageFile(imagePath, image, error)) {
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&windowClass) == 0) {
        error = FormatSystemError(L"RegisterClassExW");
        return false;
    }

    if (!DesktopHost::Discover(desktopHost_, error)) {
        return false;
    }

    RECT hostClientRectangle{};
    if (!GetClientRect(desktopHost_, &hostClientRectangle)) {
        error = FormatSystemError(L"GetClientRect");
        return false;
    }

    const int width = hostClientRectangle.right - hostClientRectangle.left;
    const int height = hostClientRectangle.bottom - hostClientRectangle.top;
    window_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"Panning Wallpaper",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0,
        0,
        width,
        height,
        desktopHost_,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        error = FormatSystemError(L"CreateWindowExW");
        return false;
    }

    std::wstring rendererError;
    HRESULT result = renderer_.Initialize(window_, image, rendererError);
    if (FAILED(result)) {
        if (rendererError.empty()) {
            error = std::format(
                L"Direct3D image renderer initialization failed with HRESULT 0x{:08X}.",
                static_cast<unsigned long>(result));
        } else {
            error = std::format(
                L"Shader compilation failed with HRESULT 0x{:08X}:\n{}",
                static_cast<unsigned long>(result),
                rendererError);
        }
        return false;
    }
    // The immutable GPU texture now owns the image data needed at runtime.
    image = {};

    if (!RegisterHotKey(
            window_,
            kExitHotKeyId,
            MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
            'Q')) {
        error = FormatSystemError(L"RegisterHotKey");
        return false;
    }

    ShowWindow(window_, SW_SHOWNOACTIVATE);
    animationStart_ = std::chrono::steady_clock::now();
    result = RenderCurrentFrame();
    if (FAILED(result)) {
        error = std::format(
            L"The initial Direct3D frame could not be presented (HRESULT 0x{:08X}).",
            static_cast<unsigned long>(result));
        return false;
    }

    return true;
}

int Application::Run() {
    auto nextFrame = std::chrono::steady_clock::now() + kFrameInterval;
    int exitCode = 0;
    bool running = true;

    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                exitCode = static_cast<int>(message.wParam);
                running = false;
                break;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (runtimeError_.empty() && now >= nextFrame) {
            const HRESULT result = RenderCurrentFrame();
            if (FAILED(result)) {
                CloseAfterFailure(L"Animation frame presentation", result);
            }

            const auto afterRender = std::chrono::steady_clock::now();
            const auto missedIntervals =
                (afterRender - nextFrame) / kFrameInterval + 1;
            nextFrame += kFrameInterval * missedIntervals;
            continue;
        }

        DWORD timeoutMilliseconds = INFINITE;
        if (runtimeError_.empty()) {
            const auto remaining = nextFrame - now;
            const auto rounded = std::chrono::ceil<std::chrono::milliseconds>(remaining);
            timeoutMilliseconds = static_cast<DWORD>(
                std::max<std::int64_t>(1, rounded.count()));
        }

        const DWORD waitResult = MsgWaitForMultipleObjectsEx(
            0,
            nullptr,
            timeoutMilliseconds,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_FAILED) {
            runtimeError_ = FormatSystemError(L"MsgWaitForMultipleObjectsEx");
            return 1;
        }
    }

    return exitCode;
}

const std::wstring& Application::RuntimeError() const noexcept {
    return runtimeError_;
}

LRESULT CALLBACK Application::WindowProcedure(
    HWND window,
    UINT message,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    Application* application = reinterpret_cast<Application*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(secondaryParameter);
        application = static_cast<Application*>(create->lpCreateParams);
        application->window_ = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }

    if (application != nullptr) {
        return application->HandleMessage(
            window, message, parameter, secondaryParameter);
    }

    return DefWindowProcW(window, message, parameter, secondaryParameter);
}

LRESULT Application::HandleMessage(
    HWND window,
    UINT message,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    switch (message) {
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(window, kExitHotKeyId);
        renderer_.Shutdown();
        window_ = nullptr;
        PostQuitMessage(runtimeError_.empty() ? 0 : 1);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_HOTKEY:
        if (parameter == kExitHotKeyId) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_DISPLAYCHANGE:
        if (renderer_.IsInitialized()) {
            HRESULT result = FitToDesktopHost();
            // A changed size synchronously produces WM_SIZE, which recreates and
            // presents the swap chain. If the size is unchanged, display-change
            // notification itself is the one legitimate redraw condition.
            if (result == S_FALSE) {
                result = RenderCurrentFrame();
            }
            if (FAILED(result)) {
                CloseAfterFailure(L"Desktop display change", result);
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);

        if (renderer_.IsInitialized()) {
            const HRESULT result = RenderCurrentFrame();
            if (FAILED(result)) {
                CloseAfterFailure(L"Paint presentation", result);
            }
        }
        return 0;
    }

    case WM_SIZE:
        if (renderer_.IsInitialized() && parameter != SIZE_MINIMIZED) {
            RECT clientRectangle{};
            if (!GetClientRect(window, &clientRectangle)) {
                CloseAfterFailure(L"Render-surface sizing", HRESULT_FROM_WIN32(GetLastError()));
                return 0;
            }

            const UINT width =
                static_cast<UINT>(clientRectangle.right - clientRectangle.left);
            const UINT height =
                static_cast<UINT>(clientRectangle.bottom - clientRectangle.top);
            HRESULT result = renderer_.Resize(width, height);
            if (SUCCEEDED(result) && width != 0 && height != 0) {
                result = RenderCurrentFrame();
            }
            if (FAILED(result)) {
                CloseAfterFailure(L"Swap-chain resize", result);
            }
        }
        return 0;
    }

    return DefWindowProcW(window, message, parameter, secondaryParameter);
}

HRESULT Application::FitToDesktopHost() {
    if (!IsWindow(desktopHost_)) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }

    RECT hostClientRectangle{};
    if (!GetClientRect(desktopHost_, &hostClientRectangle)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_INVALID_WINDOW_HANDLE);
    }

    const int width = hostClientRectangle.right - hostClientRectangle.left;
    const int height = hostClientRectangle.bottom - hostClientRectangle.top;
    if (width <= 0 || height <= 0) {
        return E_UNEXPECTED;
    }

    RECT renderClientRectangle{};
    if (!GetClientRect(window_, &renderClientRectangle)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_INVALID_WINDOW_HANDLE);
    }

    if (renderClientRectangle.right - renderClientRectangle.left == width &&
        renderClientRectangle.bottom - renderClientRectangle.top == height) {
        return S_FALSE;
    }

    if (!SetWindowPos(
            window_,
            nullptr,
            0,
            0,
            width,
            height,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
    }

    return S_OK;
}

HRESULT Application::RenderCurrentFrame() {
    return renderer_.RenderAndPresent(CurrentHorizontalOffset());
}

float Application::CurrentHorizontalOffset() const noexcept {
    const double elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - animationStart_).count();
    const double completedLoops = elapsedSeconds / kLoopDurationSeconds;
    return static_cast<float>(completedLoops - std::floor(completedLoops));
}

void Application::CloseAfterFailure(std::wstring_view operation, HRESULT result) {
    if (runtimeError_.empty()) {
        runtimeError_ = std::format(
            L"{} failed with HRESULT 0x{:08X}.",
            operation,
            static_cast<unsigned long>(result));
        OutputDebugStringW((runtimeError_ + L"\n").c_str());
    }

    PostMessageW(window_, WM_CLOSE, 0, 0);
}

}  // namespace panning_wallpaper
