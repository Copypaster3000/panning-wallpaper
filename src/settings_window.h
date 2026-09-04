#pragma once

#include "preview_window.h"
#include "settings_model.h"
#include "settings_store.h"
#include "ui_palette.h"

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

    [[nodiscard]] bool Initialize(
        HINSTANCE instance, const SavedSettings& saved, std::wstring& error);
    [[nodiscard]] HWND Window() const noexcept;

    void SetWallpaperRunning(bool running);
    void ShowWallpaperFailure(std::wstring_view detail);
    void ShowStatusError(std::wstring_view detail);
    void ShowError(std::wstring_view message) const;

private:
    static LRESULT CALLBACK WindowProcedure(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(
        HWND window, UINT message, WPARAM wParam, LPARAM lParam);

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
    void PaintWindow();
    void PaintStyledControl(HWND control);
    void DrawSegmentControl(HWND control, HDC deviceContext, const RECT& bounds);
    void DrawSliderControl(HWND control, HDC deviceContext, const RECT& bounds);
    void DrawToggleControl(HWND control, HDC deviceContext, const RECT& bounds);
    void DrawThemeToggleControl(
        HWND control, HDC deviceContext, const RECT& bounds);
    void DrawSpinnerControl(HWND control, HDC deviceContext, const RECT& bounds);
    void DrawActionButton(HWND control, HDC deviceContext, const RECT& bounds);
    [[nodiscard]] RECT SliderThumbBounds(HWND control) const noexcept;
    void InvalidateSliderMovement(
        HWND control, const RECT& previousThumbBounds) const noexcept;
    void InvalidateStyledControls() const;
    [[nodiscard]] bool InstallControlStyling(HWND control) const;
    [[nodiscard]] bool IsPointerOver(HWND control) const noexcept;
    [[nodiscard]] bool ShouldDrawKeyboardFocus(HWND control) const noexcept;
    static LRESULT CALLBACK StyledControlProcedure(
        HWND control,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData);
    [[nodiscard]] bool ApplyUiTheme(UiTheme theme) noexcept;
    void UpdateTitleBarTheme() const noexcept;
    void SynchronizeControlsFromEditedState();
    void UpdateEditedConfigurationFromControls(int clickedControlId);
    void UpdateDurationFromEdit();
    void UpdateDirectionDependentControls();
    void UpdateFramingAvailability();
    void UpdateApplyAvailability();
    void ChooseImage();
    void ApplyEditedSettings();

    [[nodiscard]] int Scale(int logicalPixels) const noexcept;
    [[nodiscard]] int CheckedDirectionButton() const noexcept;
    [[nodiscard]] int CheckedFitButton() const noexcept;

    Application& application_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;

    HWND themeToggle_ = nullptr;
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
    HFONT labelFont_ = nullptr;
    HBRUSH windowBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBRUSH controlBrush_ = nullptr;
    HBRUSH invalidEditBrush_ = nullptr;
    RECT settingsPanelBounds_{};
    RECT actionsPanelBounds_{};
    RECT directionGroupBounds_{};
    RECT fitGroupBounds_{};
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    UiTheme theme_ = UiTheme::Light;
    UiPalette palette_ = kLightPalette;
    SettingsState state_;
    DecodedImage pendingFullImage_;
    bool durationValid_ = true;
    bool updatingControls_ = false;
    bool classRegistered_ = false;
};

}  // namespace panning_wallpaper
