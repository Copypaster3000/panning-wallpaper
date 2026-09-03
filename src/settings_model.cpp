#include "settings_model.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace panning_wallpaper {

WallpaperSettings& SettingsState::Edited() noexcept {
    return edited_;
}

const WallpaperSettings& SettingsState::Edited() const noexcept {
    return edited_;
}

const std::optional<WallpaperSettings>& SettingsState::Applied() const noexcept {
    return applied_;
}

void SettingsState::MarkApplied() {
    applied_ = edited_;
}

void SettingsState::ClearApplied() noexcept {
    applied_.reset();
}

double DurationFromSlider(int sliderValue) noexcept {
    return static_cast<double>(std::clamp(
        sliderValue,
        kMinimumGuiDurationSeconds,
        kMaximumGuiDurationSeconds));
}

int DurationToSlider(double durationSeconds) noexcept {
    if (!std::isfinite(durationSeconds)) {
        return kMinimumGuiDurationSeconds;
    }
    const double clamped = std::clamp(
        durationSeconds,
        static_cast<double>(kMinimumGuiDurationSeconds),
        static_cast<double>(kMaximumGuiDurationSeconds));
    return static_cast<int>(std::lround(clamped));
}

double PositionFromSlider(int sliderValue) noexcept {
    return static_cast<double>(
        std::clamp(sliderValue, 0, kMaximumPositionSliderValue)) /
        kMaximumPositionSliderValue;
}

int PositionToSlider(double position) noexcept {
    if (!std::isfinite(position)) {
        return 0;
    }
    const double clamped = std::clamp(position, 0.0, 1.0);
    return static_cast<int>(
        std::lround(clamped * kMaximumPositionSliderValue));
}

bool TryParseGuiDuration(
    std::wstring_view text,
    int& durationSeconds) noexcept {
    if (text.empty()) {
        return false;
    }

    const std::wstring value(text);
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0' || errno == ERANGE ||
        parsed < kMinimumGuiDurationSeconds ||
        parsed > kMaximumGuiDurationSeconds) {
        return false;
    }

    durationSeconds = static_cast<int>(parsed);
    return true;
}

bool IsValidGuiConfiguration(
    const PanningConfiguration& configuration) noexcept {
    return IsValidPanningConfiguration(configuration) &&
           configuration.loopDurationSeconds >= kMinimumGuiDurationSeconds &&
           configuration.loopDurationSeconds <= kMaximumGuiDurationSeconds &&
           std::floor(configuration.loopDurationSeconds) ==
               configuration.loopDurationSeconds;
}

}  // namespace panning_wallpaper
