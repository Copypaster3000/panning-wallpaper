#include "settings_window.h"

#include "application.h"
#include "preview_image.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <utility>
#include <wrl/client.h>

namespace panning_wallpaper {
namespace {

constexpr wchar_t kSettingsWindowClassName[] =
    L"PanningWallpaper.SettingsWindow";
constexpr wchar_t kSettingsWindowTitle[] = L"Panning Wallpaper";
constexpr wchar_t kPauseCoveredLabel[] = L"Pause when fully covered";
constexpr wchar_t kPauseCoveredNote[] =
    L"(Turn off for transparent windows)";
constexpr DWORD kSettingsWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

constexpr int kPreviewId = 101;
constexpr int kImageLabelId = 102;
constexpr int kImagePathId = 103;
constexpr int kChooseImageId = 104;
constexpr int kDirectionLabelId = 105;
constexpr int kDirectionLeftId = 106;
constexpr int kDirectionRightId = 107;
constexpr int kDirectionUpId = 108;
constexpr int kDirectionDownId = 109;
constexpr int kDurationLabelId = 110;
constexpr int kDurationSliderId = 111;
constexpr int kDurationEditId = 112;
constexpr int kDurationSpinnerId = 113;
constexpr int kSecondsLabelId = 114;
constexpr int kFitLabelId = 115;
constexpr int kFitPanId = 116;
constexpr int kFitCoverId = 117;
constexpr int kPositionLabelId = 118;
constexpr int kPositionStartId = 119;
constexpr int kPositionSliderId = 120;
constexpr int kPositionEndId = 121;
constexpr int kPauseCoveredId = 122;
constexpr int kStatusId = 123;
constexpr int kStopId = 124;
constexpr int kApplyId = 125;
constexpr int kThemeToggleId = 126;

constexpr int kPreviewMaximumWidth = 1024;
constexpr int kPreviewMaximumHeight = 576;

constexpr UINT_PTR kControlSubclassId = 1;

[[nodiscard]] constexpr COLORREF ToColorRef(std::uint32_t color) noexcept {
    const auto red = static_cast<COLORREF>((color >> 16U) & 0xFFU);
    const auto green = static_cast<COLORREF>((color >> 8U) & 0xFFU);
    const auto blue = static_cast<COLORREF>(color & 0xFFU);
    return red | (green << 8U) | (blue << 16U);
}

void DrawRoundedBox(
    HDC deviceContext,
    const RECT& bounds,
    COLORREF fillColor,
    COLORREF borderColor,
    int radius) {
    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCBrushColor(deviceContext, fillColor);
    SetDCPenColor(deviceContext, borderColor);
    RoundRect(
        deviceContext,
        bounds.left,
        bounds.top,
        bounds.right,
        bounds.bottom,
        radius,
        radius);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);
}

void DrawRoundedOutline(
    HDC deviceContext,
    const RECT& bounds,
    COLORREF color,
    int radius) {
    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(NULL_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCPenColor(deviceContext, color);
    RoundRect(
        deviceContext,
        bounds.left,
        bounds.top,
        bounds.right,
        bounds.bottom,
        radius,
        radius);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);
}

void DrawEllipticalOutline(
    HDC deviceContext, const RECT& bounds, COLORREF color) {
    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(NULL_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCPenColor(deviceContext, color);
    Ellipse(
        deviceContext,
        bounds.left,
        bounds.top,
        bounds.right,
        bounds.bottom);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);
}

void DrawCenteredText(
    HWND control,
    HDC deviceContext,
    RECT bounds,
    COLORREF color,
    std::wstring_view overrideText = {}) {
    wchar_t text[128]{};
    std::wstring_view displayText = overrideText;
    if (displayText.empty()) {
        const int length = GetWindowTextW(
            control, text, static_cast<int>(std::size(text)));
        displayText = std::wstring_view(
            text, static_cast<size_t>(std::max(length, 0)));
    }

    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(control, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font != nullptr
        ? SelectObject(deviceContext, font)
        : nullptr;
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, color);
    DrawTextW(
        deviceContext,
        displayText.data(),
        static_cast<int>(displayText.size()),
        &bounds,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont != nullptr) {
        SelectObject(deviceContext, previousFont);
    }
}

[[nodiscard]] int DirectionButtonId(PanDirection direction) noexcept {
    switch (direction) {
    case PanDirection::Left:
        return kDirectionLeftId;
    case PanDirection::Right:
        return kDirectionRightId;
    case PanDirection::Up:
        return kDirectionUpId;
    case PanDirection::Down:
        return kDirectionDownId;
    }
    return kDirectionLeftId;
}

[[nodiscard]] PanDirection DirectionFromButtonId(int id) noexcept {
    switch (id) {
    case kDirectionRightId:
        return PanDirection::Right;
    case kDirectionUpId:
        return PanDirection::Up;
    case kDirectionDownId:
        return PanDirection::Down;
    default:
        return PanDirection::Left;
    }
}

[[nodiscard]] FitMode FitFromButtonId(int id) noexcept {
    return id == kFitCoverId ? FitMode::Cover : FitMode::Pan;
}

}  // namespace

SettingsWindow::SettingsWindow(Application& application) noexcept
    : application_(application) {}

SettingsWindow::~SettingsWindow() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kSettingsWindowClassName, instance_);
    }
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    if (labelFont_ != nullptr) {
        DeleteObject(labelFont_);
    }
    if (windowBrush_ != nullptr) {
        DeleteObject(windowBrush_);
    }
    if (panelBrush_ != nullptr) {
        DeleteObject(panelBrush_);
    }
    if (controlBrush_ != nullptr) {
        DeleteObject(controlBrush_);
    }
    if (invalidEditBrush_ != nullptr) {
        DeleteObject(invalidEditBrush_);
    }
}

bool SettingsWindow::Initialize(
    HINSTANCE instance, const SavedSettings& saved, std::wstring& error) {
    instance_ = instance;

    if (saved.applied) {
        state_.Edited() = *saved.applied;
        state_.MarkApplied();
    }
    if (!ApplyUiTheme(saved.theme)) {
        error = L"The settings window drawing resources could not be created.";
        return false;
    }

    INITCOMMONCONTROLSEX commonControls{
        .dwSize = sizeof(commonControls),
        .dwICC = ICC_BAR_CLASSES | ICC_UPDOWN_CLASS,
    };
    if (!InitCommonControlsEx(&commonControls)) {
        error = L"Windows common controls could not be initialized.";
        return false;
    }

    WNDCLASSEXW windowClass{
        .cbSize = sizeof(windowClass),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WindowProcedure,
        .hInstance = instance_,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = windowBrush_,
        .lpszClassName = kSettingsWindowClassName,
    };
    if (RegisterClassExW(&windowClass) == 0) {
        error = std::format(
            L"The settings window class could not be registered (error {}).",
            GetLastError());
        return false;
    }
    classRegistered_ = true;

    dpi_ = GetDpiForSystem();
    RECT desiredClient{0, 0, Scale(860), Scale(680)};
    if (!AdjustWindowRectExForDpi(
            &desiredClient,
            kSettingsWindowStyle,
            FALSE,
            WS_EX_CONTROLPARENT,
            dpi_)) {
        error = L"The settings window dimensions could not be calculated.";
        return false;
    }

    int windowWidth = desiredClient.right - desiredClient.left;
    int windowHeight = desiredClient.bottom - desiredClient.top;
    RECT workArea{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        windowWidth = std::min(
            windowWidth, static_cast<int>(workArea.right - workArea.left));
        windowHeight = std::min(
            windowHeight, static_cast<int>(workArea.bottom - workArea.top));
    }
    window_ = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        kSettingsWindowClassName,
        kSettingsWindowTitle,
        kSettingsWindowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        error = std::format(
            L"The settings window could not be created (error {}).",
            GetLastError());
        return false;
    }

    dpi_ = GetDpiForWindow(window_);
    if (!CreateControls(error)) {
        return false;
    }

    UpdateTitleBarTheme();
    UpdateFonts();
    SynchronizeControlsFromEditedState();
    RECT client{};
    GetClientRect(window_, &client);
    LayoutControls(client.right, client.bottom);
    if (saved.applied) {
        SetWindowTextW(imagePathEdit_, state_.Edited().imagePath.c_str());
        DecodedImage fullImage;
        DecodedImage previewImage;
        std::wstring previewError;
        if (DecodeImageFile(state_.Edited().imagePath, fullImage, previewError) &&
            CreateBoundedPreviewImage(fullImage, kPreviewMaximumWidth,
                kPreviewMaximumHeight, previewImage, previewError)) {
            preview_.SetImage(std::move(previewImage));
            preview_.SetConfiguration(state_.Edited().configuration);
            UpdateFramingAvailability();
        } else {
            ShowStatusError(L"Saved image unavailable. Choose another image.");
            OutputDebugStringW(previewError.c_str());
        }
    }
    return true;
}

