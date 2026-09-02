#pragma once

#include <span>

namespace panning_wallpaper {

struct CoverageRectangle {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

[[nodiscard]] bool IsFullyCovered(
    const CoverageRectangle& target,
    std::span<const CoverageRectangle> occluders);

}  // namespace panning_wallpaper
