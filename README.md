# Panning Wallpaper

Panning Wallpaper is a lightweight native Windows application for smoothly
panning still-image wallpapers. The project is under development; the current
milestone provides Windows desktop hosting, Direct3D 11 still-image panning,
and visibility-aware rendering behind the desktop icons.

## Requirements

- Windows 10 or Windows 11
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.23 or later

## Build and run

From a Developer PowerShell for Visual Studio:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
& .\build\Release\PanningWallpaper.exe "C:\path\to\wallpaper.png"
```

The command syntax is:

```text
PanningWallpaper.exe <image-path> [--direction <left|right|up|down>]
    [--duration <seconds>] [--fit <pan|cover>] [--position <0..1>]
    [--pause-when-covered <on|off>]
```

Direction defaults to `left`, and duration defaults to 90 seconds for one
complete image period. Fit defaults to `pan`, which scales left/right motion to
the desktop height and up/down motion to the desktop width. `cover` instead
scales the image proportionally until it fills the desktop without
letterboxing.

Position defaults to `0.5` and accepts values from `0` through `1`. During
horizontal panning it selects top-to-bottom framing; during vertical panning it
selects left-to-right framing. PNG and JPEG images are decoded with Windows
Imaging Component.

There is no graphical settings UI yet. The rendering cadence remains fixed at
approximately 30 FPS. Press `Ctrl+Alt+Shift+Q` to exit cleanly and restore the
normal desktop wallpaper.

## Visibility-aware rendering

Rendering automatically stops when the combined coverage of supported opaque
top-level windows fully covers the entire wallpaper surface. Multiple windows
are considered together, including across a multi-monitor surface, and
rendering resumes automatically when any part becomes exposed. This behavior
defaults to on and can be disabled for setups that use unusual overlays:

```text
--pause-when-covered off
```

Session lock and console-display-off states always pause rendering, regardless
of that option. GPU resources remain resident while paused, and animation
resumes at its current absolute-time phase rather than replaying hidden frames.

Coverage detection excludes known transparent or ambiguous layered windows
where practical. Windows does not provide a perfect general-purpose query for
whether every pixel of an arbitrary window is opaque, so detection is
intentionally conservative and may continue rendering in uncertain cases.

## Performance snapshot

M3A development measurements used a 4096x1367 PNG in left/pan mode for 30
seconds after one warm-up frame. The test system ran Windows 11 build 26200 on
an Intel Core i7-10750H (6 cores/12 threads), 32 GB RAM, and the default Intel
UHD Graphics adapter (driver 31.0.101.2137).

| Render surface | Frames/FPS | P90 interval, before -> after | Maximum interval, before -> after | Process GPU, before -> after |
| --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 900 / 30.0 | 46.0 -> 33.6 ms | 48.4 -> 33.9 ms | 4.65% -> 4.39% |
| 2560x1440 | 900 / 30.0 | 46.0 -> 33.6 ms | 48.0 -> 34.0 ms | 5.65% -> 5.58% |
| 3072x1263 | 900 / 30.0 | 46.0 -> 33.6 ms | 48.0 -> 34.1 ms | 6.56% -> 6.31% |

An 18-second process-thread counter check measured approximately 70 context
switches per second before and 32 after, close to the intended 30 wakeups per
second.

Post-change process CPU time was 0.16-0.55 seconds per 30-second run (under
0.2% of total capacity on this 12-logical-processor system). Working set was
approximately 48 MiB and private memory approximately 30 MiB, with no
continuing memory growth in a 125-second run. GPU values are process-specific
Windows GPU Engine counter samples, not whole-system utilization. The harness
uses the production renderer and scheduling algorithm in a standalone render
window; these results are a single-machine snapshot, not a performance
guarantee for other hardware or actual Explorer desktop composition.

The Explorer desktop-hosting technique used by this early milestone relies on
undocumented shell behavior. It is intended for current Windows 10 and Windows
11 Explorer versions, but is not a compatibility guarantee across future shell
updates.
