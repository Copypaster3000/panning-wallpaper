#include "application.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdlib>
#include <format>
#include <string>

namespace {

class ComApartment final {
public:
    [[nodiscard]] HRESULT Initialize() noexcept {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = SUCCEEDED(result);
        return result;
    }

    ~ComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

private:
    bool initialized_ = false;
};

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
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        ShowFatalError(L"The command line could not be parsed.");
        return EXIT_FAILURE;
    }

    if (argumentCount != 2) {
        LocalFree(arguments);
        ShowFatalError(
            L"Provide exactly one image path.\n\n"
            L"Usage: PanningWallpaper.exe \"C:\\path\\to\\wallpaper.png\"");
        return EXIT_FAILURE;
    }

    const std::wstring imagePath(arguments[1]);
    LocalFree(arguments);

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
        GetLastError() != ERROR_ACCESS_DENIED) {
        ShowFatalError(L"Per-monitor DPI awareness could not be enabled.");
        return EXIT_FAILURE;
    }

    ComApartment comApartment;
    const HRESULT comResult = comApartment.Initialize();
    if (FAILED(comResult)) {
        ShowFatalError(std::format(
            L"COM initialization failed with HRESULT 0x{:08X}.",
            static_cast<unsigned long>(comResult)));
        return EXIT_FAILURE;
    }

    panning_wallpaper::Application application;
    std::wstring error;
    if (!application.Initialize(instance, imagePath, error)) {
        ShowFatalError(error);
        return EXIT_FAILURE;
    }

    const int exitCode = application.Run();
    if (!application.RuntimeError().empty()) {
        ShowFatalError(application.RuntimeError());
    }

    return exitCode;
}
