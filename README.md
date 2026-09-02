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
PanningWallpaper.exe <image-path> [--direction <left|right|up|down>] [--duration <seconds>]
```

Direction defaults to `left`, and duration defaults to 90 seconds for one
complete image period. Left and right motion scale the image uniformly to the
desktop height; up and down motion scale it uniformly to the desktop width.
PNG and JPEG images are decoded with Windows Imaging Component.

There is no graphical settings UI yet. The rendering cadence remains fixed at
approximately 30 FPS, and fit or manual positioning controls are not yet
implemented. Press `Ctrl+Alt+Shift+Q` to exit cleanly and restore the normal
desktop wallpaper.

The Explorer desktop-hosting technique used by this early milestone relies on
undocumented shell behavior. It is intended for current Windows 10 and Windows
11 Explorer versions, but is not a compatibility guarantee across future shell
updates.
