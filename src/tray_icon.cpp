#include "tray_icon.h"

#include <array>
#include <cstdint>
#include <cwchar>

namespace panning_wallpaper {
namespace {

HICON CreateTrayIcon() {
    // A dark picture frame with light horizontal arrows stays legible on either taskbar.
    constexpr int size = 32;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* rawPixels = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    if (color == nullptr) return nullptr;
    auto* pixels = static_cast<std::uint32_t*>(rawPixels);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool inside = x >= 3 && x <= 28 && y >= 5 && y <= 26;
            const bool border = inside && (x == 3 || x == 28 || y == 5 || y == 26);
            const bool arrow = (y >= 11 && y <= 12 && x >= 8 && x <= 23) ||
                (x >= 8 && x <= 12 && (y == 19 - x || y == x + 4)) ||
                (y >= 19 && y <= 20 && x >= 8 && x <= 23) ||
                (x >= 19 && x <= 23 && (y == x - 4 || y == 43 - x));
            pixels[y * size + x] = inside
                ? (border || arrow ? 0xFFF0F0F0U : 0xFF454545U) : 0;
        }
    }
    const std::array<BYTE, size * size / 8> maskBits{};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    HICON icon = nullptr;
    if (mask != nullptr) {
        ICONINFO iconInfo{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&iconInfo);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

}  // namespace

TrayIcon::~TrayIcon() { Shutdown(); }

bool TrayIcon::Initialize(HWND owner) {
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = CreateTrayIcon();
    wcscpy_s(data_.szTip, L"Panning Wallpaper");
    return data_.hIcon != nullptr && Restore();
}

bool TrayIcon::Restore() {
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    // The same HWND/ID identifies one icon, including repeated TaskbarCreated messages.
    registered_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (!registered_) registered_ = Shell_NotifyIconW(NIM_MODIFY, &data_) != FALSE;
    if (registered_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data_)) {
            Shell_NotifyIconW(NIM_DELETE, &data_);
            registered_ = false;
            return false;
        }
    }
    return registered_;
}

void TrayIcon::Shutdown() noexcept {
    if (data_.hWnd != nullptr) Shell_NotifyIconW(NIM_DELETE, &data_);
    if (data_.hIcon != nullptr) DestroyIcon(data_.hIcon);
    data_ = {};
    registered_ = false;
}

void TrayIcon::NotifyHidden() noexcept {
    if (notified_ || !registered_) return;
    notified_ = true;
    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = NIIF_NONE;
    wcscpy_s(data_.szInfoTitle, L"Panning Wallpaper");
    wcscpy_s(data_.szInfo,
        L"Panning Wallpaper is still running in the notification area.");
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

TrayIcon::Command TrayIcon::ShowMenu(bool running, bool canStart, POINT anchor) const {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return Command::None;
    const bool populated = AppendMenuW(menu, MF_STRING,
        static_cast<UINT>(Command::OpenSettings), L"Open Settings") &&
        AppendMenuW(menu, MF_STRING | (!running && !canStart ? MF_GRAYED : 0),
            static_cast<UINT>(Command::ToggleWallpaper),
            running ? L"Stop Wallpaper" : L"Start Wallpaper") &&
        AppendMenuW(menu, MF_STRING, static_cast<UINT>(Command::Exit), L"Exit");
    UINT selected = 0;
    if (populated) {
        SetForegroundWindow(data_.hWnd);
        selected = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
            anchor.x, anchor.y, data_.hWnd, nullptr);
        // Required by the shell so dismissing the menu works on subsequent openings.
        PostMessageW(data_.hWnd, WM_NULL, 0, 0);
    }
    DestroyMenu(menu);
    return static_cast<Command>(selected);
}

}  // namespace panning_wallpaper
