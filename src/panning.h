#pragma once

#include <cstdint>

namespace panning_wallpaper {

enum class PanDirection {
    Left,
    Right,
    Up,
    Down,
};

enum class FitMode {
    Pan,
    Cover,
};

struct PanningConfiguration {
    PanDirection direction = PanDirection::Left;
    double loopDurationSeconds = 90.0;
    FitMode fitMode = FitMode::Pan;
    double position = 0.5;
    bool pauseWhenCovered = true;
};

struct PanTransform {
    float scaleU = 1.0F;
    float scaleV = 1.0F;
    float offsetU = 0.0F;
    float offsetV = 0.0F;
};

[[nodiscard]] bool IsHorizontal(PanDirection direction) noexcept;
[[nodiscard]] bool IsValidPanningConfiguration(
    const PanningConfiguration& configuration) noexcept;
[[nodiscard]] double CalculateLoopProgress(
    double elapsedSeconds,
    double loopDurationSeconds) noexcept;
[[nodiscard]] PanTransform CalculatePanTransform(
    const PanningConfiguration& configuration,
    double progress,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint32_t imageWidth,
    std::uint32_t imageHeight) noexcept;
[[nodiscard]] bool HasFramingEffect(
    const PanningConfiguration& configuration,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint32_t imageWidth,
    std::uint32_t imageHeight) noexcept;

}  // namespace panning_wallpaper
