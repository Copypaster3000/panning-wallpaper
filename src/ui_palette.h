#pragma once

#include <cstdint>

namespace panning_wallpaper {

enum class UiTheme {
    Light,
    Dark,
};

struct UiPalette {
    std::uint32_t windowBackground;
    std::uint32_t panelBackground;
    std::uint32_t controlSurface;
    std::uint32_t controlHover;
    std::uint32_t border;
    std::uint32_t primaryText;
    std::uint32_t secondaryText;
    std::uint32_t selectedSurface;
    std::uint32_t selectedHover;
    std::uint32_t selectedPressed;
    std::uint32_t selectedText;
    std::uint32_t disabledSurface;
    std::uint32_t disabledText;
    std::uint32_t sliderTrack;
    std::uint32_t sliderThumb;
    std::uint32_t toggleOff;
    std::uint32_t toggleThumb;
    std::uint32_t errorBackground;
    std::uint32_t errorText;
    std::uint32_t previewBackground;
    std::uint32_t previewPlaceholder;
};

inline constexpr UiPalette kLightPalette{
    .windowBackground = 0xF7F6F4,
    .panelBackground = 0xFCFBF9,
    .controlSurface = 0xF8F7F5,
    .controlHover = 0xF1F0ED,
    .border = 0xDAD8D4,
    .primaryText = 0x373533,
    .secondaryText = 0x706D69,
    .selectedSurface = 0x595753,
    .selectedHover = 0x4A4845,
    .selectedPressed = 0x413F3C,
    .selectedText = 0xFFFFFF,
    .disabledSurface = 0xE1DFDB,
    .disabledText = 0x918E8A,
    .sliderTrack = 0xDAD8D4,
    .sliderThumb = 0x8A8680,
    .toggleOff = 0xAAA69F,
    .toggleThumb = 0xFFFFFF,
    .errorBackground = 0xFFE8E8,
    .errorText = 0x800000,
    .previewBackground = 0xEEEDEA,
    .previewPlaceholder = 0xF8F7F5,
};

inline constexpr UiPalette kDarkPalette{
    .windowBackground = 0x24221F,
    .panelBackground = 0x2D2A27,
    .controlSurface = 0x37332F,
    .controlHover = 0x423D38,
    .border = 0x5C554D,
    .primaryText = 0xF0E8DC,
    .secondaryText = 0xBBB0A2,
    .selectedSurface = 0x6F665D,
    .selectedHover = 0x7B7167,
    .selectedPressed = 0x5B544D,
    .selectedText = 0xF7F0E6,
    .disabledSurface = 0x33302D,
    .disabledText = 0x847C73,
    .sliderTrack = 0x655E56,
    .sliderThumb = 0xE0D5C7,
    .toggleOff = 0x46413C,
    .toggleThumb = 0xE9DFD2,
    .errorBackground = 0x4A2F2F,
    .errorText = 0xF1C5BE,
    .previewBackground = 0x292622,
    .previewPlaceholder = 0x37332F,
};

[[nodiscard]] constexpr const UiPalette& PaletteForTheme(
    UiTheme theme) noexcept {
    return theme == UiTheme::Dark ? kDarkPalette : kLightPalette;
}

}  // namespace panning_wallpaper
