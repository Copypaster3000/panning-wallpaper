#include "command_line.h"
#include "coverage_geometry.h"
#include "panning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

void CheckNear(double actual, double expected, const char* description) {
    Check(std::abs(actual - expected) < 0.000001, description);
}

template <std::size_t Size>
bool Parse(
    std::array<const wchar_t*, Size> arguments,
    panning_wallpaper::CommandLineOptions& options,
    std::wstring& error) {
    std::array<wchar_t*, Size> mutableArguments{};
    for (std::size_t index = 0; index < Size; ++index) {
        mutableArguments[index] = const_cast<wchar_t*>(arguments[index]);
    }
    return panning_wallpaper::ParseCommandLine(
        static_cast<int>(Size), mutableArguments.data(), options, error);
}

void TestCommandLine() {
    using panning_wallpaper::CommandLineOptions;
    using panning_wallpaper::FitMode;
    using panning_wallpaper::PanDirection;

    CommandLineOptions options;
    std::wstring error;
    Check(Parse(std::array{L"app", L"image.png"}, options, error),
          "one-argument invocation parses");
    Check(options.panning.direction == PanDirection::Left,
          "default direction is left");
    CheckNear(options.panning.loopDurationSeconds, 90.0,
              "default duration is 90 seconds");
    Check(options.panning.fitMode == FitMode::Pan,
          "default fit mode is pan");
    CheckNear(options.panning.position, 0.5,
              "default position is centered");
    Check(options.panning.pauseWhenCovered,
          "coverage pausing defaults on");

    const CommandLineOptions defaults = options;

    Check(Parse(
              std::array{L"app", L"image.png", L"--direction", L"left",
                         L"--duration", L"90", L"--fit", L"pan",
                         L"--position", L"0.5", L"--pause-when-covered", L"on"},
              options,
              error),
          "all explicit defaults parse");
    Check(options.panning.direction == defaults.panning.direction &&
              options.panning.loopDurationSeconds ==
                  defaults.panning.loopDurationSeconds &&
              options.panning.fitMode == defaults.panning.fitMode &&
              options.panning.position == defaults.panning.position &&
              options.panning.pauseWhenCovered ==
                  defaults.panning.pauseWhenCovered,
          "implicit and explicit defaults are equivalent");

    Check(Parse(
              std::array{L"app", L"image.png", L"--direction", L"right",
                         L"--duration", L"60", L"--fit", L"cover",
                         L"--position", L"0.25"},
              options,
              error),
          "right, duration, cover, and position options parse");
    Check(options.panning.direction == PanDirection::Right,
          "right direction is retained");
    CheckNear(options.panning.loopDurationSeconds, 60.0,
              "60-second duration is retained");
    Check(options.panning.fitMode == FitMode::Cover,
          "cover fit mode is retained");
    CheckNear(options.panning.position, 0.25,
              "quarter position is retained");

    Check(Parse(
              std::array{L"app", L"image.png", L"--pause-when-covered", L"off"},
              options,
              error),
          "coverage pausing accepts off");
    Check(!options.panning.pauseWhenCovered,
          "coverage pausing retains off");

    Check(Parse(
              std::array{L"app", L"image.png", L"--duration", L"120",
                         L"--position", L"1", L"--direction", L"up"},
              options,
              error),
          "options parse in either order");
    Check(options.panning.direction == PanDirection::Up,
          "up direction is retained");
    CheckNear(options.panning.loopDurationSeconds, 120.0,
              "120-second duration is retained");
    CheckNear(options.panning.position, 1.0,
              "position upper boundary is accepted");

    Check(Parse(
              std::array{L"app", L"image.png", L"--direction", L"down",
                         L"--position", L"0"},
              options,
              error),
          "down direction parses");
    Check(options.panning.direction == PanDirection::Down,
          "down direction is retained");
    CheckNear(options.panning.position, 0.0,
              "position lower boundary is accepted");

    const auto Rejects = [&](auto arguments, const char* description) {
        Check(!Parse(arguments, options, error) && !error.empty(), description);
    };
    Rejects(std::array{L"app"}, "missing image is rejected");
    Rejects(std::array{L"app", L"image.png", L"--unknown"},
            "unknown option is rejected");
    Rejects(std::array{L"app", L"image.png", L"--direction"},
            "missing direction is rejected");
    Rejects(std::array{L"app", L"image.png", L"--direction", L"diagonal"},
            "invalid direction is rejected");
    Rejects(std::array{L"app", L"image.png", L"--direction", L"left",
                       L"--direction", L"right"},
            "duplicate direction is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration"},
            "missing duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration", L"abc"},
            "malformed duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration", L"0"},
            "zero duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration", L"-1"},
            "negative duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration", L"nan"},
            "NaN duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration", L"inf"},
            "infinite duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--duration", L"60",
                       L"--duration", L"120"},
            "duplicate duration is rejected");
    Rejects(std::array{L"app", L"image.png", L"--fit"},
            "missing fit mode is rejected");
    Rejects(std::array{L"app", L"image.png", L"--fit", L"contain"},
            "unknown fit mode is rejected");
    Rejects(std::array{L"app", L"image.png", L"--fit", L"pan",
                       L"--fit", L"cover"},
            "duplicate fit mode is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position"},
            "missing position is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position", L"center"},
            "malformed position is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position", L"nan"},
            "NaN position is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position", L"inf"},
            "infinite position is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position", L"-0.01"},
            "position below zero is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position", L"1.01"},
            "position above one is rejected");
    Rejects(std::array{L"app", L"image.png", L"--position", L"0.25",
                       L"--position", L"0.75"},
            "duplicate position is rejected");
    Rejects(std::array{L"app", L"image.png", L"--pause-when-covered"},
            "missing pause-when-covered value is rejected");
    Rejects(std::array{L"app", L"image.png", L"--pause-when-covered", L"true"},
            "unknown pause-when-covered value is rejected");
    Rejects(std::array{L"app", L"image.png", L"--pause-when-covered", L"on",
                       L"--pause-when-covered", L"off"},
            "duplicate pause-when-covered option is rejected");
    Rejects(std::array{L"app", L"image.png", L"extra"},
            "unexpected positional argument is rejected");
}

