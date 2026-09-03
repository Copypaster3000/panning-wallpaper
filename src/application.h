#pragma once

#include "image_decoder.h"
#include "panning.h"
#include "renderer.h"
#include "window_coverage.h"

#include <windows.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace panning_wallpaper {

class SettingsWindow;

class Application final {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool InitializeDirectWallpaper(
        HINSTANCE instance,
        std::wstring_view imagePath,
        const PanningConfiguration& configuration,
        std::wstring& error);
    [[nodiscard]] bool InitializeSettings(
        HINSTANCE instance,
        std::wstring& error);
    [[nodiscard]] bool ApplyWallpaper(
        std::wstring_view imagePath,
        const PanningConfiguration& configuration,
        const DecodedImage* decodedImage,
        std::wstring& error);
    void StopWallpaper();
    [[nodiscard]] bool IsWallpaperRunning() const noexcept;
    void SettingsWindowClosed();

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

    static LRESULT CALLBACK WallpaperWindowProcedure(
        HWND window,
        UINT message,
        WPARAM parameter,
        LPARAM secondaryParameter);

    [[nodiscard]] LRESULT HandleWallpaperMessage(
        HWND window,
        UINT message,
        WPARAM parameter,
        LPARAM secondaryParameter);
    [[nodiscard]] bool RegisterWallpaperWindowClass(std::wstring& error);
    [[nodiscard]] bool StartWallpaper(
        std::wstring_view imagePath,
        const DecodedImage& image,
        const PanningConfiguration& configuration,
        std::wstring& error);
    [[nodiscard]] bool UpdateWallpaperConfiguration(
        const PanningConfiguration& configuration,
        std::wstring& error);
    [[nodiscard]] HRESULT FitToDesktopHost();
    [[nodiscard]] HRESULT RenderCurrentFrame();
    [[nodiscard]] double CurrentPanProgress() const noexcept;
    [[nodiscard]] bool IsRenderingPaused() const noexcept;
    void SetPauseReason(PauseReason reason, bool active) noexcept;
    void ReevaluateWindowCoverage();
    void InitializeVisibilityNotifications();
    void ReconfigureCoverageMonitoring();
    void ShutdownVisibilityNotifications() noexcept;
    void CloseAfterFailure(std::wstring_view operation, HRESULT result);

    HINSTANCE instance_ = nullptr;
    HWND desktopHost_ = nullptr;
    HWND wallpaperWindow_ = nullptr;
    Renderer renderer_;
    WindowCoverageMonitor coverageMonitor_;
    PanningConfiguration configuration_;
    std::chrono::steady_clock::time_point animationStart_{};
    std::wstring wallpaperImagePath_;
    std::wstring runtimeError_;
    std::unique_ptr<SettingsWindow> settingsWindow_;
    HPOWERNOTIFY displayPowerNotification_ = nullptr;
    unsigned int pauseReasons_ = 0;
    bool wallpaperClassRegistered_ = false;
    bool wallpaperRunning_ = false;
    bool settingsMode_ = false;
    bool shuttingDown_ = false;
    bool sessionNotificationsRegistered_ = false;
    bool coverageMonitoringActive_ = false;
    bool frameScheduleResetRequested_ = false;
};

}  // namespace panning_wallpaper
