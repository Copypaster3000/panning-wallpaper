#pragma once

#include "settings_model.h"
#include "ui_palette.h"

#include <windows.h>

namespace panning_wallpaper {

struct SavedSettings {
    std::optional<WallpaperSettings> applied;
    bool wallpaperEnabled = false;
    UiTheme theme = UiTheme::Light;
};

// The optional key path keeps registry tests separate from the user's settings.
class SettingsStore final {
public:
    explicit SettingsStore(
        std::wstring keyPath = L"Software\\PanningWallpaper");
    [[nodiscard]] SavedSettings Load(std::wstring& error) const;
    [[nodiscard]] bool SaveApplied(
        const WallpaperSettings& settings, std::wstring& error) const;
    [[nodiscard]] bool SaveEnabled(bool enabled, std::wstring& error) const;
    [[nodiscard]] bool SaveTheme(UiTheme theme, std::wstring& error) const;

private:
    std::wstring keyPath_;
};

}  // namespace panning_wallpaper
