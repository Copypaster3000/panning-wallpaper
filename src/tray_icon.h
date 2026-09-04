#pragma once

#include <windows.h>
#include <shellapi.h>

namespace panning_wallpaper {

class TrayIcon final {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 2;
    enum class Command : UINT { None, OpenSettings, ToggleWallpaper, Exit };

    TrayIcon() = default;
    ~TrayIcon();
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    [[nodiscard]] bool Initialize(HWND owner);
    [[nodiscard]] bool Restore();
    void Shutdown() noexcept;
    void NotifyHidden() noexcept;
    [[nodiscard]] Command ShowMenu(bool running, bool canStart, POINT anchor) const;
    [[nodiscard]] bool IsRegistered() const noexcept { return registered_; }

private:
    NOTIFYICONDATAW data_{};
    bool registered_ = false;
    bool notified_ = false;
};

}  // namespace panning_wallpaper
