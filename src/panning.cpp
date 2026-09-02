#include "panning.h"

#include <cmath>

namespace panning_wallpaper {

bool IsHorizontal(PanDirection direction) noexcept {
    return direction == PanDirection::Left || direction == PanDirection::Right;
}

bool IsValidPanningConfiguration(
    const PanningConfiguration& configuration) noexcept {
    const bool knownDirection =
        configuration.direction == PanDirection::Left ||
        configuration.direction == PanDirection::Right ||
        configuration.direction == PanDirection::Up ||
        configuration.direction == PanDirection::Down;
    return knownDirection &&
           std::isfinite(configuration.loopDurationSeconds) &&
           configuration.loopDurationSeconds > 0.0;
}

double CalculateLoopProgress(
    double elapsedSeconds,
    double loopDurationSeconds) noexcept {
    const double completedLoops = elapsedSeconds / loopDurationSeconds;
    return completedLoops - std::floor(completedLoops);
}

PanTransform CalculatePanTransform(
    PanDirection direction,
    double progress,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint32_t imageWidth,
    std::uint32_t imageHeight) noexcept {
    PanTransform transform;
    if (viewportWidth == 0 || viewportHeight == 0 ||
        imageWidth == 0 || imageHeight == 0) {
        return transform;
    }

    if (IsHorizontal(direction)) {
        const double displayedWidth =
            static_cast<double>(imageWidth) * viewportHeight / imageHeight;
        transform.scaleU = static_cast<float>(viewportWidth / displayedWidth);
        // A positive sample offset makes visible texture content move toward
        // decreasing screen coordinates; a negative offset reverses it.
        transform.offsetU = static_cast<float>(
            direction == PanDirection::Left ? progress : -progress);
    } else {
        const double displayedHeight =
            static_cast<double>(imageHeight) * viewportWidth / imageWidth;
        transform.scaleV = static_cast<float>(viewportHeight / displayedHeight);
        transform.offsetV = static_cast<float>(
            direction == PanDirection::Up ? progress : -progress);
    }

    return transform;
}

}  // namespace panning_wallpaper
