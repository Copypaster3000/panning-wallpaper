#pragma once

#include "image_decoder.h"

#include <cstdint>
#include <string>

namespace panning_wallpaper {

[[nodiscard]] bool CreateBoundedPreviewImage(
    const DecodedImage& source,
    std::uint32_t maximumWidth,
    std::uint32_t maximumHeight,
    DecodedImage& preview,
    std::wstring& error);

}  // namespace panning_wallpaper
