#pragma once

#include <windows.h>

#include <array>
#include <string>

namespace panning_wallpaper {

class WindowCoverageMonitor final {
public:
    WindowCoverageMonitor() = default;
    ~WindowCoverageMonitor();

    WindowCoverageMonitor(const WindowCoverageMonitor&) = delete;
    WindowCoverageMonitor& operator=(const WindowCoverageMonitor&) = delete;

    [[nodiscard]] bool Initialize(
        HWND notificationWindow,
        HWND desktopHost,
        UINT notificationMessage,
        std::wstring& diagnostic);
    void Shutdown() noexcept;

    void AcknowledgeNotification() noexcept;
    [[nodiscard]] bool IsWallpaperFullyCovered() const;

private:
    static void CALLBACK EventCallback(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND window,
        LONG objectId,
        LONG childId,
        DWORD eventThread,
        DWORD eventTime);
    void RequestEvaluation() noexcept;

    HWND notificationWindow_ = nullptr;
    HWND desktopHost_ = nullptr;
    HWND desktopEnumerationRoot_ = nullptr;
    UINT notificationMessage_ = 0;
    DWORD processId_ = 0;
    bool notificationPending_ = false;
    std::array<HWINEVENTHOOK, 4> hooks_{};
};

}  // namespace panning_wallpaper
