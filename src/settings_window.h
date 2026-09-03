#pragma once

#include "preview_window.h"
#include "settings_model.h"

#include <windows.h>

#include <array>
#include <string>
#include <string_view>

namespace panning_wallpaper {

class Application;

class SettingsWindow final {
public:
    explicit SettingsWindow(Application& application) noexcept;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    [[nodiscard]] bool Initialize(HINSTANCE instance, std::wstring& error);
    [[nodiscard]] HWND Window() const noexcept;

    void SetWallpaperRunning(bool running);
    void ShowWallpaperFailure(std::wstring_view detail);

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool CreateControls(std::wstring& error);
    [[nodiscard]] HWND CreateControl(
        DWORD extendedStyle,
        const wchar_t* className,
        const wchar_t* text,
        DWORD style,
        int id);
    void LayoutControls(int clientWidth, int clientHeight);
    void UpdateFonts();
    void ApplyFont(HWND control, HFONT font) const;
    void SynchronizeControlsFromEditedState();
    void UpdateEditedConfigurationFromControls(int clickedControlId);
    void UpdateDurationFromEdit();
    void UpdatePositionLabels();
    void UpdateApplyAvailability();
    void ChooseImage();
    void ApplyEditedSettings();
    void ShowError(std::wstring_view message) const;

    [[nodiscard]] int Scale(int logicalPixels) const noexcept;
    [[nodiscard]] int CheckedDirectionButton() const noexcept;
    [[nodiscard]] int CheckedFitButton() const noexcept;

    Application& application_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;

    HWND titleLabel_ = nullptr;
    PreviewWindow preview_;
    HWND imageLabel_ = nullptr;
    HWND imagePathEdit_ = nullptr;
    HWND chooseButton_ = nullptr;
    HWND directionLabel_ = nullptr;
    std::array<HWND, 4> directionButtons_{};
    HWND durationLabel_ = nullptr;
    HWND durationSlider_ = nullptr;
    HWND durationEdit_ = nullptr;
    HWND durationSpinner_ = nullptr;
    HWND secondsLabel_ = nullptr;
    HWND fitLabel_ = nullptr;
    std::array<HWND, 2> fitButtons_{};
    HWND positionLabel_ = nullptr;
    HWND positionStartLabel_ = nullptr;
    HWND positionSlider_ = nullptr;
    HWND positionEndLabel_ = nullptr;
    HWND pauseCheckBox_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND applyButton_ = nullptr;

    HFONT uiFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HBRUSH invalidEditBrush_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    SettingsState state_;
    DecodedImage pendingFullImage_;
    bool durationValid_ = true;
    bool updatingControls_ = false;
    bool classRegistered_ = false;
};

}  // namespace panning_wallpaper
