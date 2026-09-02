# Panning Wallpaper

Panning Wallpaper is a lightweight native Windows application for smoothly
panning still-image wallpapers. The project is under development; the current
milestone provides only the Windows desktop-hosting and Direct3D 11 rendering
foundation plus configurable horizontal or vertical still-image panning behind
the desktop icons.

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

The Explorer desktop-hosting technique used by this early milestone relies on
undocumented shell behavior. It is intended for current Windows 10 and Windows
11 Explorer versions, but is not a compatibility guarantee across future shell
updates.
