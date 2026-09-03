#include "preview_image.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>

namespace panning_wallpaper {

bool CreateBoundedPreviewImage(
    const DecodedImage& source,
    std::uint32_t maximumWidth,
    std::uint32_t maximumHeight,
    DecodedImage& preview,
    std::wstring& error) {
    preview = {};
    error.clear();
    if (source.width == 0 || source.height == 0 || source.rowPitch == 0 ||
        source.pixels.empty() || maximumWidth == 0 || maximumHeight == 0) {
        error = L"The decoded image cannot be used for a preview.";
        return false;
    }
    if (source.width > std::numeric_limits<std::uint32_t>::max() / 4U ||
        source.rowPitch < source.width * 4U ||
        static_cast<std::size_t>(source.rowPitch) * source.height >
            source.pixels.size()) {
        error = L"The decoded image data is incomplete.";
        return false;
    }

    const double scale = std::min({
        1.0,
        static_cast<double>(maximumWidth) / source.width,
        static_cast<double>(maximumHeight) / source.height,
    });
    const std::uint32_t width = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::lround(source.width * scale)));
    const std::uint32_t height = std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::lround(source.height * scale)));
    if (width > std::numeric_limits<std::uint32_t>::max() / 4U) {
        error = L"The preview image dimensions are too large.";
        return false;
    }

    const std::uint32_t rowPitch = width * 4U;
    try {
        preview.pixels.resize(static_cast<std::size_t>(rowPitch) * height);
    } catch (const std::bad_alloc&) {
        error = L"Memory allocation for the image preview failed.";
        return false;
    }

    for (std::uint32_t y = 0; y < height; ++y) {
        const double sourceY = (y + 0.5) * source.height / height - 0.5;
        const std::uint32_t y0 = static_cast<std::uint32_t>(
            std::clamp(std::floor(sourceY), 0.0, source.height - 1.0));
        const std::uint32_t y1 = std::min(y0 + 1, source.height - 1);
        const double fractionY = std::clamp(sourceY - y0, 0.0, 1.0);

        for (std::uint32_t x = 0; x < width; ++x) {
            const double sourceX = (x + 0.5) * source.width / width - 0.5;
            const std::uint32_t x0 = static_cast<std::uint32_t>(
                std::clamp(std::floor(sourceX), 0.0, source.width - 1.0));
            const std::uint32_t x1 = std::min(x0 + 1, source.width - 1);
            const double fractionX = std::clamp(sourceX - x0, 0.0, 1.0);

            const auto* topLeft = source.pixels.data() +
                static_cast<std::size_t>(y0) * source.rowPitch + x0 * 4U;
            const auto* topRight = source.pixels.data() +
                static_cast<std::size_t>(y0) * source.rowPitch + x1 * 4U;
            const auto* bottomLeft = source.pixels.data() +
                static_cast<std::size_t>(y1) * source.rowPitch + x0 * 4U;
            const auto* bottomRight = source.pixels.data() +
                static_cast<std::size_t>(y1) * source.rowPitch + x1 * 4U;
            auto* destination = preview.pixels.data() +
                static_cast<std::size_t>(y) * rowPitch + x * 4U;

            for (std::size_t channel = 0; channel < 4; ++channel) {
                const double top = topLeft[channel] +
                    (topRight[channel] - topLeft[channel]) * fractionX;
                const double bottom = bottomLeft[channel] +
                    (bottomRight[channel] - bottomLeft[channel]) * fractionX;
                destination[channel] = static_cast<std::uint8_t>(std::lround(
                    top + (bottom - top) * fractionY));
            }
        }
    }

    preview.width = width;
    preview.height = height;
    preview.rowPitch = rowPitch;
    return true;
}

}  // namespace panning_wallpaper
