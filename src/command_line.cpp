#include "command_line.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace panning_wallpaper {
namespace {

[[nodiscard]] bool ParseDirection(
    std::wstring_view value,
    PanDirection& direction) noexcept {
    if (value == L"left") {
        direction = PanDirection::Left;
    } else if (value == L"right") {
        direction = PanDirection::Right;
    } else if (value == L"up") {
        direction = PanDirection::Up;
    } else if (value == L"down") {
        direction = PanDirection::Down;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseDuration(
    const wchar_t* value,
    double& durationSeconds) noexcept {
    if (value == nullptr || *value == L'\0') {
        return false;
    }

    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(value, &end);
    if (end == value || *end != L'\0' || errno == ERANGE ||
        !std::isfinite(parsed) || parsed <= 0.0) {
        return false;
    }

    durationSeconds = parsed;
    return true;
}

[[nodiscard]] bool IsOption(std::wstring_view value) noexcept {
    return value.starts_with(L"--");
}

}  // namespace

bool ParseCommandLine(
    int argumentCount,
    wchar_t* const* arguments,
    CommandLineOptions& options,
    std::wstring& error) {
    options = {};
    error.clear();

    if (argumentCount < 2 || arguments == nullptr) {
        error = L"An image path is required.";
        return false;
    }

    options.imagePath = arguments[1];
    if (options.imagePath.empty() || IsOption(options.imagePath)) {
        error = L"The first argument must be an image path.";
        return false;
    }

    bool directionSeen = false;
    bool durationSeen = false;
    for (int index = 2; index < argumentCount; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"--direction") {
            if (directionSeen) {
                error = L"The --direction option may be specified only once.";
                return false;
            }
            if (index + 1 >= argumentCount || IsOption(arguments[index + 1])) {
                error = L"The --direction option requires a value.";
                return false;
            }
            if (!ParseDirection(arguments[++index], options.panning.direction)) {
                error = L"Invalid direction. Use left, right, up, or down.";
                return false;
            }
            directionSeen = true;
        } else if (argument == L"--duration") {
            if (durationSeen) {
                error = L"The --duration option may be specified only once.";
                return false;
            }
            if (index + 1 >= argumentCount || IsOption(arguments[index + 1])) {
                error = L"The --duration option requires a value in seconds.";
                return false;
            }
            if (!ParseDuration(arguments[++index], options.panning.loopDurationSeconds)) {
                error = L"Invalid duration. Use a finite number greater than zero.";
                return false;
            }
            durationSeen = true;
        } else if (IsOption(argument)) {
            error = L"Unknown option: " + std::wstring(argument);
            return false;
        } else {
            error = L"Unexpected positional argument: " + std::wstring(argument);
            return false;
        }
    }

    return true;
}

std::wstring CommandLineUsage() {
    return L"Usage: PanningWallpaper.exe <image-path> "
           L"[--direction <left|right|up|down>] [--duration <seconds>]";
}

}  // namespace panning_wallpaper