HWND SettingsWindow::Window() const noexcept {
    return window_;
}

void SettingsWindow::SetWallpaperRunning(bool running) {
    if (running) {
        SetWindowTextW(statusLabel_, L"\u25CF  Wallpaper is running");
    } else {
        SetWindowTextW(statusLabel_, L"\u25CF  Wallpaper is stopped");
    }
    EnableWindow(stopButton_, running ? TRUE : FALSE);
}

void SettingsWindow::ShowWallpaperFailure(std::wstring_view detail) {
    SetWallpaperRunning(false);
    std::wstring message = L"The wallpaper stopped because of an unexpected error.";
    if (!detail.empty()) {
        message += L"\n\n";
        message.append(detail);
    }
    application_.OpenSettings();
    ShowError(message);
}

void SettingsWindow::ShowStatusError(std::wstring_view detail) {
    const std::wstring text(detail);
    SetWindowTextW(statusLabel_, text.c_str());
}

LRESULT CALLBACK SettingsWindow::WindowProcedure(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->HandleMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (application_.HandleSettingsMessage(message, wParam, lParam)) return 0;
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintWindow();
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kThemeToggleId && notification == BN_CLICKED) {
            const UiTheme requestedTheme =
                Button_GetCheck(themeToggle_) == BST_CHECKED
                ? UiTheme::Dark
                : UiTheme::Light;
            if (!ApplyUiTheme(requestedTheme)) {
                Button_SetCheck(
                    themeToggle_,
                    theme_ == UiTheme::Dark ? BST_CHECKED : BST_UNCHECKED);
                MessageBeep(MB_ICONWARNING);
            } else {
                application_.SaveTheme(theme_);
            }
            return 0;
        }
        if (id == kChooseImageId && notification == BN_CLICKED) {
            ChooseImage();
            return 0;
        }
        if (id == kApplyId && notification == BN_CLICKED) {
            ApplyEditedSettings();
            return 0;
        }
        if (id == kStopId && notification == BN_CLICKED) {
            application_.StopWallpaper();
            return 0;
        }
        if (id == IDCANCEL) {
            SendMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
        }
        if (id == kDurationEditId && notification == EN_CHANGE) {
            UpdateDurationFromEdit();
            return 0;
        }
        if ((id >= kDirectionLeftId && id <= kDirectionDownId) ||
            id == kFitPanId || id == kFitCoverId || id == kPauseCoveredId) {
            if (notification == BN_CLICKED) {
                UpdateEditedConfigurationFromControls(id);
            }
            return 0;
        }
        break;
    }
    case WM_HSCROLL: {
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (control == durationSlider_ || control == positionSlider_) {
            UpdateEditedSliderFromControl(control);
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lParam) == durationEdit_ && !durationValid_) {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetBkColor(
                deviceContext, ToColorRef(palette_.errorBackground));
            SetTextColor(deviceContext, ToColorRef(palette_.errorText));
            return reinterpret_cast<LRESULT>(invalidEditBrush_);
        }
        SetBkColor(
            reinterpret_cast<HDC>(wParam),
            ToColorRef(palette_.controlSurface));
        SetTextColor(
            reinterpret_cast<HDC>(wParam),
            ToColorRef(palette_.primaryText));
        return reinterpret_cast<LRESULT>(controlBrush_);
    case WM_CTLCOLORSTATIC: {
        const HWND control = reinterpret_cast<HWND>(lParam);
        const int id = GetDlgCtrlID(control);
        const HDC deviceContext = reinterpret_cast<HDC>(wParam);
        if (control == imagePathEdit_) {
            SetBkColor(
                deviceContext, ToColorRef(palette_.controlSurface));
            SetTextColor(
                deviceContext, ToColorRef(palette_.secondaryText));
            return reinterpret_cast<LRESULT>(controlBrush_);
        }
        SetBkColor(deviceContext, ToColorRef(palette_.panelBackground));
        const bool framingText = id == kPositionLabelId ||
            id == kPositionStartId || id == kPositionEndId;
        const bool framingDisabled = framingText &&
            IsWindowEnabled(positionSlider_) == FALSE;
        const bool secondary = id == kPositionStartId || id == kPositionEndId ||
            id == kSecondsLabelId || id == kStatusId || id == kImagePathId;
        SetTextColor(
            deviceContext,
            ToColorRef(
                framingDisabled || !IsWindowEnabled(control)
                    ? palette_.disabledText
                    : (secondary ? palette_.secondaryText
                                 : palette_.primaryText)));
        return reinterpret_cast<LRESULT>(panelBrush_);
    }
    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        UpdateFonts();
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(
            window_,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT client{0, 0, Scale(700), Scale(570)};
        AdjustWindowRectExForDpi(
            &client,
            kSettingsWindowStyle,
            FALSE,
            WS_EX_CONTROLPARENT,
            dpi_);
        limits->ptMinTrackSize.x = client.right - client.left;
        limits->ptMinTrackSize.y = client.bottom - client.top;
        return 0;
    }
    case WM_CLOSE:
        application_.HideSettings();
        return 0;
    case WM_DESTROY:
        application_.SettingsWindowClosed();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        window_ = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool SettingsWindow::CreateControls(std::wstring& error) {
    if (!preview_.Initialize(instance_, window_, kPreviewId, error)) {
        return false;
    }
    imageLabel_ = CreateControl(
        0, WC_STATICW, L"Wallpaper", WS_VISIBLE | SS_CENTERIMAGE, kImageLabelId);
    imagePathEdit_ = CreateControl(
        WS_EX_CLIENTEDGE,
        WC_EDITW,
        L"No image selected",
        WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
        kImagePathId);
    chooseButton_ = CreateControl(
        0,
        WC_BUTTONW,
        L"Choose Image...",
        WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kChooseImageId);
    themeToggle_ = CreateControl(
        0,
        WC_BUTTONW,
        L"Light / Dark appearance",
        WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTOCHECKBOX,
        kThemeToggleId);
    directionLabel_ = CreateControl(
        0, WC_STATICW, L"Direction", WS_VISIBLE | SS_CENTERIMAGE, kDirectionLabelId);
    directionButtons_[0] = CreateControl(
        0,
        WC_BUTTONW,
        L"Left",
        WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        kDirectionLeftId);
    directionButtons_[1] = CreateControl(
        0, WC_BUTTONW, L"Right", WS_VISIBLE | BS_AUTORADIOBUTTON, kDirectionRightId);
    directionButtons_[2] = CreateControl(
        0, WC_BUTTONW, L"Up", WS_VISIBLE | BS_AUTORADIOBUTTON, kDirectionUpId);
    directionButtons_[3] = CreateControl(
        0, WC_BUTTONW, L"Down", WS_VISIBLE | BS_AUTORADIOBUTTON, kDirectionDownId);
    durationLabel_ = CreateControl(
        0, WC_STATICW, L"Loop duration", WS_VISIBLE | SS_CENTERIMAGE, kDurationLabelId);
    durationSlider_ = CreateControl(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS,
        kDurationSliderId);
    durationEdit_ = CreateControl(
        WS_EX_CLIENTEDGE,
        WC_EDITW,
        L"90",
        WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
        kDurationEditId);
    durationSpinner_ = CreateControl(
        0,
        UPDOWN_CLASSW,
        L"",
        WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS |
            UDS_SETBUDDYINT,
        kDurationSpinnerId);
    secondsLabel_ = CreateControl(
        0, WC_STATICW, L"seconds", WS_VISIBLE | SS_CENTERIMAGE, kSecondsLabelId);
    fitLabel_ = CreateControl(
        0,
        WC_STATICW,
        L"Image sizing",
        WS_VISIBLE | SS_CENTERIMAGE,
        kFitLabelId);
    fitButtons_[0] = CreateControl(
        0,
        WC_BUTTONW,
        L"Fit height",
        WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        kFitPanId);
    fitButtons_[1] = CreateControl(
        0,
        WC_BUTTONW,
        L"Fill screen",
        WS_VISIBLE | BS_AUTORADIOBUTTON,
        kFitCoverId);
    positionLabel_ = CreateControl(
        0,
        WC_STATICW,
        L"Vertical framing",
        WS_VISIBLE | SS_CENTERIMAGE,
        kPositionLabelId);
    positionStartLabel_ = CreateControl(
        0,
        WC_STATICW,
        L"Top",
        WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        kPositionStartId);
    positionSlider_ = CreateControl(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS,
        kPositionSliderId);
    positionEndLabel_ = CreateControl(
        0, WC_STATICW, L"Bottom", WS_VISIBLE | SS_CENTERIMAGE, kPositionEndId);
    pauseCheckBox_ = CreateControl(
        0,
        WC_BUTTONW,
        L"Pause when fully covered (Turn off for transparent windows)",
        WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        kPauseCoveredId);
    statusLabel_ = CreateControl(
        0,
        WC_STATICW,
        L"\u25CF  Wallpaper is stopped",
        WS_VISIBLE | SS_CENTERIMAGE,
        kStatusId);
    stopButton_ = CreateControl(
        0,
        WC_BUTTONW,
        L"Stop Wallpaper",
        WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kStopId);
    applyButton_ = CreateControl(
        0,
        WC_BUTTONW,
        L"Apply Wallpaper",
        WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        kApplyId);

    const std::array requiredControls{
        themeToggle_, imageLabel_, imagePathEdit_, chooseButton_,
        directionLabel_, directionButtons_[0], directionButtons_[1],
        directionButtons_[2], directionButtons_[3], durationLabel_,
        durationSlider_, durationEdit_, durationSpinner_, secondsLabel_,
        fitLabel_, fitButtons_[0], fitButtons_[1], positionLabel_,
        positionStartLabel_, positionSlider_, positionEndLabel_, pauseCheckBox_,
        statusLabel_, stopButton_, applyButton_};
    if (std::ranges::any_of(requiredControls, [](HWND control) {
            return control == nullptr;
        })) {
        error = std::format(
            L"A settings control could not be created (error {}).",
            GetLastError());
        return false;
    }

    SendMessageW(
        durationSlider_,
        TBM_SETRANGE,
        TRUE,
        MAKELPARAM(kMinimumGuiDurationSeconds, kMaximumGuiDurationSeconds));
    SendMessageW(durationSlider_, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(durationSlider_, TBM_SETTICFREQ, 60, 0);
    SendMessageW(
        durationSpinner_,
        UDM_SETBUDDY,
        reinterpret_cast<WPARAM>(durationEdit_),
        0);
    SendMessageW(
        durationSpinner_,
        UDM_SETRANGE32,
        kMinimumGuiDurationSeconds,
        kMaximumGuiDurationSeconds);
    SendMessageW(
        positionSlider_,
        TBM_SETRANGE,
        TRUE,
        MAKELPARAM(0, kMaximumPositionSliderValue));
    SendMessageW(positionSlider_, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(positionSlider_, TBM_SETTICFREQ, 10, 0);
    const std::array styledControls{
        themeToggle_, chooseButton_, directionButtons_[0], directionButtons_[1],
        directionButtons_[2], directionButtons_[3], durationSlider_,
        durationSpinner_, fitButtons_[0], fitButtons_[1], positionSlider_,
        pauseCheckBox_, stopButton_, applyButton_};
    if (std::ranges::any_of(styledControls, [this](HWND control) {
            return !InstallControlStyling(control);
        })) {
        error = L"A settings control could not be styled.";
        return false;
    }
    EnableWindow(stopButton_, FALSE);
    return true;
}

HWND SettingsWindow::CreateControl(
    DWORD extendedStyle,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int id) {
    return CreateWindowExW(
        extendedStyle,
        className,
        text,
        WS_CHILD | style,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_,
        nullptr);
}

void SettingsWindow::LayoutControls(int clientWidth, int clientHeight) {
    if (imageLabel_ == nullptr) {
        return;
    }

    const int margin = Scale(16);
    const int sectionGap = Scale(10);
    const int settingsHeight = Scale(238);
    const int actionsHeight = Scale(50);
    const int previewHeight = std::max(
        Scale(150),
        clientHeight - 2 * margin - 2 * sectionGap - settingsHeight -
            actionsHeight);
    int y = margin;
    MoveWindow(
        preview_.Window(),
        margin,
        y,
        clientWidth - 2 * margin,
        previewHeight,
        TRUE);
    y += previewHeight + sectionGap;

    settingsPanelBounds_ = RECT{
        margin, y, clientWidth - margin, y + settingsHeight};
    const int panelPadding = Scale(12);
    const int innerX = settingsPanelBounds_.left + panelPadding;
    const int innerRight = settingsPanelBounds_.right - panelPadding;
    const int labelWidth = Scale(140);
    const int rowHeight = Scale(32);
    const int rowGap = Scale(5);
    const int contentX = innerX + labelWidth;
    const int contentWidth = std::max(Scale(280), innerRight - contentX);

    y = settingsPanelBounds_.top + Scale(10);
    MoveWindow(imageLabel_, innerX, y, labelWidth, rowHeight, TRUE);
    const int chooseWidth = Scale(140);
    const int themeToggleWidth = Scale(142);
    const int controlGap = Scale(8);
    const int imagePathWidth = std::max(
        Scale(120),
        contentWidth - chooseWidth - themeToggleWidth - 2 * controlGap);
    MoveWindow(
        imagePathEdit_,
        contentX,
        y,
        imagePathWidth,
        rowHeight,
        TRUE);
    MoveWindow(
        chooseButton_,
        contentX + imagePathWidth + controlGap,
        y,
        chooseWidth,
        rowHeight,
        TRUE);
    MoveWindow(
        themeToggle_,
        contentX + contentWidth - themeToggleWidth,
        y + Scale(3),
        themeToggleWidth,
        Scale(26),
        TRUE);
    y += rowHeight + rowGap;

    MoveWindow(directionLabel_, innerX, y, labelWidth, rowHeight, TRUE);
    const int directionGroupWidth = std::min(contentWidth, Scale(452));
    directionGroupBounds_ = RECT{
        contentX,
        y,
        contentX + directionGroupWidth,
        y + rowHeight};
    const int directionInnerWidth = directionGroupWidth - 2 * Scale(1);
    for (size_t index = 0; index < directionButtons_.size(); ++index) {
        const int left = directionGroupBounds_.left + Scale(1) +
            static_cast<int>(index) * directionInnerWidth /
                static_cast<int>(directionButtons_.size());
        const int right = directionGroupBounds_.left + Scale(1) +
            static_cast<int>(index + 1) * directionInnerWidth /
                static_cast<int>(directionButtons_.size());
        MoveWindow(
            directionButtons_[index],
            left,
            y + Scale(1),
            right - left,
            rowHeight - 2 * Scale(1),
            TRUE);
    }
    y += rowHeight + rowGap;

    MoveWindow(durationLabel_, innerX, y, labelWidth, rowHeight, TRUE);
    const int durationEditWidth = Scale(82);
    const int secondsWidth = Scale(54);
    const int durationSliderWidth = std::max(
        Scale(100), contentWidth - durationEditWidth - secondsWidth - Scale(18));
    MoveWindow(durationSlider_, contentX, y, durationSliderWidth, rowHeight, TRUE);
    MoveWindow(
        durationEdit_,
        contentX + durationSliderWidth + Scale(10),
        y,
        durationEditWidth,
        rowHeight,
        TRUE);
    MoveWindow(
        secondsLabel_,
        contentX + durationSliderWidth + durationEditWidth + Scale(14),
        y,
        secondsWidth,
        rowHeight,
        TRUE);
    y += rowHeight + rowGap;

    MoveWindow(fitLabel_, innerX, y, labelWidth, rowHeight, TRUE);
    const int fitGroupWidth = std::min(contentWidth, Scale(232));
    fitGroupBounds_ = RECT{
        contentX, y, contentX + fitGroupWidth, y + rowHeight};
    const int fitInnerWidth = fitGroupWidth - 2 * Scale(1);
    MoveWindow(
        fitButtons_[0],
        contentX + Scale(1),
        y + Scale(1),
        fitInnerWidth / 2,
        rowHeight - 2 * Scale(1),
        TRUE);
    MoveWindow(
        fitButtons_[1],
        contentX + Scale(1) + fitInnerWidth / 2,
        y + Scale(1),
        fitInnerWidth - fitInnerWidth / 2,
        rowHeight - 2 * Scale(1),
        TRUE);
    y += rowHeight + rowGap;

    MoveWindow(positionLabel_, innerX, y, labelWidth, rowHeight, TRUE);
    const int startWidth = Scale(44);
    const int endWidth = Scale(56);
    const int positionSliderWidth =
        std::max(Scale(100), contentWidth - startWidth - endWidth - Scale(16));
    MoveWindow(positionStartLabel_, contentX, y, startWidth, rowHeight, TRUE);
    MoveWindow(
        positionSlider_,
        contentX + startWidth + Scale(8),
        y,
        positionSliderWidth,
        rowHeight,
        TRUE);
    MoveWindow(
        positionEndLabel_,
        contentX + startWidth + Scale(16) + positionSliderWidth,
        y,
        endWidth,
        rowHeight,
        TRUE);
    y += rowHeight + Scale(4);

    MoveWindow(
        pauseCheckBox_, innerX, y, innerRight - innerX, Scale(26), TRUE);

    actionsPanelBounds_ = RECT{
        margin,
        settingsPanelBounds_.bottom + sectionGap,
        clientWidth - margin,
        settingsPanelBounds_.bottom + sectionGap + actionsHeight};
    const int buttonHeight = Scale(36);
    const int stopButtonWidth = Scale(152);
    const int applyButtonWidth = Scale(174);
    const int actionPadding = Scale(7);
    const int buttonY = actionsPanelBounds_.top + actionPadding;
    MoveWindow(
        statusLabel_,
        actionsPanelBounds_.left + Scale(14),
        buttonY,
        std::max(
            Scale(80),
            static_cast<int>(
                actionsPanelBounds_.right - actionsPanelBounds_.left) -
                stopButtonWidth - applyButtonWidth - Scale(50)),
        buttonHeight,
        TRUE);
    MoveWindow(
        stopButton_,
        actionsPanelBounds_.right - actionPadding - applyButtonWidth -
            Scale(8) - stopButtonWidth,
        buttonY,
        stopButtonWidth,
        buttonHeight,
        TRUE);
    MoveWindow(
        applyButton_,
        actionsPanelBounds_.right - actionPadding - applyButtonWidth,
        buttonY,
        applyButtonWidth,
        buttonHeight,
        TRUE);
    UpdateFramingAvailability();
    InvalidateRect(window_, nullptr, FALSE);
}

void SettingsWindow::UpdateFonts() {
    NONCLIENTMETRICSW metrics{.cbSize = sizeof(metrics)};
    if (!SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS,
            sizeof(metrics),
            &metrics,
            0,
            dpi_)) {
        metrics.lfMessageFont.lfHeight = -MulDiv(9, dpi_, 72);
        wcscpy_s(metrics.lfMessageFont.lfFaceName, L"Segoe UI");
    }

    HFONT newUiFont = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW labelDescription = metrics.lfMessageFont;
    labelDescription.lfWeight = FW_SEMIBOLD;
    HFONT newLabelFont = CreateFontIndirectW(&labelDescription);
    if (newUiFont == nullptr || newLabelFont == nullptr) {
        if (newUiFont != nullptr) {
            DeleteObject(newUiFont);
        }
        if (newLabelFont != nullptr) {
            DeleteObject(newLabelFont);
        }
        return;
    }

    const std::array controls{
        themeToggle_, imageLabel_, imagePathEdit_, chooseButton_, directionLabel_,
        directionButtons_[0], directionButtons_[1], directionButtons_[2],
        directionButtons_[3], durationLabel_, durationSlider_, durationEdit_,
        durationSpinner_, secondsLabel_, fitLabel_, fitButtons_[0], fitButtons_[1],
        positionLabel_, positionStartLabel_, positionSlider_, positionEndLabel_,
        pauseCheckBox_, statusLabel_, stopButton_, applyButton_};
    for (HWND control : controls) {
        ApplyFont(control, newUiFont);
    }
    const std::array labelControls{
        imageLabel_, directionLabel_, durationLabel_, fitLabel_, positionLabel_};
    for (HWND control : labelControls) {
        ApplyFont(control, newLabelFont);
    }

    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    if (labelFont_ != nullptr) {
        DeleteObject(labelFont_);
    }
    uiFont_ = newUiFont;
    labelFont_ = newLabelFont;
    SendMessageW(
        imagePathEdit_,
        EM_SETMARGINS,
        EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(Scale(8), Scale(6)));
    InvalidateStyledControls();
}

void SettingsWindow::ApplyFont(HWND control, HFONT font) const {
    if (control != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

bool SettingsWindow::ApplyUiTheme(UiTheme theme) noexcept {
    const UiPalette nextPalette = PaletteForTheme(theme);
    HBRUSH nextWindowBrush = CreateSolidBrush(
        ToColorRef(nextPalette.windowBackground));
    HBRUSH nextPanelBrush = CreateSolidBrush(
        ToColorRef(nextPalette.panelBackground));
    HBRUSH nextControlBrush = CreateSolidBrush(
        ToColorRef(nextPalette.controlSurface));
    HBRUSH nextInvalidEditBrush = CreateSolidBrush(
        ToColorRef(nextPalette.errorBackground));
    if (nextWindowBrush == nullptr || nextPanelBrush == nullptr ||
        nextControlBrush == nullptr || nextInvalidEditBrush == nullptr) {
        DeleteObject(nextWindowBrush);
        DeleteObject(nextPanelBrush);
        DeleteObject(nextControlBrush);
        DeleteObject(nextInvalidEditBrush);
        return false;
    }

    HBRUSH previousWindowBrush = std::exchange(windowBrush_, nextWindowBrush);
    HBRUSH previousPanelBrush = std::exchange(panelBrush_, nextPanelBrush);
    HBRUSH previousControlBrush = std::exchange(controlBrush_, nextControlBrush);
    HBRUSH previousInvalidEditBrush =
        std::exchange(invalidEditBrush_, nextInvalidEditBrush);
    theme_ = theme;
    palette_ = nextPalette;

    if (window_ != nullptr) {
        SetClassLongPtrW(
            window_,
            GCLP_HBRBACKGROUND,
            reinterpret_cast<LONG_PTR>(windowBrush_));
    }
    preview_.SetPalette(palette_);
    UpdateTitleBarTheme();
    if (themeToggle_ != nullptr) {
        Button_SetCheck(
            themeToggle_,
            theme_ == UiTheme::Dark ? BST_CHECKED : BST_UNCHECKED);
    }

    if (window_ != nullptr) {
        RedrawWindow(
            window_,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
        InvalidateStyledControls();
    }

    DeleteObject(previousWindowBrush);
    DeleteObject(previousPanelBrush);
    DeleteObject(previousControlBrush);
    DeleteObject(previousInvalidEditBrush);
    return true;
}

void SettingsWindow::UpdateTitleBarTheme() const noexcept {
    if (window_ == nullptr) {
        return;
    }
    const BOOL useDarkTitleBar = theme_ == UiTheme::Dark ? TRUE : FALSE;
    DwmSetWindowAttribute(
        window_,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &useDarkTitleBar,
        sizeof(useDarkTitleBar));
}

void SettingsWindow::PaintWindow() {
    PAINTSTRUCT paint{};
    const HDC deviceContext = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(deviceContext, &client, windowBrush_);

    const int cardRadius = Scale(9);
    DrawRoundedBox(
        deviceContext,
        settingsPanelBounds_,
        ToColorRef(palette_.panelBackground),
        ToColorRef(palette_.border),
        cardRadius);
    DrawRoundedBox(
        deviceContext,
        actionsPanelBounds_,
        ToColorRef(palette_.panelBackground),
        ToColorRef(palette_.border),
        cardRadius);
    DrawRoundedBox(
        deviceContext,
        directionGroupBounds_,
        ToColorRef(palette_.controlSurface),
        ToColorRef(palette_.border),
        Scale(6));
    DrawRoundedBox(
        deviceContext,
        fitGroupBounds_,
        ToColorRef(palette_.controlSurface),
        ToColorRef(palette_.border),
        Scale(6));
    EndPaint(window_, &paint);
}

void SettingsWindow::PaintStyledControl(HWND control) {
    PAINTSTRUCT paint{};
    const HDC targetContext = BeginPaint(control, &paint);
    RECT bounds{};
    GetClientRect(control, &bounds);

    HDC drawingContext = CreateCompatibleDC(targetContext);
    HBITMAP buffer = drawingContext != nullptr
        ? CreateCompatibleBitmap(
              targetContext, bounds.right - bounds.left, bounds.bottom - bounds.top)
        : nullptr;
    const HGDIOBJ previousBitmap = buffer != nullptr
        ? SelectObject(drawingContext, buffer)
        : nullptr;
    if (buffer == nullptr) {
        if (drawingContext != nullptr) {
            DeleteDC(drawingContext);
        }
        drawingContext = targetContext;
    }

    const int id = GetDlgCtrlID(control);
    if (id == kThemeToggleId) {
        DrawThemeToggleControl(control, drawingContext, bounds);
    } else if ((id >= kDirectionLeftId && id <= kDirectionDownId) ||
        id == kFitPanId || id == kFitCoverId) {
        DrawSegmentControl(control, drawingContext, bounds);
    } else if (id == kDurationSliderId || id == kPositionSliderId) {
        DrawSliderControl(control, drawingContext, bounds);
    } else if (id == kDurationSpinnerId) {
        DrawSpinnerControl(control, drawingContext, bounds);
    } else if (id == kPauseCoveredId) {
        DrawToggleControl(control, drawingContext, bounds);
    } else {
        DrawActionButton(control, drawingContext, bounds);
    }

    if (buffer != nullptr) {
        BitBlt(
            targetContext,
            0,
            0,
            bounds.right,
            bounds.bottom,
            drawingContext,
            0,
            0,
            SRCCOPY);
        SelectObject(drawingContext, previousBitmap);
        DeleteObject(buffer);
        DeleteDC(drawingContext);
    }
    EndPaint(control, &paint);
}

void SettingsWindow::DrawSegmentControl(
    HWND control, HDC deviceContext, const RECT& bounds) {
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool selected = Button_GetCheck(control) == BST_CHECKED;
    const bool hovered = IsPointerOver(control);
    const bool pressed =
        (Button_GetState(control) & BST_PUSHED) == BST_PUSHED;

    COLORREF fillColor = ToColorRef(palette_.controlSurface);
    COLORREF textColor = ToColorRef(
        enabled ? palette_.primaryText : palette_.disabledText);
    if (selected) {
        fillColor = pressed
            ? ToColorRef(palette_.selectedPressed)
            : ToColorRef(
                  hovered ? palette_.selectedHover : palette_.selectedSurface);
        textColor = ToColorRef(
            enabled ? palette_.selectedText : palette_.disabledText);
    } else if (hovered && enabled) {
        fillColor = ToColorRef(palette_.controlHover);
    }
    RECT fillBounds = bounds;
    SetDCBrushColor(deviceContext, fillColor);
    FillRect(
        deviceContext,
        &fillBounds,
        reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const int id = GetDlgCtrlID(control);
    if (id != kDirectionLeftId && id != kFitPanId) {
        const HGDIOBJ previousPen = SelectObject(
            deviceContext, GetStockObject(DC_PEN));
        SetDCPenColor(deviceContext, ToColorRef(palette_.border));
        MoveToEx(deviceContext, bounds.left, bounds.top + Scale(5), nullptr);
        LineTo(deviceContext, bounds.left, bounds.bottom - Scale(5));
        SelectObject(deviceContext, previousPen);
    }

    std::wstring_view displayText;
    switch (id) {
    case kDirectionLeftId:
        displayText = L"\u2190  Left";
        break;
    case kDirectionRightId:
        displayText = L"\u2192  Right";
        break;
    case kDirectionUpId:
        displayText = L"\u2191  Up";
        break;
    case kDirectionDownId:
        displayText = L"\u2193  Down";
        break;
    default:
        break;
    }
    DrawCenteredText(control, deviceContext, fillBounds, textColor, displayText);

    if (ShouldDrawKeyboardFocus(control)) {
        RECT focus = bounds;
        InflateRect(&focus, -Scale(3), -Scale(3));
        DrawRoundedOutline(
            deviceContext,
            focus,
            ToColorRef(
                selected ? palette_.selectedText : palette_.secondaryText),
            Scale(4));
    }
}

void SettingsWindow::DrawSliderControl(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, panelBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool hovered = IsPointerOver(control);
    const RECT trackBounds = SliderTrackBounds(control);
    const int trackLeft = trackBounds.left;
    const int trackRight = trackBounds.right;
    const int trackY = (bounds.top + bounds.bottom) / 2;

    RECT track{trackLeft, trackY - Scale(1), trackRight, trackY + Scale(2)};
    const COLORREF trackColor = ToColorRef(
        enabled ? palette_.sliderTrack : palette_.disabledText);
    SetDCBrushColor(deviceContext, trackColor);
    FillRect(
        deviceContext,
        &track,
        reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCBrushColor(
        deviceContext,
        ToColorRef(enabled ? palette_.sliderThumb : palette_.disabledSurface));
    SetDCPenColor(
        deviceContext,
        ToColorRef(
            !enabled ? palette_.disabledText
                     : (hovered ? palette_.selectedSurface : palette_.border)));
    const RECT thumbBounds = SliderThumbBounds(control);
    Ellipse(
        deviceContext,
        thumbBounds.left,
        thumbBounds.top,
        thumbBounds.right,
        thumbBounds.bottom);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);

    if (ShouldDrawKeyboardFocus(control)) {
        RECT focus = thumbBounds;
        InflateRect(&focus, Scale(3), Scale(3));
        DrawEllipticalOutline(
            deviceContext, focus, ToColorRef(palette_.secondaryText));
    }
}

void SettingsWindow::DrawToggleControl(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, panelBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool checked = Button_GetCheck(control) == BST_CHECKED;
    const bool hovered = IsPointerOver(control);
    const int toggleWidth = Scale(36);
    const int toggleHeight = Scale(20);
    RECT toggle{
        bounds.left,
        (bounds.top + bounds.bottom - toggleHeight) / 2,
        bounds.left + toggleWidth,
        (bounds.top + bounds.bottom + toggleHeight) / 2};
    const COLORREF fillColor = !enabled
        ? ToColorRef(palette_.disabledSurface)
        : (checked
               ? ToColorRef(
                     hovered ? palette_.selectedHover : palette_.selectedSurface)
               : ToColorRef(
                     hovered ? palette_.controlHover : palette_.toggleOff));
    DrawRoundedBox(
        deviceContext,
        toggle,
        fillColor,
        ToColorRef(palette_.border),
        toggleHeight);

    const int thumbRadius = Scale(7);
    const int thumbCenterX = checked
        ? toggle.right - Scale(10)
        : toggle.left + Scale(10);
    const int thumbCenterY = (toggle.top + toggle.bottom) / 2;
    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCBrushColor(deviceContext, ToColorRef(palette_.toggleThumb));
    SetDCPenColor(
        deviceContext,
        ToColorRef(checked ? palette_.toggleThumb : palette_.border));
    Ellipse(
        deviceContext,
        thumbCenterX - thumbRadius,
        thumbCenterY - thumbRadius,
        thumbCenterX + thumbRadius + 1,
        thumbCenterY + thumbRadius + 1);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);

    RECT textBounds = bounds;
    textBounds.left = toggle.right + Scale(9);
    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(control, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font != nullptr
        ? SelectObject(deviceContext, font)
        : nullptr;
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(
        deviceContext,
        ToColorRef(enabled ? palette_.primaryText : palette_.disabledText));
    DrawTextW(
        deviceContext,
        kPauseCoveredLabel,
        static_cast<int>(std::size(kPauseCoveredLabel) - 1),
        &textBounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SIZE labelSize{};
    GetTextExtentPoint32W(
        deviceContext,
        kPauseCoveredLabel,
        static_cast<int>(std::size(kPauseCoveredLabel) - 1),
        &labelSize);
    textBounds.left += labelSize.cx + Scale(8);
    SetTextColor(
        deviceContext,
        ToColorRef(enabled ? palette_.secondaryText : palette_.disabledText));
    DrawTextW(
        deviceContext,
        kPauseCoveredNote,
        static_cast<int>(std::size(kPauseCoveredNote) - 1),
        &textBounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont != nullptr) {
        SelectObject(deviceContext, previousFont);
    }
    if (ShouldDrawKeyboardFocus(control)) {
        RECT focus = toggle;
        InflateRect(&focus, Scale(2), Scale(2));
        DrawRoundedOutline(
            deviceContext,
            focus,
            ToColorRef(palette_.secondaryText),
            toggleHeight + Scale(4));
    }
}

void SettingsWindow::DrawThemeToggleControl(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, panelBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool checked = Button_GetCheck(control) == BST_CHECKED;
    const bool hovered = IsPointerOver(control);
    const int switchWidth = Scale(34);
    const int switchHeight = Scale(18);
    const int labelWidth = Scale(38);
    const int gap = Scale(4);
    const int contentWidth = 2 * labelWidth + switchWidth + 2 * gap;
    const int contentLeft = bounds.left +
        std::max(
            0,
            (static_cast<int>(bounds.right - bounds.left) - contentWidth) / 2);

    RECT lightLabel{
        contentLeft,
        bounds.top,
        contentLeft + labelWidth,
        bounds.bottom};
    RECT toggle{
        lightLabel.right + gap,
        (bounds.top + bounds.bottom - switchHeight) / 2,
        lightLabel.right + gap + switchWidth,
        (bounds.top + bounds.bottom + switchHeight) / 2};
    RECT darkLabel{
        toggle.right + gap,
        bounds.top,
        toggle.right + gap + labelWidth,
        bounds.bottom};

    const COLORREF toggleFill = !enabled
        ? ToColorRef(palette_.disabledSurface)
        : checked
        ? ToColorRef(
              hovered ? palette_.selectedHover : palette_.selectedSurface)
        : ToColorRef(hovered ? palette_.controlHover : palette_.toggleOff);
    DrawRoundedBox(
        deviceContext,
        toggle,
        toggleFill,
        ToColorRef(palette_.border),
        switchHeight);

    const int thumbRadius = Scale(6);
    const int thumbCenterX = checked
        ? toggle.right - Scale(9)
        : toggle.left + Scale(9);
    const int thumbCenterY = (toggle.top + toggle.bottom) / 2;
    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCBrushColor(deviceContext, ToColorRef(palette_.toggleThumb));
    SetDCPenColor(deviceContext, ToColorRef(palette_.toggleThumb));
    Ellipse(
        deviceContext,
        thumbCenterX - thumbRadius,
        thumbCenterY - thumbRadius,
        thumbCenterX + thumbRadius + 1,
        thumbCenterY + thumbRadius + 1);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);

    const COLORREF activeText = ToColorRef(
        enabled ? palette_.primaryText : palette_.disabledText);
    const COLORREF inactiveText = ToColorRef(
        enabled ? palette_.secondaryText : palette_.disabledText);
    DrawCenteredText(
        control,
        deviceContext,
        lightLabel,
        checked ? inactiveText : activeText,
        L"Light");
    DrawCenteredText(
        control,
        deviceContext,
        darkLabel,
        checked ? activeText : inactiveText,
        L"Dark");

    if (ShouldDrawKeyboardFocus(control)) {
        RECT focus = toggle;
        InflateRect(&focus, Scale(2), Scale(2));
        DrawRoundedOutline(
            deviceContext,
            focus,
            ToColorRef(palette_.secondaryText),
            switchHeight + Scale(4));
    }
}

void SettingsWindow::DrawSpinnerControl(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, controlBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool hovered = IsPointerOver(control);
    POINT pointer{};
    bool upperHalfHovered = false;
    if (hovered && GetCursorPos(&pointer) && ScreenToClient(control, &pointer)) {
        upperHalfHovered = pointer.y < (bounds.top + bounds.bottom) / 2;
        RECT hoverBounds = bounds;
        if (upperHalfHovered) {
            hoverBounds.bottom = (bounds.top + bounds.bottom) / 2;
        } else {
            hoverBounds.top = (bounds.top + bounds.bottom) / 2;
        }
        SetDCBrushColor(deviceContext, ToColorRef(palette_.controlHover));
        FillRect(
            deviceContext,
            &hoverBounds,
            reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCPenColor(deviceContext, ToColorRef(palette_.border));
    const int middle = (bounds.top + bounds.bottom) / 2;
    MoveToEx(deviceContext, bounds.left, bounds.top, nullptr);
    LineTo(deviceContext, bounds.left, bounds.bottom);
    MoveToEx(deviceContext, bounds.left, middle, nullptr);
    LineTo(deviceContext, bounds.right, middle);

    SetDCPenColor(
        deviceContext,
        ToColorRef(enabled ? palette_.primaryText : palette_.disabledText));
    const int centerX = (bounds.left + bounds.right) / 2;
    const int chevron = std::max(Scale(2), 2);
    const int upperY = (bounds.top + middle) / 2;
    MoveToEx(deviceContext, centerX - chevron, upperY + Scale(1), nullptr);
    LineTo(deviceContext, centerX, upperY - Scale(1));
    LineTo(deviceContext, centerX + chevron + 1, upperY + Scale(1));
    const int lowerY = (middle + bounds.bottom) / 2;
    MoveToEx(deviceContext, centerX - chevron, lowerY - Scale(1), nullptr);
    LineTo(deviceContext, centerX, lowerY + Scale(1));
    LineTo(deviceContext, centerX + chevron + 1, lowerY - Scale(1));
    SelectObject(deviceContext, previousPen);
}

void SettingsWindow::DrawActionButton(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, panelBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool hovered = IsPointerOver(control);
    const bool pressed =
        (Button_GetState(control) & BST_PUSHED) == BST_PUSHED;
    const bool primary = GetDlgCtrlID(control) == kApplyId;

    COLORREF fillColor = ToColorRef(palette_.controlSurface);
    COLORREF borderColor = ToColorRef(palette_.border);
    COLORREF textColor = ToColorRef(
        enabled ? palette_.primaryText : palette_.disabledText);
    if (!enabled) {
        fillColor = ToColorRef(palette_.disabledSurface);
    } else if (primary) {
        fillColor = pressed
            ? ToColorRef(palette_.selectedPressed)
            : ToColorRef(
                  hovered ? palette_.selectedHover : palette_.selectedSurface);
        borderColor = fillColor;
        textColor = ToColorRef(palette_.selectedText);
    } else if (pressed || hovered) {
        fillColor = ToColorRef(
            pressed ? palette_.disabledSurface : palette_.controlHover);
        borderColor = ToColorRef(palette_.secondaryText);
    }

    RECT buttonBounds = bounds;
    buttonBounds.right -= 1;
    buttonBounds.bottom -= 1;
    DrawRoundedBox(
        deviceContext,
        buttonBounds,
        fillColor,
        borderColor,
        Scale(6));
    std::wstring_view displayText;
    if (GetDlgCtrlID(control) == kApplyId) {
        displayText = L"\u2713  Apply Wallpaper";
    } else if (GetDlgCtrlID(control) == kStopId) {
        displayText = L"\u25A0  Stop Wallpaper";
    }
    DrawCenteredText(control, deviceContext, buttonBounds, textColor, displayText);

    if (ShouldDrawKeyboardFocus(control)) {
        RECT focus = buttonBounds;
        InflateRect(&focus, -Scale(3), -Scale(3));
        DrawRoundedOutline(
            deviceContext,
            focus,
            ToColorRef(
                primary ? palette_.selectedText : palette_.secondaryText),
            Scale(4));
    }
}

RECT SettingsWindow::SliderTrackBounds(HWND control) const noexcept {
    RECT bounds{};
    GetClientRect(control, &bounds);
    const int thumbRadius = Scale(8);
    const int left = bounds.left + thumbRadius + Scale(2);
    int right = bounds.right - thumbRadius - Scale(2);
    // An even span gives the integer center pixel the exact range midpoint at
    // every DPI while changing the right inset by at most one pixel.
    if ((right - left) % 2 != 0) --right;
    return RECT{
        left,
        (bounds.top + bounds.bottom) / 2 - Scale(1),
        right,
        (bounds.top + bounds.bottom) / 2 + Scale(2)};
}

RECT SettingsWindow::SliderInteractionBounds(HWND control) const noexcept {
    RECT bounds = SliderTrackBounds(control);
    const int thumbRadius = Scale(8);
    bounds.top -= thumbRadius;
    bounds.bottom += thumbRadius;
    ++bounds.right;  // PtInRect excludes the right edge; the track endpoint is usable.
    return bounds;
}

RECT SettingsWindow::SliderThumbBounds(HWND control) const noexcept {
    RECT bounds{};
    GetClientRect(control, &bounds);
    const int minimum = static_cast<int>(
        SendMessageW(control, TBM_GETRANGEMIN, 0, 0));
    const int maximum = static_cast<int>(
        SendMessageW(control, TBM_GETRANGEMAX, 0, 0));
    const int position = static_cast<int>(
        SendMessageW(control, TBM_GETPOS, 0, 0));
    const int thumbRadius = Scale(8);
    const RECT track = SliderTrackBounds(control);
    const int trackLeft = track.left;
    const int trackRight = track.right;
    const double progress = maximum > minimum
        ? static_cast<double>(position - minimum) /
              static_cast<double>(maximum - minimum)
        : 0.0;
    const int thumbX = trackLeft + static_cast<int>(std::lround(
        progress * static_cast<double>(trackRight - trackLeft)));
    const int thumbY = (bounds.top + bounds.bottom) / 2;
    RECT thumbBounds{
        thumbX - thumbRadius,
        thumbY - thumbRadius,
        thumbX + thumbRadius + 1,
        thumbY + thumbRadius + 1};
    return thumbBounds;
}

bool SettingsWindow::HandleSliderPointerDown(
    HWND control, POINT click) noexcept {
    if (control != durationSlider_ && control != positionSlider_) {
        return false;
    }
    if (IsWindowEnabled(control) == FALSE) {
        return true;
    }
    const RECT thumbBounds = SliderThumbBounds(control);
    if (PtInRect(&thumbBounds, click)) {
        return false;
    }
    SetFocus(control);
    const RECT interactionBounds = SliderInteractionBounds(control);
    if (!PtInRect(&interactionBounds, click)) {
        return true;
    }
    const RECT track = SliderTrackBounds(control);
    const int minimum = static_cast<int>(
        SendMessageW(control, TBM_GETRANGEMIN, 0, 0));
    const int maximum = static_cast<int>(
        SendMessageW(control, TBM_GETRANGEMAX, 0, 0));
    const int value = SliderValueFromTrackClick(
        minimum, maximum, click.x, track.left, track.right);
    // TBM_SETPOS is subclassed to invalidate exactly the old/new custom thumb
    // bounds, preserving the existing tracer-free bounded repaint behavior.
    SendMessageW(control, TBM_SETPOS, TRUE, value);
    UpdateEditedSliderFromControl(control);
    return true;
}

void SettingsWindow::UpdateEditedSliderFromControl(HWND control) {
    const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
    if (control == durationSlider_) {
        state_.Edited().configuration.loopDurationSeconds =
            DurationFromSlider(value);
        updatingControls_ = true;
        SendMessageW(durationSpinner_, UDM_SETPOS32, 0, value);
        const std::wstring text = std::to_wstring(value);
        SetWindowTextW(durationEdit_, text.c_str());
        updatingControls_ = false;
        durationValid_ = true;
        InvalidateRect(durationEdit_, nullptr, TRUE);
        UpdateApplyAvailability();
    } else if (control == positionSlider_) {
        state_.Edited().configuration.position = PositionFromSlider(value);
        preview_.SetConfiguration(state_.Edited().configuration);
    }
}

void SettingsWindow::InvalidateSliderMovement(
    HWND control, const RECT& previousThumbBounds) const noexcept {
    RECT previousBounds = previousThumbBounds;
    RECT currentThumbBounds = SliderThumbBounds(control);
    InflateRect(&previousBounds, Scale(4), Scale(4));
    InflateRect(&currentThumbBounds, Scale(4), Scale(4));
    RECT invalidBounds{};
    UnionRect(&invalidBounds, &previousBounds, &currentThumbBounds);
    InvalidateRect(control, &invalidBounds, FALSE);
}

void SettingsWindow::InvalidateStyledControls() const {
    const std::array controls{
        themeToggle_, chooseButton_, directionButtons_[0], directionButtons_[1],
        directionButtons_[2], directionButtons_[3], durationSlider_,
        durationSpinner_, fitButtons_[0], fitButtons_[1], positionSlider_,
        pauseCheckBox_, stopButton_, applyButton_};
    for (HWND control : controls) {
        if (control != nullptr) {
            InvalidateRect(control, nullptr, FALSE);
        }
    }
}

bool SettingsWindow::InstallControlStyling(HWND control) const {
    return SetWindowSubclass(
               control,
               StyledControlProcedure,
               kControlSubclassId,
               reinterpret_cast<DWORD_PTR>(this)) != FALSE;
}

bool SettingsWindow::IsPointerOver(HWND control) const noexcept {
    return GetPropW(control, L"PanningWallpaper.PointerInside") != nullptr;
}

bool SettingsWindow::ShouldDrawKeyboardFocus(HWND control) const noexcept {
    if (GetFocus() != control) {
        return false;
    }
    const LRESULT uiState = SendMessageW(control, WM_QUERYUISTATE, 0, 0);
    return (uiState & UISF_HIDEFOCUS) == 0;
}

LRESULT CALLBACK SettingsWindow::StyledControlProcedure(
    HWND control,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR subclassId,
    DWORD_PTR referenceData) {
    auto* self = reinterpret_cast<SettingsWindow*>(referenceData);
    switch (message) {
    case WM_PAINT:
        if (GetDlgCtrlID(control) == kDurationSpinnerId &&
            self->theme_ == UiTheme::Light) {
            return DefSubclassProc(control, message, wParam, lParam);
        }
        self->PaintStyledControl(control);
        return 0;
    case WM_ERASEBKGND:
        if (GetDlgCtrlID(control) == kDurationSpinnerId &&
            self->theme_ == UiTheme::Light) {
            return DefSubclassProc(control, message, wParam, lParam);
        }
        return 1;
    case WM_MOUSEMOVE: {
        const int controlId = GetDlgCtrlID(control);
        const bool slider = controlId == kDurationSliderId ||
            controlId == kPositionSliderId;
        const RECT previousThumbBounds = slider
            ? self->SliderThumbBounds(control)
            : RECT{};
        if (GetPropW(control, L"PanningWallpaper.PointerInside") == nullptr) {
            SetPropW(
                control,
                L"PanningWallpaper.PointerInside",
                reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
            TRACKMOUSEEVENT tracking{
                .cbSize = sizeof(tracking),
                .dwFlags = TME_LEAVE,
                .hwndTrack = control,
            };
            TrackMouseEvent(&tracking);
            InvalidateRect(control, nullptr, FALSE);
        }
        const LRESULT result =
            DefSubclassProc(control, message, wParam, lParam);
        if (slider) {
            // The native trackbar invalidates for its smaller native thumb.
            // Include both custom-thumb extents so clipped painting clears the
            // old position without redrawing the full settings window.
            self->InvalidateSliderMovement(control, previousThumbBounds);
        }
        return result;
    }
    case WM_MOUSELEAVE:
        RemovePropW(control, L"PanningWallpaper.PointerInside");
        InvalidateRect(control, nullptr, FALSE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_UPDATEUISTATE:
    case WM_CHANGEUISTATE:
    case WM_ENABLE:
    case BM_SETCHECK: {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        InvalidateRect(control, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONDOWN:
        // Keep native focus ownership but hide keyboard-only cues for pointer
        // interaction through the standard Windows UI-state mechanism.
        SendMessageW(
            GetAncestor(control, GA_ROOT),
            WM_CHANGEUISTATE,
            MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS),
            0);
        if (self->HandleSliderPointerDown(
                control, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) {
            return 0;
        }
        [[fallthrough]];
    case WM_LBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        InvalidateRect(control, nullptr, FALSE);
        return result;
    }
    case TBM_SETPOS: {
        const RECT previousThumbBounds = self->SliderThumbBounds(control);
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        self->InvalidateSliderMovement(control, previousThumbBounds);
        return result;
    }
    case WM_NCDESTROY:
        RemovePropW(control, L"PanningWallpaper.PointerInside");
        RemoveWindowSubclass(control, StyledControlProcedure, subclassId);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wParam, lParam);
}

void SettingsWindow::SynchronizeControlsFromEditedState() {
    updatingControls_ = true;
    Button_SetCheck(
        themeToggle_,
        theme_ == UiTheme::Dark ? BST_CHECKED : BST_UNCHECKED);
    const PanningConfiguration& configuration = state_.Edited().configuration;
    CheckRadioButton(
        window_,
        kDirectionLeftId,
        kDirectionDownId,
        DirectionButtonId(configuration.direction));
    const int duration = DurationToSlider(configuration.loopDurationSeconds);
    SendMessageW(durationSlider_, TBM_SETPOS, TRUE, duration);
    SendMessageW(durationSpinner_, UDM_SETPOS32, 0, duration);
    const std::wstring durationText = std::to_wstring(duration);
    SetWindowTextW(durationEdit_, durationText.c_str());
    CheckRadioButton(
        window_,
        kFitPanId,
        kFitCoverId,
        configuration.fitMode == FitMode::Cover ? kFitCoverId : kFitPanId);
    SendMessageW(
        positionSlider_,
        TBM_SETPOS,
        TRUE,
        PositionToSlider(configuration.position));
    Button_SetCheck(
        pauseCheckBox_,
        configuration.pauseWhenCovered ? BST_CHECKED : BST_UNCHECKED);
    updatingControls_ = false;
    durationValid_ = true;
    preview_.SetConfiguration(configuration);
    UpdateDirectionDependentControls();
    UpdateApplyAvailability();
}

void SettingsWindow::UpdateEditedConfigurationFromControls(int clickedControlId) {
    PanningConfiguration& configuration = state_.Edited().configuration;
    if (clickedControlId >= kDirectionLeftId &&
        clickedControlId <= kDirectionDownId) {
        configuration.direction = DirectionFromButtonId(CheckedDirectionButton());
        preview_.SetConfiguration(configuration);
        UpdateDirectionDependentControls();
    } else if (clickedControlId == kFitPanId || clickedControlId == kFitCoverId) {
        configuration.fitMode = FitFromButtonId(CheckedFitButton());
        preview_.SetConfiguration(configuration);
        UpdateFramingAvailability();
    } else if (clickedControlId == kPauseCoveredId) {
        configuration.pauseWhenCovered =
            Button_GetCheck(pauseCheckBox_) == BST_CHECKED;
    }
    UpdateApplyAvailability();
}

void SettingsWindow::UpdateDurationFromEdit() {
    if (updatingControls_) {
        return;
    }

    const int length = GetWindowTextLengthW(durationEdit_);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        const int copied = GetWindowTextW(durationEdit_, text.data(), length + 1);
        text.resize(static_cast<size_t>(std::max(copied, 0)));
    } else {
        text.clear();
    }

    int duration = 0;
    durationValid_ = TryParseGuiDuration(text, duration);
    if (durationValid_) {
        state_.Edited().configuration.loopDurationSeconds =
            static_cast<double>(duration);
        SendMessageW(
            durationSlider_,
            TBM_SETPOS,
            TRUE,
            DurationToSlider(duration));
    }
    InvalidateRect(durationEdit_, nullptr, TRUE);
    UpdateApplyAvailability();
}

void SettingsWindow::UpdateDirectionDependentControls() {
    const PanDirection direction = state_.Edited().configuration.direction;
    const bool vertical =
        direction == PanDirection::Up || direction == PanDirection::Down;
    SetWindowTextW(fitButtons_[0], vertical ? L"Fit width" : L"Fit height");
    SetWindowTextW(positionLabel_, vertical
        ? L"Horizontal framing"
        : L"Vertical framing");
    SetWindowTextW(positionStartLabel_, vertical ? L"Left" : L"Top");
    SetWindowTextW(positionEndLabel_, vertical ? L"Right" : L"Bottom");
    UpdateFramingAvailability();
}

void SettingsWindow::UpdateFramingAvailability() {
    const bool enabled = preview_.HasMeaningfulFraming();
    if ((IsWindowEnabled(positionSlider_) != FALSE) != enabled) {
        // Keep static text enabled so Windows preserves its exact typography;
        // WM_CTLCOLORSTATIC supplies the quieter framing color instead.
        EnableWindow(positionSlider_, enabled ? TRUE : FALSE);
        InvalidateRect(positionLabel_, nullptr, FALSE);
        InvalidateRect(positionStartLabel_, nullptr, FALSE);
        InvalidateRect(positionEndLabel_, nullptr, FALSE);
    }
}

void SettingsWindow::UpdateApplyAvailability() {
    const bool available =
        CanApplyEditedSettings(state_.Edited(), durationValid_);
    EnableWindow(applyButton_, available ? TRUE : FALSE);
}

void SettingsWindow::ChooseImage() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));
    if (FAILED(result)) {
        ShowError(L"The Windows image chooser could not be opened.");
        return;
    }

    const COMDLG_FILTERSPEC filters[] = {
        {L"Supported images", L"*.png;*.jpg;*.jpeg"},
        {L"PNG images", L"*.png"},
        {L"JPEG images", L"*.jpg;*.jpeg"},
    };
    FILEOPENDIALOGOPTIONS options = 0;
    result = dialog->SetFileTypes(
        static_cast<UINT>(std::size(filters)), filters);
    if (SUCCEEDED(result)) {
        result = dialog->SetFileTypeIndex(1);
    }
    if (SUCCEEDED(result)) {
        result = dialog->SetTitle(L"Choose a wallpaper image");
    }
    if (SUCCEEDED(result)) {
        result = dialog->GetOptions(&options);
    }
    if (SUCCEEDED(result)) {
        result = dialog->SetOptions(
            options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST |
            FOS_FORCEFILESYSTEM);
    }
    if (FAILED(result)) {
        ShowError(L"The Windows image chooser could not be configured.");
        return;
    }

    result = dialog->Show(window_);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return;
    }
    if (FAILED(result)) {
        ShowError(L"The Windows image chooser could not complete.");
        return;
    }

    Microsoft::WRL::ComPtr<IShellItem> item;
    result = dialog->GetResult(&item);
    if (FAILED(result)) {
        ShowError(L"The selected image path could not be read.");
        return;
    }

    PWSTR rawPath = nullptr;
    result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    if (FAILED(result) || rawPath == nullptr) {
        ShowError(L"The selected image path could not be read.");
        return;
    }
    const std::wstring selectedPath(rawPath);
    CoTaskMemFree(rawPath);

    DecodedImage fullImage;
    std::wstring decodeError;
    if (!DecodeImageFile(selectedPath, fullImage, decodeError)) {
        ShowError(L"The selected image could not be opened.\n\n" + decodeError);
        return;
    }

    DecodedImage previewImage;
    std::wstring previewError;
    if (!CreateBoundedPreviewImage(
            fullImage,
            kPreviewMaximumWidth,
            kPreviewMaximumHeight,
            previewImage,
            previewError)) {
        ShowError(L"The image preview could not be prepared.\n\n" + previewError);
        return;
    }

    state_.Edited().imagePath = selectedPath;
    pendingFullImage_ = std::move(fullImage);
    SetWindowTextW(imagePathEdit_, selectedPath.c_str());
    preview_.SetImage(std::move(previewImage));
    preview_.SetConfiguration(state_.Edited().configuration);
    UpdateFramingAvailability();
    UpdateApplyAvailability();
}

void SettingsWindow::ApplyEditedSettings() {
    if (!CanApplyEditedSettings(state_.Edited(), durationValid_)) {
        MessageBeep(MB_ICONWARNING);
        UpdateApplyAvailability();
        return;
    }

    std::wstring error;
    const DecodedImage* decodedImage =
        pendingFullImage_.pixels.empty() ? nullptr : &pendingFullImage_;
    if (!application_.ApplyWallpaper(
            state_.Edited().imagePath,
            state_.Edited().configuration,
            decodedImage,
            error)) {
        ShowError(error);
        SetWallpaperRunning(application_.IsWallpaperRunning());
        return;
    }

    state_.MarkApplied();
    pendingFullImage_ = {};
}

void SettingsWindow::ShowError(std::wstring_view message) const {
    const std::wstring ownedMessage(message);
    MessageBoxW(
        window_,
        ownedMessage.c_str(),
        kSettingsWindowTitle,
        MB_OK | MB_ICONERROR);
}

int SettingsWindow::Scale(int logicalPixels) const noexcept {
    return MulDiv(logicalPixels, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI);
}

int SettingsWindow::CheckedDirectionButton() const noexcept {
    for (int id = kDirectionLeftId; id <= kDirectionDownId; ++id) {
        if (IsDlgButtonChecked(window_, id) == BST_CHECKED) {
            return id;
        }
    }
    return kDirectionLeftId;
}

int SettingsWindow::CheckedFitButton() const noexcept {
    return IsDlgButtonChecked(window_, kFitCoverId) == BST_CHECKED
        ? kFitCoverId
        : kFitPanId;
}

}  // namespace panning_wallpaper
