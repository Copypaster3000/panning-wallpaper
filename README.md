# Panning Wallpaper

Panning Wallpaper is a lightweight native Windows application for smoothly
panning still-image wallpapers. The project is under development; the current
milestone provides only the Windows desktop-hosting and Direct3D 11 rendering
foundation plus horizontally panned still-image rendering behind the desktop
icons.

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

PNG and JPEG images are decoded with Windows Imaging Component. The image is
scaled uniformly to the desktop height, pans leftward, and repeats horizontally.
M1B supports horizontal panning only and uses a fixed 90-second loop at an
approximately 30 FPS cadence. Press `Ctrl+Alt+Shift+Q` to exit cleanly and
restore the normal desktop wallpaper.

The Explorer desktop-hosting technique used by this early milestone relies on
undocumented shell behavior. It is intended for current Windows 10 and Windows
11 Explorer versions, but is not a compatibility guarantee across future shell
updates.
