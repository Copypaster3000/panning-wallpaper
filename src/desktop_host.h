#pragma once

#include <windows.h>

#include <string>

namespace panning_wallpaper {

class DesktopHost final {
public:
    [[nodiscard]] static bool Discover(HWND& hostWindow, std::wstring& error);

private:
    [[nodiscard]] static HWND FindClassicWorkerWindow();
    [[nodiscard]] static HWND FindRaisedDesktopWorkerWindow(HWND programManager);
};

}  // namespace panning_wallpaper
