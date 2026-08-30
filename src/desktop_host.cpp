#include "desktop_host.h"

namespace panning_wallpaper {
namespace {

constexpr UINT kCreateWorkerWindowMessage = 0x052C;
constexpr DWORD kShellMessageTimeoutMilliseconds = 1'000;

struct WorkerWindowSearch {
    HWND workerWindow = nullptr;
};

BOOL CALLBACK FindWorkerWindowAfterShellView(HWND topLevelWindow, LPARAM parameter) {
    auto* search = reinterpret_cast<WorkerWindowSearch*>(parameter);

    const HWND shellView =
        FindWindowExW(topLevelWindow, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shellView == nullptr) {
        return TRUE;
    }

    // In the classic Explorer layout, the WorkerW immediately following the
    // top-level shell-view host occupies the wallpaper layer below the icons.
    search->workerWindow =
        FindWindowExW(nullptr, topLevelWindow, L"WorkerW", nullptr);
    return search->workerWindow == nullptr ? TRUE : FALSE;
}

[[nodiscard]] bool RequestWorkerWindow(
    HWND programManager,
    WPARAM parameter,
    LPARAM secondaryParameter) {
    DWORD_PTR messageResult = 0;
    return SendMessageTimeoutW(
               programManager,
               kCreateWorkerWindowMessage,
               parameter,
               secondaryParameter,
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               kShellMessageTimeoutMilliseconds,
               &messageResult) != 0;
}

}  // namespace

bool DesktopHost::Discover(HWND& hostWindow, std::wstring& error) {
    hostWindow = nullptr;

    const HWND programManager = FindWindowW(L"Progman", nullptr);
    if (programManager == nullptr) {
        error = L"Explorer's Program Manager window was not found.";
        return false;
    }

    const LONG_PTR extendedStyle = GetWindowLongPtrW(programManager, GWL_EXSTYLE);
    const bool usesRaisedDesktop =
        (extendedStyle & WS_EX_NOREDIRECTIONBITMAP) != 0;

    // 0x052C is an undocumented Explorer message used by established desktop
    // wallpaper applications. Newer raised-desktop builds use 0xD/1, while the
    // classic shell layout responds to 0/0. Neither contract is guaranteed by
    // Win32, so failure is explicit instead of assuming a stable shell topology.
    if (usesRaisedDesktop) {
        if (!RequestWorkerWindow(programManager, 0xD, 1)) {
            error = L"Explorer did not respond while creating its desktop WorkerW window.";
            return false;
        }
        hostWindow = FindRaisedDesktopWorkerWindow(programManager);
    } else {
        if (!RequestWorkerWindow(programManager, 0, 0)) {
            error = L"Explorer did not respond while creating its desktop WorkerW window.";
            return false;
        }
        hostWindow = FindClassicWorkerWindow();
    }

    // Some Explorer revisions retain the alternate topology after an update or
    // shell transition. Searching the other known layout is a bounded fallback.
    if (hostWindow == nullptr) {
        hostWindow = usesRaisedDesktop ? FindClassicWorkerWindow()
                                       : FindRaisedDesktopWorkerWindow(programManager);
    }

    if (hostWindow == nullptr || !IsWindow(hostWindow)) {
        error = L"Explorer's wallpaper WorkerW window could not be located.";
        return false;
    }

    RECT clientRectangle{};
    if (!GetClientRect(hostWindow, &clientRectangle) ||
        clientRectangle.right <= clientRectangle.left ||
        clientRectangle.bottom <= clientRectangle.top) {
        error = L"Explorer's wallpaper WorkerW window has no usable client area.";
        hostWindow = nullptr;
        return false;
    }

    return true;
}

HWND DesktopHost::FindClassicWorkerWindow() {
    WorkerWindowSearch search{};
    EnumWindows(FindWorkerWindowAfterShellView, reinterpret_cast<LPARAM>(&search));
    return search.workerWindow;
}

HWND DesktopHost::FindRaisedDesktopWorkerWindow(HWND programManager) {
    HWND workerWindow = nullptr;
    while ((workerWindow = FindWindowExW(
                programManager, workerWindow, L"WorkerW", nullptr)) != nullptr) {
        if (FindWindowExW(
                workerWindow, nullptr, L"SHELLDLL_DefView", nullptr) == nullptr) {
            return workerWindow;
        }
    }

    return nullptr;
}

}  // namespace panning_wallpaper
