#pragma once

#include "renderer.h"

#include <windows.h>

#include <chrono>
#include <string>
#include <string_view>

namespace panning_wallpaper {

class Application final {
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool Initialize(
        HINSTANCE instance,
        std::wstring_view imagePath,
        std::wstring& error);
    [[nodiscard]] int Run();
    [[nodiscard]] const std::wstring& RuntimeError() const noexcept;

private:
    static constexpr int kExitHotKeyId = 1;

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
    [[nodiscard]] HRESULT FitToDesktopHost();
    [[nodiscard]] HRESULT RenderCurrentFrame();
    [[nodiscard]] float CurrentHorizontalOffset() const noexcept;
    void CloseAfterFailure(std::wstring_view operation, HRESULT result);

    HINSTANCE instance_ = nullptr;
    HWND desktopHost_ = nullptr;
    HWND window_ = nullptr;
    Renderer renderer_;
    std::chrono::steady_clock::time_point animationStart_{};
    std::wstring runtimeError_;
};

}  // namespace panning_wallpaper