void TestCoverageGeometry() {
    using panning_wallpaper::CoverageRectangle;
    using panning_wallpaper::IsFullyCovered;

    constexpr CoverageRectangle wallpaper{0, 0, 100, 80};
    Check(IsFullyCovered(wallpaper, std::array{CoverageRectangle{0, 0, 100, 80}}),
          "one exact rectangle covers wallpaper");
    Check(!IsFullyCovered(wallpaper, std::array{CoverageRectangle{0, 0, 99, 80}}),
          "one exposed edge is not covered");
    Check(IsFullyCovered(wallpaper, std::array{CoverageRectangle{-10, -20, 120, 90}}),
          "one oversized rectangle covers wallpaper");
    Check(IsFullyCovered(
              wallpaper,
              std::array{CoverageRectangle{0, 0, 50, 80},
                         CoverageRectangle{50, 0, 100, 80}}),
          "two half-width rectangles cover wallpaper");
    Check(!IsFullyCovered(
              wallpaper,
              std::array{CoverageRectangle{0, 0, 49, 80},
                         CoverageRectangle{51, 0, 100, 80}}),
          "two rectangles with a gap do not cover wallpaper");
    Check(!IsFullyCovered(
              wallpaper,
              std::array{CoverageRectangle{0, 0, 75, 60},
                         CoverageRectangle{25, 20, 100, 80}}),
          "heavy overlap does not hide uncovered corners");
    Check(IsFullyCovered(
              wallpaper,
              std::array{CoverageRectangle{0, 0, 35, 80},
                         CoverageRectangle{35, 0, 70, 80},
                         CoverageRectangle{70, 0, 100, 80}}),
          "three rectangles together cover wallpaper");
    Check(IsFullyCovered(
              wallpaper,
              std::array{CoverageRectangle{-40, -30, 60, 100},
                         CoverageRectangle{60, -30, 140, 100}}),
          "outside rectangles clip to wallpaper");

    constexpr CoverageRectangle negativeWallpaper{-200, -100, 0, 100};
    Check(IsFullyCovered(
              negativeWallpaper,
              std::array{CoverageRectangle{-250, -150, -100, 150},
                         CoverageRectangle{-100, -150, 20, 150}}),
          "negative virtual-desktop coordinates are covered correctly");

    constexpr CoverageRectangle twoMonitorWallpaper{-1920, 0, 1920, 1080};
    Check(IsFullyCovered(
              twoMonitorWallpaper,
              std::array{CoverageRectangle{-1920, 0, 0, 1080},
                         CoverageRectangle{0, 0, 1920, 1080}}),
          "multiple-monitor-shaped virtual coordinates require both halves");
    Check(!IsFullyCovered(wallpaper, std::array<CoverageRectangle, 0>{}),
          "empty occluder set does not cover wallpaper");
}

