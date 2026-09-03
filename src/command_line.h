#pragma once

#include "panning.h"

#include <string>

namespace panning_wallpaper {

enum class LaunchMode {
    Settings,
    DirectWallpaper,
};

struct CommandLineOptions {
    LaunchMode launchMode = LaunchMode::Settings;
    std::wstring imagePath;
    PanningConfiguration panning;
};

[[nodiscard]] bool ParseCommandLine(
    int argumentCount,
    wchar_t* const* arguments,
    CommandLineOptions& options,
    std::wstring& error);
[[nodiscard]] std::wstring CommandLineUsage();

}  // namespace panning_wallpaper
