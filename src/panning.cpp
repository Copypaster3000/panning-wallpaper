#include "panning.h"

#include <algorithm>
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
    const bool knownFitMode =
        configuration.fitMode == FitMode::Pan ||
        configuration.fitMode == FitMode::Cover;
    return knownDirection && knownFitMode &&
           std::isfinite(configuration.loopDurationSeconds) &&
           configuration.loopDurationSeconds > 0.0 &&
           std::isfinite(configuration.position) &&
           configuration.position >= 0.0 &&
           configuration.position <= 1.0;
}

double CalculateLoopProgress(
    double elapsedSeconds,
    double loopDurationSeconds) noexcept {
    const double completedLoops = elapsedSeconds / loopDurationSeconds;
    return completedLoops - std::floor(completedLoops);
}

PanTransform CalculatePanTransform(
    const PanningConfiguration& configuration,
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

    const bool horizontal = IsHorizontal(configuration.direction);
    if (configuration.fitMode == FitMode::Pan) {
        if (horizontal) {
            const double displayedWidth =
                static_cast<double>(imageWidth) * viewportHeight / imageHeight;
            transform.scaleU = static_cast<float>(viewportWidth / displayedWidth);
        } else {
            const double displayedHeight =
                static_cast<double>(imageHeight) * viewportWidth / imageWidth;
            transform.scaleV = static_cast<float>(viewportHeight / displayedHeight);
        }
    } else {
        const double scale = std::max(
            static_cast<double>(viewportWidth) / imageWidth,
            static_cast<double>(viewportHeight) / imageHeight);
        const double displayedWidth = imageWidth * scale;
        const double displayedHeight = imageHeight * scale;
        transform.scaleU = static_cast<float>(viewportWidth / displayedWidth);
        transform.scaleV = static_cast<float>(viewportHeight / displayedHeight);
    }

    if (horizontal) {
        // A positive sample offset makes visible texture content move toward
        // decreasing screen coordinates; a negative offset reverses it.
        transform.offsetU = static_cast<float>(
            configuration.direction == PanDirection::Left ? progress : -progress);
        transform.offsetV = static_cast<float>(
            (1.0 - transform.scaleV) * configuration.position);
    } else {
        transform.offsetU = static_cast<float>(
            (1.0 - transform.scaleU) * configuration.position);
        transform.offsetV = static_cast<float>(
            configuration.direction == PanDirection::Up ? progress : -progress);
    }

    return transform;
}

bool HasFramingEffect(
    const PanningConfiguration& configuration,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint32_t imageWidth,
    std::uint32_t imageHeight) noexcept {
    if (viewportWidth == 0 || viewportHeight == 0 ||
        imageWidth == 0 || imageHeight == 0) {
        return false;
    }
    const PanTransform transform = CalculatePanTransform(
        configuration,
        0.0,
        viewportWidth,
        viewportHeight,
        imageWidth,
        imageHeight);
    // Framing operates perpendicular to motion. A sample scale below one on
    // that axis means the scaled image extends beyond the viewport.
    return IsHorizontal(configuration.direction)
        ? transform.scaleV < 1.0F
        : transform.scaleU < 1.0F;
}

}  // namespace panning_wallpaper
