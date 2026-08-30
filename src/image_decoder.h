#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace panning_wallpaper {

struct DecodedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    std::vector<std::uint8_t> pixels;
};

[[nodiscard]] bool DecodeImageFile(
    std::wstring_view path,
    DecodedImage& image,
    std::wstring& error);

}  // namespace panning_wallpaper
