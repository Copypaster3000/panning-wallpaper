# Panning Wallpaper

Panning Wallpaper is a lightweight native Windows application for smoothly
panning still-image wallpapers. The project is under development; the current
milestone provides only the Windows desktop-hosting and Direct3D 11 rendering
foundation, displaying a solid test color behind the desktop icons.

## Requirements

- Windows 10 or Windows 11
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.23 or later

## Build and run

From a Developer PowerShell for Visual Studio:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
& .\build\Release\PanningWallpaper.exe
```

The desktop should show a blue-green Direct3D test surface while its icons
remain visible and usable. Press `Ctrl+Alt+Shift+Q` to exit cleanly and restore
the normal desktop wallpaper.

The Explorer desktop-hosting technique used by this early milestone relies on
undocumented shell behavior. It is intended for current Windows 10 and Windows
11 Explorer versions, but is not a compatibility guarantee across future shell
updates.
