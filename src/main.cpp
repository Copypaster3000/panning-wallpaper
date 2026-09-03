#include "application.h"
#include "command_line.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdlib>
#include <format>
#include <string>

#if defined(_MSC_VER)
// Opt in to the themed Windows common controls used by the settings window.
#pragma comment(                                                               \
    linker,                                                                    \
    "\"/manifestdependency:type='win32' "                                      \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' "             \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "            \
    "language='*'\"")
#endif

namespace {

class ComApartment final {
public:
    [[nodiscard]] HRESULT Initialize() noexcept {
        const HRESULT result = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
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

    panning_wallpaper::CommandLineOptions options;
    std::wstring commandLineError;
    if (!panning_wallpaper::ParseCommandLine(
            argumentCount, arguments, options, commandLineError)) {
        LocalFree(arguments);
        ShowFatalError(
            commandLineError + L"\n\n" +
            panning_wallpaper::CommandLineUsage());
        return EXIT_FAILURE;
    }
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
    const bool initialized =
        options.launchMode == panning_wallpaper::LaunchMode::Settings
        ? application.InitializeSettings(instance, error)
        : application.InitializeDirectWallpaper(
              instance, options.imagePath, options.panning, error);
    if (!initialized) {
        ShowFatalError(error);
        return EXIT_FAILURE;
    }

    const int exitCode = application.Run();
    if (!application.RuntimeError().empty()) {
        ShowFatalError(application.RuntimeError());
    }

    return exitCode;
}
