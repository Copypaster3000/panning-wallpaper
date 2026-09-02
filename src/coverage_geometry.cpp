#include "coverage_geometry.h"

#include <algorithm>
#include <vector>

namespace panning_wallpaper {
namespace {

[[nodiscard]] bool HasArea(const CoverageRectangle& rectangle) noexcept {
    return rectangle.left < rectangle.right && rectangle.top < rectangle.bottom;
}

[[nodiscard]] CoverageRectangle Intersect(
    const CoverageRectangle& first,
    const CoverageRectangle& second) noexcept {
    return {
        std::max(first.left, second.left),
        std::max(first.top, second.top),
        std::min(first.right, second.right),
        std::min(first.bottom, second.bottom),
    };
}

void AddIfNotEmpty(
    std::vector<CoverageRectangle>& rectangles,
    const CoverageRectangle& rectangle) {
    if (HasArea(rectangle)) {
        rectangles.push_back(rectangle);
    }
}

}  // namespace

bool IsFullyCovered(
    const CoverageRectangle& target,
    std::span<const CoverageRectangle> occluders) {
    if (!HasArea(target) || occluders.empty()) {
        return false;
    }

    std::vector<CoverageRectangle> uncovered{target};
    std::vector<CoverageRectangle> next;
    for (const CoverageRectangle& occluder : occluders) {
        if (!HasArea(occluder)) {
            continue;
        }

        next.clear();
        next.reserve(uncovered.size() * 2);
        for (const CoverageRectangle& rectangle : uncovered) {
            const CoverageRectangle intersection = Intersect(rectangle, occluder);
            if (!HasArea(intersection)) {
                next.push_back(rectangle);
                continue;
            }

            AddIfNotEmpty(next, {
                rectangle.left,
                rectangle.top,
                rectangle.right,
                intersection.top,
            });
            AddIfNotEmpty(next, {
                rectangle.left,
                intersection.bottom,
                rectangle.right,
                rectangle.bottom,
            });
            AddIfNotEmpty(next, {
                rectangle.left,
                intersection.top,
                intersection.left,
                intersection.bottom,
            });
            AddIfNotEmpty(next, {
                intersection.right,
                intersection.top,
                rectangle.right,
                intersection.bottom,
            });
        }

        uncovered.swap(next);
        if (uncovered.empty()) {
            return true;
        }
    }

    return false;
}

}  // namespace panning_wallpaper
