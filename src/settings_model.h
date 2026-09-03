#pragma once

#include "panning.h"

#include <optional>
#include <string>
#include <string_view>

namespace panning_wallpaper {

constexpr int kMinimumGuiDurationSeconds = 10;
constexpr int kMaximumGuiDurationSeconds = 600;
constexpr int kMaximumPositionSliderValue = 100;

struct WallpaperSettings {
    std::wstring imagePath;
    PanningConfiguration configuration;
};

class SettingsState final {
public:
    [[nodiscard]] WallpaperSettings& Edited() noexcept;
    [[nodiscard]] const WallpaperSettings& Edited() const noexcept;
    [[nodiscard]] const std::optional<WallpaperSettings>& Applied() const noexcept;

    void MarkApplied();
    void ClearApplied() noexcept;

private:
    WallpaperSettings edited_;
    std::optional<WallpaperSettings> applied_;
};

[[nodiscard]] double DurationFromSlider(int sliderValue) noexcept;
[[nodiscard]] int DurationToSlider(double durationSeconds) noexcept;
[[nodiscard]] double PositionFromSlider(int sliderValue) noexcept;
[[nodiscard]] int PositionToSlider(double position) noexcept;
[[nodiscard]] bool TryParseGuiDuration(
    std::wstring_view text,
    int& durationSeconds) noexcept;
[[nodiscard]] bool IsValidGuiConfiguration(
    const PanningConfiguration& configuration) noexcept;
[[nodiscard]] bool CanApplyEditedSettings(
    const WallpaperSettings& settings,
    bool durationTextValid) noexcept;

}  // namespace panning_wallpaper
