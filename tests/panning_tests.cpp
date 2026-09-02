#include "command_line.h"
#include "panning.h"

#include <array>
#include <cmath>
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
    using panning_wallpaper::PanDirection;

    CommandLineOptions options;
    std::wstring error;
    Check(Parse(std::array{L"app", L"image.png"}, options, error),
          "one-argument invocation parses");
    Check(options.panning.direction == PanDirection::Left,
          "default direction is left");
    CheckNear(options.panning.loopDurationSeconds, 90.0,
              "default duration is 90 seconds");

    Check(Parse(
              std::array{L"app", L"image.png", L"--direction", L"left",
                         L"--duration", L"90"},
              options,
              error),
          "explicit left and 90-second options parse");

    Check(Parse(
              std::array{L"app", L"image.png", L"--direction", L"right",
                         L"--duration", L"60"},
              options,
              error),
          "right and 60-second options parse");
    Check(options.panning.direction == PanDirection::Right,
          "right direction is retained");
    CheckNear(options.panning.loopDurationSeconds, 60.0,
              "60-second duration is retained");

    Check(Parse(
              std::array{L"app", L"image.png", L"--duration", L"120",
                         L"--direction", L"up"},
              options,
              error),
          "options parse in either order");
    Check(options.panning.direction == PanDirection::Up,
          "up direction is retained");
    CheckNear(options.panning.loopDurationSeconds, 120.0,
              "120-second duration is retained");

    Check(Parse(
              std::array{L"app", L"image.png", L"--direction", L"down"},
              options,
              error),
          "down direction parses");
    Check(options.panning.direction == PanDirection::Down,
          "down direction is retained");

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
    Rejects(std::array{L"app", L"image.png", L"extra"},
            "unexpected positional argument is rejected");
}

void TestTiming() {
    using panning_wallpaper::CalculateLoopProgress;
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

void TestTransforms() {
    using panning_wallpaper::CalculatePanTransform;
    using panning_wallpaper::PanDirection;

    const auto left = CalculatePanTransform(
        PanDirection::Left, 0.25, 1920, 1080, 3840, 1080);
    CheckNear(left.scaleU, 0.5, "left scales image height to viewport height");
    CheckNear(left.scaleV, 1.0, "left leaves V scale unchanged");
    CheckNear(left.offsetU, 0.25, "left uses positive U sample offset");
    CheckNear(left.offsetV, 0.0, "left does not pan vertically");

    const auto right = CalculatePanTransform(
        PanDirection::Right, 0.25, 1920, 1080, 3840, 1080);
    CheckNear(right.scaleU, 0.5, "right scales image height to viewport height");
    CheckNear(right.scaleV, 1.0, "right leaves V scale unchanged");
    CheckNear(right.offsetU, -0.25, "right uses negative U sample offset");
    CheckNear(right.offsetV, 0.0, "right does not pan vertically");

    const auto up = CalculatePanTransform(
        PanDirection::Up, 0.25, 1920, 1080, 1920, 2160);
    CheckNear(up.scaleU, 1.0, "up leaves U scale unchanged");
    CheckNear(up.scaleV, 0.5, "up scales image width to viewport width");
    CheckNear(up.offsetU, 0.0, "up does not pan horizontally");
    CheckNear(up.offsetV, 0.25, "up uses positive V sample offset");

    const auto down = CalculatePanTransform(
        PanDirection::Down, 0.25, 1920, 1080, 1920, 2160);
    CheckNear(down.scaleU, 1.0, "down leaves U scale unchanged");
    CheckNear(down.scaleV, 0.5, "down scales image width to viewport width");
    CheckNear(down.offsetU, 0.0, "down does not pan horizontally");
    CheckNear(down.offsetV, -0.25, "down uses negative V sample offset");

    const auto narrowHorizontal = CalculatePanTransform(
        PanDirection::Left, 0.0, 1920, 1080, 1080, 1920);
    Check(narrowHorizontal.scaleU > 1.0F,
          "narrow horizontal image repeats through U wrapping");

    const auto shortVertical = CalculatePanTransform(
        PanDirection::Up, 0.0, 1920, 1080, 3840, 1080);
    Check(shortVertical.scaleV > 1.0F,
          "short vertical image repeats through V wrapping");
}

}  // namespace

int main() {
    TestCommandLine();
    TestTiming();
    TestTransforms();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }

    std::cout << "All panning tests passed.\n";
    return 0;
}
