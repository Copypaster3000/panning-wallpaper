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

[[nodiscard]] bool ParseFitMode(
    std::wstring_view value,
    FitMode& fitMode) noexcept {
    if (value == L"pan") {
        fitMode = FitMode::Pan;
    } else if (value == L"cover") {
        fitMode = FitMode::Cover;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseOnOff(
    std::wstring_view value,
    bool& enabled) noexcept {
    if (value == L"on") {
        enabled = true;
    } else if (value == L"off") {
        enabled = false;
    } else {
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseFiniteNumber(
    const wchar_t* value,
    double& number) noexcept {
    if (value == nullptr || *value == L'\0') {
        return false;
    }

    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(value, &end);
    if (end == value || *end != L'\0' || errno == ERANGE ||
        !std::isfinite(parsed)) {
        return false;
    }

    number = parsed;
    return true;
}

[[nodiscard]] bool ParseDuration(
    const wchar_t* value,
    double& durationSeconds) noexcept {
    double parsed = 0.0;
    if (!ParseFiniteNumber(value, parsed) || parsed <= 0.0) {
        return false;
    }

    durationSeconds = parsed;
    return true;
}

[[nodiscard]] bool ParsePosition(
    const wchar_t* value,
    double& position) noexcept {
    double parsed = 0.0;
    if (!ParseFiniteNumber(value, parsed) || parsed < 0.0 || parsed > 1.0) {
        return false;
    }

    position = parsed;
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
    bool fitSeen = false;
    bool positionSeen = false;
    bool pauseWhenCoveredSeen = false;
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
        } else if (argument == L"--fit") {
            if (fitSeen) {
                error = L"The --fit option may be specified only once.";
                return false;
            }
            if (index + 1 >= argumentCount || IsOption(arguments[index + 1])) {
                error = L"The --fit option requires a value.";
                return false;
            }
            if (!ParseFitMode(arguments[++index], options.panning.fitMode)) {
                error = L"Invalid fit mode. Use pan or cover.";
                return false;
            }
            fitSeen = true;
        } else if (argument == L"--position") {
            if (positionSeen) {
                error = L"The --position option may be specified only once.";
                return false;
            }
            if (index + 1 >= argumentCount || IsOption(arguments[index + 1])) {
                error = L"The --position option requires a value from 0 through 1.";
                return false;
            }
            if (!ParsePosition(arguments[++index], options.panning.position)) {
                error = L"Invalid position. Use a finite number from 0 through 1.";
                return false;
            }
            positionSeen = true;
        } else if (argument == L"--pause-when-covered") {
            if (pauseWhenCoveredSeen) {
                error = L"The --pause-when-covered option may be specified only once.";
                return false;
            }
            if (index + 1 >= argumentCount || IsOption(arguments[index + 1])) {
                error = L"The --pause-when-covered option requires on or off.";
                return false;
            }
            if (!ParseOnOff(
                    arguments[++index], options.panning.pauseWhenCovered)) {
                error = L"Invalid pause-when-covered value. Use on or off.";
                return false;
            }
            pauseWhenCoveredSeen = true;
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
           L"[--direction <left|right|up|down>] [--duration <seconds>] "
           L"[--fit <pan|cover>] [--position <0..1>] "
           L"[--pause-when-covered <on|off>]";
}

}  // namespace panning_wallpaper
