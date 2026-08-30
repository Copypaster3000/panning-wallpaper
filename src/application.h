#pragma once

#include "renderer.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace panning_wallpaper {

class Application final {
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool Initialize(HINSTANCE instance, std::wstring& error);
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
    void CloseAfterFailure(std::wstring_view operation, HRESULT result);

    HINSTANCE instance_ = nullptr;
    HWND desktopHost_ = nullptr;
    HWND window_ = nullptr;
    Renderer renderer_;
    std::wstring runtimeError_;
};

}  // namespace panning_wallpaper
