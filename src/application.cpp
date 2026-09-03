#include "application.h"

#include "desktop_host.h"
#include "settings_window.h"

#include <powrprof.h>
#include <wtsapi32.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <new>
#include <string_view>

namespace panning_wallpaper {
namespace {

constexpr wchar_t kWallpaperWindowClassName[] =
    L"PanningWallpaper.RenderSurface";
constexpr auto kFrameInterval = std::chrono::nanoseconds{1'000'000'000 / 30};

[[nodiscard]] std::wstring FormatSystemError(std::wstring_view operation) {
    return std::format(
        L"{} failed with Win32 error {}.", operation, GetLastError());
}

void OutputVisibilityDiagnostic(std::wstring_view diagnostic) {
    const std::wstring message =
        L"Panning Wallpaper: " + std::wstring(diagnostic) + L"\n";
    OutputDebugStringW(message.c_str());
}

[[nodiscard]] bool HasUsableImage(const DecodedImage& image) noexcept {
    return image.width != 0 && image.height != 0 && image.rowPitch != 0 &&
           !image.pixels.empty();
}

[[nodiscard]] bool ValidateImageSource(
    std::wstring_view imagePath,
    std::wstring& error) {
    const std::wstring filePath(imagePath);
    const HANDLE file = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"The selected image can no longer be opened. "
                L"Choose the image again or select another file.";
        return false;
    }

    BY_HANDLE_FILE_INFORMATION information{};
    const bool regularFile = GetFileType(file) == FILE_TYPE_DISK &&
        GetFileInformationByHandle(file, &information) &&
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    CloseHandle(file);
    if (!regularFile) {
        error = L"The selected image is no longer a readable file. "
                L"Choose the image again or select another file.";
        return false;
    }
    return true;
}

}  // namespace

Application::Application() = default;

Application::~Application() {
    shuttingDown_ = true;
    StopWallpaper();
    settingsWindow_.reset();

    if (wallpaperClassRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kWallpaperWindowClassName, instance_);
    }
}

bool Application::InitializeDirectWallpaper(
    HINSTANCE instance,
    std::wstring_view imagePath,
    const PanningConfiguration& configuration,
    std::wstring& error) {
    instance_ = instance;
    settingsMode_ = false;

    DecodedImage image;
    if (!DecodeImageFile(imagePath, image, error)) {
        return false;
    }
    return StartWallpaper(imagePath, image, configuration, error);
}

bool Application::InitializeSettings(
    HINSTANCE instance,
    std::wstring& error) {
    instance_ = instance;
    settingsMode_ = true;

    try {
        settingsWindow_ = std::make_unique<SettingsWindow>(*this);
    } catch (const std::bad_alloc&) {
        error = L"Memory allocation for the settings window failed.";
        return false;
    }
    if (!settingsWindow_->Initialize(instance_, error)) {
        settingsWindow_.reset();
        return false;
    }
    return true;
}

bool Application::ApplyWallpaper(
    std::wstring_view imagePath,
    const PanningConfiguration& configuration,
    const DecodedImage* decodedImage,
    std::wstring& error) {
    error.clear();
    if (imagePath.empty()) {
        error = L"Choose an image before applying the wallpaper.";
        return false;
    }
    if (!IsValidPanningConfiguration(configuration)) {
        error = L"The wallpaper settings are invalid.";
        return false;
    }
    if (!ValidateImageSource(imagePath, error)) {
        return false;
    }

    if (wallpaperRunning_ && imagePath == wallpaperImagePath_) {
        return UpdateWallpaperConfiguration(configuration, error);
    }

    DecodedImage decoded;
    const DecodedImage* image = decodedImage;
    if (image == nullptr || !HasUsableImage(*image)) {
        if (!DecodeImageFile(imagePath, decoded, error)) {
            return false;
        }
        image = &decoded;
    }

    StopWallpaper();
    return StartWallpaper(imagePath, *image, configuration, error);
}

