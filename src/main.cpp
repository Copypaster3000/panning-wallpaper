#include "application.h"

#include <windows.h>

#include <cstdlib>
#include <string>

namespace {

void ShowFatalError(const std::wstring& error) {
    const std::wstring diagnostic = L"Panning Wallpaper: " + error + L"\n";
    OutputDebugStringW(diagnostic.c_str());
    MessageBoxW(
        nullptr,
        error.c_str(),
        L"Panning Wallpaper",
        MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
        GetLastError() != ERROR_ACCESS_DENIED) {
        ShowFatalError(L"Per-monitor DPI awareness could not be enabled.");
        return EXIT_FAILURE;
    }

    panning_wallpaper::Application application;
    std::wstring error;
    if (!application.Initialize(instance, error)) {
        ShowFatalError(error);
        return EXIT_FAILURE;
    }

    const int exitCode = application.Run();
    if (!application.RuntimeError().empty()) {
        ShowFatalError(application.RuntimeError());
    }

    return exitCode;
}