void TestTiming() {
    using panning_wallpaper::CalculateLoopProgress;
    using panning_wallpaper::FitMode;
    using panning_wallpaper::IsValidPanningConfiguration;
    using panning_wallpaper::PanDirection;
    using panning_wallpaper::PanningConfiguration;

    Check(IsValidPanningConfiguration(PanningConfiguration{}),
          "default configuration is valid");
    Check(!IsValidPanningConfiguration(
              PanningConfiguration{PanDirection::Left, 0.0}),
          "configuration rejects zero duration");
    Check(!IsValidPanningConfiguration(PanningConfiguration{
              static_cast<PanDirection>(99), 90.0}),
          "configuration rejects unknown direction values");
    Check(!IsValidPanningConfiguration(PanningConfiguration{
              PanDirection::Left, 90.0, static_cast<FitMode>(99), 0.5}),
          "configuration rejects unknown fit mode values");
    Check(!IsValidPanningConfiguration(PanningConfiguration{
              PanDirection::Left, 90.0, FitMode::Pan, -0.01}),
          "configuration rejects position below zero");
    Check(!IsValidPanningConfiguration(PanningConfiguration{
              PanDirection::Left, 90.0, FitMode::Pan, 1.01}),
          "configuration rejects position above one");

    for (const double duration : {90.0, 60.0, 120.0}) {
        CheckNear(CalculateLoopProgress(0.0, duration), 0.0,
                  "period starts at zero");
        CheckNear(CalculateLoopProgress(duration * 0.25, duration), 0.25,
                  "quarter period has quarter progress");
        CheckNear(CalculateLoopProgress(duration, duration), 0.0,
                  "complete period wraps to zero");
        CheckNear(CalculateLoopProgress(duration * 2.5, duration), 0.5,
                  "multiple periods use absolute modulo progress");
    }
}

void CheckCoverTransform(
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    std::uint32_t imageWidth,
    std::uint32_t imageHeight,
    const char* scaleDescription,
    const char* aspectDescription) {
    using panning_wallpaper::CalculatePanTransform;
    using panning_wallpaper::FitMode;
    using panning_wallpaper::PanDirection;
    using panning_wallpaper::PanningConfiguration;

    const PanningConfiguration configuration{
        PanDirection::Left, 90.0, FitMode::Cover, 0.5};
    const auto transform = CalculatePanTransform(
        configuration,
        0.0,
        viewportWidth,
        viewportHeight,
        imageWidth,
        imageHeight);
    const double displayedWidth = viewportWidth / transform.scaleU;
    const double displayedHeight = viewportHeight / transform.scaleV;
    const double expectedScale = std::max(
        static_cast<double>(viewportWidth) / imageWidth,
        static_cast<double>(viewportHeight) / imageHeight);

    CheckNear(displayedWidth / imageWidth, expectedScale, scaleDescription);
    CheckNear(displayedHeight / imageHeight, expectedScale, scaleDescription);
    CheckNear(
        displayedWidth / displayedHeight,
        static_cast<double>(imageWidth) / imageHeight,
        aspectDescription);
    Check(displayedWidth + 0.001 >= viewportWidth &&
              displayedHeight + 0.001 >= viewportHeight,
          "cover fills both viewport dimensions");
}

