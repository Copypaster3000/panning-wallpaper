#include "window_coverage.h"

#include "coverage_geometry.h"

#include <dwmapi.h>

#include <array>
#include <format>
#include <string_view>
#include <vector>

namespace panning_wallpaper {
namespace {

// Out-of-context WinEvent callbacks are delivered on the thread that installed
// the hooks. The application installs one monitor on its message-loop thread,
// so the callback target and coalescing flag need no cross-thread machinery.
WindowCoverageMonitor* activeMonitor = nullptr;

[[nodiscard]] bool IsShellInfrastructure(HWND window) {
    std::array<wchar_t, 64> className{};
    if (GetClassNameW(window, className.data(), static_cast<int>(className.size())) == 0) {
        return true;
    }

    const std::wstring_view name(className.data());
    return name == L"Progman" || name == L"WorkerW" ||
           name == L"Shell_TrayWnd" || name == L"Shell_SecondaryTrayWnd";
}

[[nodiscard]] bool TryGetVisibleBounds(HWND window, CoverageRectangle& bounds) {
    RECT rectangle{};
    if (FAILED(DwmGetWindowAttribute(
            window,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &rectangle,
            sizeof(rectangle))) &&
        !GetWindowRect(window, &rectangle)) {
        return false;
    }

    if (rectangle.left >= rectangle.right || rectangle.top >= rectangle.bottom) {
        return false;
    }

    bounds = {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    return true;
}

struct EnumerationContext {
    HWND desktopHost = nullptr;
    DWORD processId = 0;
    bool desktopHostFound = false;
    std::vector<CoverageRectangle> occluders;
};

BOOL CALLBACK EnumerateOccludingWindows(HWND window, LPARAM parameter) {
    auto& context = *reinterpret_cast<EnumerationContext*>(parameter);
    if (window == context.desktopHost) {
        context.desktopHostFound = true;
        return FALSE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == context.processId || !IsWindowVisible(window) ||
        IsIconic(window) || IsShellInfrastructure(window)) {
        return TRUE;
    }

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(
            window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) {
        return TRUE;
    }

    CoverageRectangle bounds;
    if (TryGetVisibleBounds(window, bounds)) {
        context.occluders.push_back(bounds);
    }
    return TRUE;
}

}  // namespace

WindowCoverageMonitor::~WindowCoverageMonitor() {
    Shutdown();
}

bool WindowCoverageMonitor::Initialize(
    HWND notificationWindow,
    HWND desktopHost,
    UINT notificationMessage,
    std::wstring& diagnostic) {
    Shutdown();
    diagnostic.clear();

    if (activeMonitor != nullptr || notificationWindow == nullptr ||
        desktopHost == nullptr || notificationMessage < WM_APP) {
        diagnostic = L"Window coverage monitoring received invalid initialization state.";
        return false;
    }

    notificationWindow_ = notificationWindow;
    desktopHost_ = desktopHost;
    desktopEnumerationRoot_ = GetAncestor(desktopHost, GA_ROOT);
    if (desktopEnumerationRoot_ == nullptr) {
        desktopEnumerationRoot_ = desktopHost;
    }
    notificationMessage_ = notificationMessage;
    processId_ = GetCurrentProcessId();
    activeMonitor = this;

    constexpr DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    hooks_[0] = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        EventCallback,
        0,
        0,
        flags);
    hooks_[1] = SetWinEventHook(
        EVENT_SYSTEM_MINIMIZESTART,
        EVENT_SYSTEM_MINIMIZEEND,
        nullptr,
        EventCallback,
        0,
        0,
        flags);
    hooks_[2] = SetWinEventHook(
        EVENT_OBJECT_CREATE,
        EVENT_OBJECT_REORDER,
        nullptr,
        EventCallback,
        0,
        0,
        flags);
    hooks_[3] = SetWinEventHook(
        EVENT_OBJECT_LOCATIONCHANGE,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        EventCallback,
        0,
        0,
        flags);

    for (const HWINEVENTHOOK hook : hooks_) {
        if (hook == nullptr) {
            const DWORD error = GetLastError();
            Shutdown();
            diagnostic = std::format(
                L"SetWinEventHook failed with Win32 error {}; window-coverage pausing is disabled.",
                error);
            return false;
        }
    }

    return true;
}

void WindowCoverageMonitor::Shutdown() noexcept {
    if (activeMonitor == this) {
        activeMonitor = nullptr;
    }
    for (HWINEVENTHOOK& hook : hooks_) {
        if (hook != nullptr) {
            UnhookWinEvent(hook);
            hook = nullptr;
        }
    }
    notificationPending_ = false;
    notificationWindow_ = nullptr;
    desktopHost_ = nullptr;
    desktopEnumerationRoot_ = nullptr;
    notificationMessage_ = 0;
    processId_ = 0;
}

void WindowCoverageMonitor::AcknowledgeNotification() noexcept {
    notificationPending_ = false;
}

bool WindowCoverageMonitor::IsWallpaperFullyCovered() const {
    if (notificationWindow_ == nullptr || desktopHost_ == nullptr ||
        desktopEnumerationRoot_ == nullptr) {
        return false;
    }

    RECT wallpaperBounds{};
    if (!GetWindowRect(notificationWindow_, &wallpaperBounds) ||
        wallpaperBounds.left >= wallpaperBounds.right ||
        wallpaperBounds.top >= wallpaperBounds.bottom) {
        return false;
    }

    EnumerationContext context;
    // Raised-desktop Explorer builds nest the wallpaper WorkerW beneath
    // Progman, while classic builds expose it as a top-level WorkerW. The
    // top-level ancestor is therefore the common z-order boundary.
    context.desktopHost = desktopEnumerationRoot_;
    context.processId = processId_;
    EnumWindows(EnumerateOccludingWindows, reinterpret_cast<LPARAM>(&context));
    if (!context.desktopHostFound) {
        return false;
    }

    return IsFullyCovered(
        {wallpaperBounds.left,
         wallpaperBounds.top,
         wallpaperBounds.right,
         wallpaperBounds.bottom},
        context.occluders);
}

void CALLBACK WindowCoverageMonitor::EventCallback(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG objectId,
    LONG childId,
    DWORD,
    DWORD) {
    WindowCoverageMonitor* const monitor = activeMonitor;
    if (monitor == nullptr || window == nullptr) {
        return;
    }

    if (event >= EVENT_OBJECT_CREATE &&
        (objectId != OBJID_WINDOW || childId != CHILDID_SELF)) {
        return;
    }

    monitor->RequestEvaluation();
}

void WindowCoverageMonitor::RequestEvaluation() noexcept {
    if (notificationPending_ || notificationWindow_ == nullptr) {
        return;
    }

    notificationPending_ = true;
    if (!PostMessageW(notificationWindow_, notificationMessage_, 0, 0)) {
        notificationPending_ = false;
    }
}

}  // namespace panning_wallpaper
