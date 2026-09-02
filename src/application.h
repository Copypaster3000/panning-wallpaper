#pragma once

#include "panning.h"
#include "renderer.h"
#include "window_coverage.h"

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
        const PanningConfiguration& configuration,
        std::wstring& error);
    [[nodiscard]] int Run();
    [[nodiscard]] const std::wstring& RuntimeError() const noexcept;

private:
    static constexpr int kExitHotKeyId = 1;
    static constexpr UINT kCoverageChangedMessage = WM_APP + 1;

    enum class PauseReason : unsigned int {
        CoveredByWindows = 1U << 0,
        SessionLocked = 1U << 1,
        DisplayOff = 1U << 2,
    };

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
    [[nodiscard]] double CurrentPanProgress() const noexcept;
    [[nodiscard]] bool IsRenderingPaused() const noexcept;
    void SetPauseReason(PauseReason reason, bool active) noexcept;
    void ReevaluateWindowCoverage();
    void InitializeVisibilityNotifications();
    void ShutdownVisibilityNotifications() noexcept;
    void CloseAfterFailure(std::wstring_view operation, HRESULT result);

    HINSTANCE instance_ = nullptr;
    HWND desktopHost_ = nullptr;
    HWND window_ = nullptr;
    Renderer renderer_;
    WindowCoverageMonitor coverageMonitor_;
    PanningConfiguration configuration_;
    std::chrono::steady_clock::time_point animationStart_{};
    std::wstring runtimeError_;
    HPOWERNOTIFY displayPowerNotification_ = nullptr;
    unsigned int pauseReasons_ = 0;
    bool sessionNotificationsRegistered_ = false;
    bool coverageMonitoringActive_ = false;
    bool frameScheduleResetRequested_ = false;
};

}  // namespace panning_wallpaper