void Application::StopWallpaper() {
    if (wallpaperWindow_ != nullptr && IsWindow(wallpaperWindow_)) {
        DestroyWindow(wallpaperWindow_);
        return;
    }

    ShutdownVisibilityNotifications();
    renderer_.Shutdown();
    wallpaperWindow_ = nullptr;
    desktopHost_ = nullptr;
    wallpaperRunning_ = false;
    wallpaperImagePath_.clear();
    pauseReasons_ = 0;
    frameScheduleResetRequested_ = false;
}

bool Application::IsWallpaperRunning() const noexcept {
    return wallpaperRunning_;
}

void Application::SettingsWindowClosed() {
    if (shuttingDown_) {
        return;
    }
    StopWallpaper();
    PostQuitMessage(0);
}

int Application::Run() {
    // A process-local high-resolution timer avoids quantizing 30 FPS deadlines
    // to the coarse system timer tick without changing global timer resolution.
    HANDLE frameTimer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (frameTimer == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) {
        frameTimer = CreateWaitableTimerExW(
            nullptr,
            nullptr,
            0,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
    if (frameTimer == nullptr) {
        runtimeError_ = FormatSystemError(L"CreateWaitableTimerExW");
        return 1;
    }

    auto nextFrame = std::chrono::steady_clock::now() + kFrameInterval;
    int exitCode = 0;
    bool running = true;
    bool timerArmed = false;

    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                exitCode = static_cast<int>(message.wParam);
                running = false;
                break;
            }

            if (settingsWindow_ && settingsWindow_->Window() != nullptr &&
                IsDialogMessageW(settingsWindow_->Window(), &message)) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }

        if (frameScheduleResetRequested_ && wallpaperRunning_ &&
            !IsRenderingPaused()) {
            nextFrame = std::chrono::steady_clock::now();
            frameScheduleResetRequested_ = false;
        }

        if (!wallpaperRunning_ || IsRenderingPaused()) {
            if (timerArmed) {
                if (!CancelWaitableTimer(frameTimer)) {
                    runtimeError_ = FormatSystemError(L"CancelWaitableTimer");
                    exitCode = 1;
                    break;
                }
                timerArmed = false;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (wallpaperRunning_ && runtimeError_.empty() &&
            !IsRenderingPaused() && now >= nextFrame) {
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

        DWORD handleCount = 0;
        const HANDLE handles[] = {frameTimer};
        if (wallpaperRunning_ && runtimeError_.empty() &&
            !IsRenderingPaused()) {
            using HundredNanoseconds =
                std::chrono::duration<LONGLONG, std::ratio<1, 10'000'000>>;
            const auto remaining = nextFrame - now;
            const auto rounded = std::chrono::ceil<HundredNanoseconds>(remaining);
            LARGE_INTEGER dueTime{};
            dueTime.QuadPart = -std::max<LONGLONG>(1, rounded.count());
            if (!SetWaitableTimer(
                    frameTimer, &dueTime, 0, nullptr, nullptr, FALSE)) {
                runtimeError_ = FormatSystemError(L"SetWaitableTimer");
                exitCode = 1;
                break;
            }
            handleCount = 1;
            timerArmed = true;
        }

        const DWORD waitResult = MsgWaitForMultipleObjectsEx(
            handleCount,
            handleCount != 0 ? handles : nullptr,
            INFINITE,
            QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_FAILED) {
            runtimeError_ = FormatSystemError(L"MsgWaitForMultipleObjectsEx");
            exitCode = 1;
            break;
        }
        if (handleCount != 0 && waitResult == WAIT_OBJECT_0) {
            timerArmed = false;
        }
    }

    CloseHandle(frameTimer);
    return exitCode;
}

const std::wstring& Application::RuntimeError() const noexcept {
    return runtimeError_;
}

LRESULT CALLBACK Application::WallpaperWindowProcedure(
    HWND window,
    UINT message,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    Application* application = reinterpret_cast<Application*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(
            secondaryParameter);
        application = static_cast<Application*>(create->lpCreateParams);
        application->wallpaperWindow_ = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    }

    if (application != nullptr) {
        return application->HandleWallpaperMessage(
            window, message, parameter, secondaryParameter);
    }
    return DefWindowProcW(window, message, parameter, secondaryParameter);
}

LRESULT Application::HandleWallpaperMessage(
    HWND window,
    UINT message,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    switch (message) {
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY: {
        const bool wasRunning = wallpaperRunning_;
        const std::wstring failure = runtimeError_;
        ShutdownVisibilityNotifications();
        UnregisterHotKey(window, kExitHotKeyId);
        renderer_.Shutdown();
        wallpaperWindow_ = nullptr;
        desktopHost_ = nullptr;
        wallpaperRunning_ = false;
        wallpaperImagePath_.clear();
        pauseReasons_ = 0;
        frameScheduleResetRequested_ = false;

        if (settingsMode_) {
            if (settingsWindow_) {
                settingsWindow_->SetWallpaperRunning(false);
                if (!failure.empty()) {
                    settingsWindow_->ShowWallpaperFailure(failure);
                }
            }
            runtimeError_.clear();
        } else if (wasRunning) {
            PostQuitMessage(failure.empty() ? 0 : 1);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_HOTKEY:
        if (parameter == kExitHotKeyId) {
            if (settingsMode_ && settingsWindow_ &&
                settingsWindow_->Window() != nullptr) {
                PostMessageW(settingsWindow_->Window(), WM_CLOSE, 0, 0);
            } else {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        break;

    case kCoverageChangedMessage:
        coverageMonitor_.AcknowledgeNotification();
        ReevaluateWindowCoverage();
        return 0;

    case WM_WTSSESSION_CHANGE:
        if (parameter == WTS_SESSION_LOCK) {
            SetPauseReason(PauseReason::SessionLocked, true);
        } else if (parameter == WTS_SESSION_UNLOCK) {
            ReevaluateWindowCoverage();
            SetPauseReason(PauseReason::SessionLocked, false);
        }
        return 0;

    case WM_POWERBROADCAST:
        if (parameter == PBT_POWERSETTINGCHANGE && secondaryParameter != 0) {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(
                secondaryParameter);
            if (IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) &&
                setting->DataLength >= sizeof(DWORD)) {
                DWORD displayState = 0;
                std::memcpy(&displayState, setting->Data, sizeof(displayState));
                if (displayState == 0) {
                    SetPauseReason(PauseReason::DisplayOff, true);
                } else {
                    // Both on (1) and dimmed-but-visible (2) permit rendering.
                    ReevaluateWindowCoverage();
                    SetPauseReason(PauseReason::DisplayOff, false);
                }
            }
        }
        return TRUE;

    case WM_DISPLAYCHANGE:
        if (renderer_.IsInitialized()) {
            HRESULT result = FitToDesktopHost();
            ReevaluateWindowCoverage();
            // A changed size synchronously produces WM_SIZE, which recreates and
            // presents the swap chain. If the size is unchanged, display-change
            // notification itself is the one legitimate redraw condition.
            if (result == S_FALSE && !IsRenderingPaused() &&
                !frameScheduleResetRequested_) {
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

        if (renderer_.IsInitialized() && !IsRenderingPaused()) {
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
                CloseAfterFailure(
                    L"Render-surface sizing",
                    HRESULT_FROM_WIN32(GetLastError()));
                return 0;
            }

            const UINT width = static_cast<UINT>(
                clientRectangle.right - clientRectangle.left);
            const UINT height = static_cast<UINT>(
                clientRectangle.bottom - clientRectangle.top);
            HRESULT result = renderer_.Resize(width, height);
            ReevaluateWindowCoverage();
            if (SUCCEEDED(result) && width != 0 && height != 0 &&
                !IsRenderingPaused() && !frameScheduleResetRequested_) {
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

bool Application::RegisterWallpaperWindowClass(std::wstring& error) {
    if (wallpaperClassRegistered_) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WallpaperWindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWallpaperWindowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        error = FormatSystemError(L"RegisterClassExW");
        return false;
    }
    wallpaperClassRegistered_ = true;
    return true;
}

bool Application::StartWallpaper(
    std::wstring_view imagePath,
    const DecodedImage& image,
    const PanningConfiguration& configuration,
    std::wstring& error) {
    if (!IsValidPanningConfiguration(configuration) ||
        !HasUsableImage(image)) {
        error = L"The wallpaper image or settings are invalid.";
        return false;
    }
    if (!RegisterWallpaperWindowClass(error)) {
        return false;
    }

    HWND desktopHost = nullptr;
    if (!DesktopHost::Discover(desktopHost, error)) {
        return false;
    }
    RECT hostClientRectangle{};
    if (!GetClientRect(desktopHost, &hostClientRectangle)) {
        error = FormatSystemError(L"GetClientRect");
        return false;
    }

    configuration_ = configuration;
    desktopHost_ = desktopHost;
    wallpaperWindow_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kWallpaperWindowClassName,
        L"Panning Wallpaper",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0,
        0,
        hostClientRectangle.right - hostClientRectangle.left,
        hostClientRectangle.bottom - hostClientRectangle.top,
        desktopHost_,
        nullptr,
        instance_,
        this);
    if (wallpaperWindow_ == nullptr) {
        error = FormatSystemError(L"CreateWindowExW");
        desktopHost_ = nullptr;
        return false;
    }

    std::wstring rendererError;
    HRESULT result = renderer_.Initialize(
        wallpaperWindow_, image, configuration_, rendererError);
    if (FAILED(result)) {
        error = rendererError.empty()
            ? std::format(
                L"The wallpaper renderer could not start (HRESULT 0x{:08X}).",
                static_cast<unsigned long>(result))
            : L"The wallpaper renderer could not compile its graphics pipeline.\n" +
                rendererError;
        DestroyWindow(wallpaperWindow_);
        return false;
    }

    if (!RegisterHotKey(
            wallpaperWindow_,
            kExitHotKeyId,
            MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT,
            'Q')) {
        error = FormatSystemError(L"RegisterHotKey");
        DestroyWindow(wallpaperWindow_);
        return false;
    }

    wallpaperImagePath_ = imagePath;
    runtimeError_.clear();
    InitializeVisibilityNotifications();
    ShowWindow(wallpaperWindow_, SW_SHOWNOACTIVATE);
    animationStart_ = std::chrono::steady_clock::now();
    wallpaperRunning_ = true;
    frameScheduleResetRequested_ = true;
    ReevaluateWindowCoverage();
    if (!IsRenderingPaused()) {
        result = RenderCurrentFrame();
        if (FAILED(result)) {
            error = std::format(
                L"The initial wallpaper frame could not be displayed "
                L"(HRESULT 0x{:08X}).",
                static_cast<unsigned long>(result));
            wallpaperRunning_ = false;
            DestroyWindow(wallpaperWindow_);
            return false;
        }
    }
    return true;
}

bool Application::UpdateWallpaperConfiguration(
    const PanningConfiguration& configuration,
    std::wstring& error) {
    const HRESULT result = renderer_.UpdateConfiguration(configuration);
    if (FAILED(result)) {
        error = std::format(
            L"The wallpaper settings could not be applied "
            L"(HRESULT 0x{:08X}).",
            static_cast<unsigned long>(result));
        return false;
    }

    configuration_ = configuration;
    animationStart_ = std::chrono::steady_clock::now();
    frameScheduleResetRequested_ = true;
    ReconfigureCoverageMonitoring();
    return true;
}

HRESULT Application::FitToDesktopHost() {
    if (!IsWindow(desktopHost_)) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }

    RECT hostClientRectangle{};
    if (!GetClientRect(desktopHost_, &hostClientRectangle)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(
            error != ERROR_SUCCESS ? error : ERROR_INVALID_WINDOW_HANDLE);
    }
    const int width = hostClientRectangle.right - hostClientRectangle.left;
    const int height = hostClientRectangle.bottom - hostClientRectangle.top;
    if (width <= 0 || height <= 0) {
        return E_UNEXPECTED;
    }

    RECT renderClientRectangle{};
    if (!GetClientRect(wallpaperWindow_, &renderClientRectangle)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(
            error != ERROR_SUCCESS ? error : ERROR_INVALID_WINDOW_HANDLE);
    }
    if (renderClientRectangle.right - renderClientRectangle.left == width &&
        renderClientRectangle.bottom - renderClientRectangle.top == height) {
        return S_FALSE;
    }

    if (!SetWindowPos(
            wallpaperWindow_,
            nullptr,
            0,
            0,
            width,
            height,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(
            error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
    }
    return S_OK;
}

HRESULT Application::RenderCurrentFrame() {
    return renderer_.RenderAndPresent(CurrentPanProgress());
}

double Application::CurrentPanProgress() const noexcept {
    const double elapsedSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - animationStart_).count();
    return CalculateLoopProgress(
        elapsedSeconds, configuration_.loopDurationSeconds);
}

bool Application::IsRenderingPaused() const noexcept {
    return pauseReasons_ != 0;
}

void Application::SetPauseReason(PauseReason reason, bool active) noexcept {
    const bool wasPaused = IsRenderingPaused();
    const unsigned int mask = static_cast<unsigned int>(reason);
    if (active) {
        pauseReasons_ |= mask;
    } else {
        pauseReasons_ &= ~mask;
    }
    if (wasPaused && !IsRenderingPaused()) {
        frameScheduleResetRequested_ = true;
    }
}

void Application::ReevaluateWindowCoverage() {
    const bool covered = configuration_.pauseWhenCovered &&
        coverageMonitoringActive_ &&
        coverageMonitor_.IsWallpaperFullyCovered();
    SetPauseReason(PauseReason::CoveredByWindows, covered);
}

void Application::InitializeVisibilityNotifications() {
    if (WTSRegisterSessionNotification(
            wallpaperWindow_, NOTIFY_FOR_THIS_SESSION)) {
        sessionNotificationsRegistered_ = true;
    } else {
        OutputVisibilityDiagnostic(std::format(
            L"WTSRegisterSessionNotification failed with Win32 error {}; "
            L"session-lock pausing is unavailable.",
            GetLastError()));
    }

    displayPowerNotification_ = RegisterPowerSettingNotification(
        wallpaperWindow_,
        &GUID_CONSOLE_DISPLAY_STATE,
        DEVICE_NOTIFY_WINDOW_HANDLE);
    if (displayPowerNotification_ == nullptr) {
        OutputVisibilityDiagnostic(std::format(
            L"RegisterPowerSettingNotification failed with Win32 error {}; "
            L"display-off pausing is unavailable.",
            GetLastError()));
    }
    ReconfigureCoverageMonitoring();
}

void Application::ReconfigureCoverageMonitoring() {
    coverageMonitoringActive_ = false;
    coverageMonitor_.Shutdown();
    SetPauseReason(PauseReason::CoveredByWindows, false);

    if (configuration_.pauseWhenCovered && wallpaperWindow_ != nullptr) {
        std::wstring diagnostic;
        coverageMonitoringActive_ = coverageMonitor_.Initialize(
            wallpaperWindow_,
            desktopHost_,
            kCoverageChangedMessage,
            diagnostic);
        if (!coverageMonitoringActive_) {
            OutputVisibilityDiagnostic(diagnostic);
        }
    }
    ReevaluateWindowCoverage();
}

void Application::ShutdownVisibilityNotifications() noexcept {
    coverageMonitoringActive_ = false;
    coverageMonitor_.Shutdown();

    if (sessionNotificationsRegistered_ && wallpaperWindow_ != nullptr) {
        WTSUnRegisterSessionNotification(wallpaperWindow_);
        sessionNotificationsRegistered_ = false;
    }
    if (displayPowerNotification_ != nullptr) {
        UnregisterPowerSettingNotification(displayPowerNotification_);
        displayPowerNotification_ = nullptr;
    }
}

void Application::CloseAfterFailure(
    std::wstring_view operation,
    HRESULT result) {
    if (runtimeError_.empty()) {
        runtimeError_ = std::format(
            L"{} failed with HRESULT 0x{:08X}.",
            operation,
            static_cast<unsigned long>(result));
        OutputDebugStringW((runtimeError_ + L"\n").c_str());
    }
    PostMessageW(wallpaperWindow_, WM_CLOSE, 0, 0);
}

}  // namespace panning_wallpaper
