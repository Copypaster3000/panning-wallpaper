#pragma once

#include <cstdint>

namespace panning_wallpaper {

enum class PanDirection {
    Left,
    Right,
    Up,
    Down,
};

struct PanningConfiguration {
    PanDirection direction = PanDirection::Left;
    double loopDurationSeconds = 90.0;
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
    PanDirection direction,
    double progress,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint32_t imageWidth,
    std::uint32_t imageHeight) noexcept;

}  // namespace panning_wallpaper