void TestTransforms() {
    using panning_wallpaper::CalculatePanTransform;
    using panning_wallpaper::FitMode;
    using panning_wallpaper::PanDirection;
    using panning_wallpaper::PanningConfiguration;

    const PanningConfiguration leftConfiguration{
        PanDirection::Left, 90.0, FitMode::Pan, 0.5};
    const auto left = CalculatePanTransform(
        leftConfiguration, 0.25, 1920, 1080, 3840, 1080);
    CheckNear(left.scaleU, 0.5, "left scales image height to viewport height");
    CheckNear(left.scaleV, 1.0, "left leaves V scale unchanged");
    CheckNear(left.offsetU, 0.25, "left uses positive U sample offset");
    CheckNear(left.offsetV, 0.0, "left does not pan vertically");

    const PanningConfiguration rightConfiguration{
        PanDirection::Right, 90.0, FitMode::Pan, 0.5};
    const auto right = CalculatePanTransform(
        rightConfiguration, 0.25, 1920, 1080, 3840, 1080);
    CheckNear(right.scaleU, 0.5, "right scales image height to viewport height");
    CheckNear(right.scaleV, 1.0, "right leaves V scale unchanged");
    CheckNear(right.offsetU, -0.25, "right uses negative U sample offset");
    CheckNear(right.offsetV, 0.0, "right does not pan vertically");

    const PanningConfiguration upConfiguration{
        PanDirection::Up, 90.0, FitMode::Pan, 0.5};
    const auto up = CalculatePanTransform(
        upConfiguration, 0.25, 1920, 1080, 1920, 2160);
    CheckNear(up.scaleU, 1.0, "up leaves U scale unchanged");
    CheckNear(up.scaleV, 0.5, "up scales image width to viewport width");
    CheckNear(up.offsetU, 0.0, "up does not pan horizontally");
    CheckNear(up.offsetV, 0.25, "up uses positive V sample offset");

    const PanningConfiguration downConfiguration{
        PanDirection::Down, 90.0, FitMode::Pan, 0.5};
    const auto down = CalculatePanTransform(
        downConfiguration, 0.25, 1920, 1080, 1920, 2160);
    CheckNear(down.scaleU, 1.0, "down leaves U scale unchanged");
    CheckNear(down.scaleV, 0.5, "down scales image width to viewport width");
    CheckNear(down.offsetU, 0.0, "down does not pan horizontally");
    CheckNear(down.offsetV, -0.25, "down uses negative V sample offset");

    const auto narrowHorizontal = CalculatePanTransform(
        leftConfiguration, 0.0, 1920, 1080, 1080, 1920);
    Check(narrowHorizontal.scaleU > 1.0F,
          "narrow horizontal image repeats through U wrapping");

    const auto shortVertical = CalculatePanTransform(
        upConfiguration, 0.0, 1920, 1080, 3840, 1080);
    Check(shortVertical.scaleV > 1.0F,
          "short vertical image repeats through V wrapping");

    CheckCoverTransform(
        1920, 1080, 4000, 2000,
        "landscape-on-landscape cover uses maximum scale",
        "landscape-on-landscape cover preserves aspect ratio");
    CheckCoverTransform(
        1920, 1080, 2000, 4000,
        "portrait-on-landscape cover uses maximum scale",
        "portrait-on-landscape cover preserves aspect ratio");
    CheckCoverTransform(
        1080, 1920, 4000, 2000,
        "landscape-on-portrait cover uses maximum scale",
        "landscape-on-portrait cover preserves aspect ratio");
    CheckCoverTransform(
        1080, 1920, 2000, 4000,
        "portrait-on-portrait cover uses maximum scale",
        "portrait-on-portrait cover preserves aspect ratio");

    for (const double position : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const PanningConfiguration horizontalCover{
            PanDirection::Right, 90.0, FitMode::Cover, position};
        const auto transform = CalculatePanTransform(
            horizontalCover, 0.25, 1920, 1080, 2000, 4000);
        CheckNear(
            transform.offsetV,
            (1.0 - transform.scaleV) * position,
            "horizontal position aligns vertical excess content");
        CheckNear(transform.offsetU, -0.25,
                  "horizontal position does not alter active-axis motion");
    }

    for (const double position : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const PanningConfiguration verticalCover{
            PanDirection::Down, 90.0, FitMode::Cover, position};
        const auto transform = CalculatePanTransform(
            verticalCover, 0.25, 1080, 1920, 4000, 2000);
        CheckNear(
            transform.offsetU,
            (1.0 - transform.scaleU) * position,
            "vertical position aligns horizontal excess content");
        CheckNear(transform.offsetV, -0.25,
                  "vertical position does not alter active-axis motion");
    }

    const PanningConfiguration noInactiveExcess{
        PanDirection::Left, 90.0, FitMode::Cover, 1.0};
    const auto noPositionEffect = CalculatePanTransform(
        noInactiveExcess, 0.25, 1920, 1080, 4000, 2000);
    CheckNear(noPositionEffect.offsetV, 0.0,
              "position has no effect without inactive-axis excess");
}

}  // namespace

int main() {
    TestCommandLine();
    TestTiming();
    TestTransforms();
    TestCoverageGeometry();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All panning tests passed.\n";
    return 0;
}
