#include "settings_window.h"

#include "application.h"
#include "preview_image.h"

#include <commctrl.h>
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

constexpr int kTitleId = 100;
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

constexpr int kPreviewMaximumWidth = 1024;
constexpr int kPreviewMaximumHeight = 576;

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
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
    }
    if (invalidEditBrush_ != nullptr) {
        DeleteObject(invalidEditBrush_);
    }
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kSettingsWindowClassName, instance_);
    }
}

bool SettingsWindow::Initialize(HINSTANCE instance, std::wstring& error) {
    instance_ = instance;

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
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
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
    RECT desiredClient{0, 0, Scale(720), Scale(620)};
    if (!AdjustWindowRectExForDpi(
            &desiredClient,
            WS_OVERLAPPEDWINDOW,
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
        WS_OVERLAPPEDWINDOW,
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
    invalidEditBrush_ = CreateSolidBrush(RGB(255, 232, 232));
    if (!CreateControls(error)) {
        return false;
    }

    UpdateFonts();
    SynchronizeControlsFromEditedState();
    RECT client{};
    GetClientRect(window_, &client);
    LayoutControls(client.right, client.bottom);
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
    SetFocus(chooseButton_);
    return true;
}

HWND SettingsWindow::Window() const noexcept {
    return window_;
}

void SettingsWindow::SetWallpaperRunning(bool running) {
    if (running) {
        SetWindowTextW(statusLabel_, L"Wallpaper is running");
    } else {
        state_.ClearApplied();
        SetWindowTextW(statusLabel_, L"Wallpaper is stopped");
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
    ShowError(message);
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
    switch (message) {
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
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
        if (control == durationSlider_) {
            const int value = static_cast<int>(
                SendMessageW(durationSlider_, TBM_GETPOS, 0, 0));
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
            return 0;
        }
        if (control == positionSlider_) {
            const int value = static_cast<int>(
                SendMessageW(positionSlider_, TBM_GETPOS, 0, 0));
            state_.Edited().configuration.position = PositionFromSlider(value);
            preview_.SetConfiguration(state_.Edited().configuration);
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lParam) == durationEdit_ && !durationValid_) {
            const HDC deviceContext = reinterpret_cast<HDC>(wParam);
            SetBkColor(deviceContext, RGB(255, 232, 232));
            SetTextColor(deviceContext, RGB(128, 0, 0));
            return reinterpret_cast<LRESULT>(invalidEditBrush_);
        }
        break;
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
        RECT client{0, 0, Scale(620), Scale(460)};
        AdjustWindowRectExForDpi(
            &client,
            WS_OVERLAPPEDWINDOW,
            FALSE,
            WS_EX_CONTROLPARENT,
            dpi_);
        limits->ptMinTrackSize.x = client.right - client.left;
        limits->ptMinTrackSize.y = client.bottom - client.top;
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window_);
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
    titleLabel_ = CreateControl(
        0, WC_STATICW, L"Panning Wallpaper", WS_VISIBLE, kTitleId);
    if (!preview_.Initialize(instance_, window_, kPreviewId, error)) {
        return false;
    }
    imageLabel_ = CreateControl(
        0, WC_STATICW, L"Wallpaper", WS_VISIBLE, kImageLabelId);
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
    directionLabel_ = CreateControl(
        0, WC_STATICW, L"Direction", WS_VISIBLE, kDirectionLabelId);
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
        0, WC_STATICW, L"Loop duration", WS_VISIBLE, kDurationLabelId);
    durationSlider_ = CreateControl(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        kDurationSliderId);
    durationEdit_ = CreateControl(
        WS_EX_CLIENTEDGE,
        WC_EDITW,
        L"90",
        WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT | ES_AUTOHSCROLL,
        kDurationEditId);
    durationSpinner_ = CreateControl(
        0,
        UPDOWN_CLASSW,
        L"",
        WS_VISIBLE | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS |
            UDS_SETBUDDYINT,
        kDurationSpinnerId);
    secondsLabel_ = CreateControl(
        0, WC_STATICW, L"seconds", WS_VISIBLE, kSecondsLabelId);
    fitLabel_ = CreateControl(0, WC_STATICW, L"Fit", WS_VISIBLE, kFitLabelId);
    fitButtons_[0] = CreateControl(
        0,
        WC_BUTTONW,
        L"Pan",
        WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        kFitPanId);
    fitButtons_[1] = CreateControl(
        0, WC_BUTTONW, L"Cover", WS_VISIBLE | BS_AUTORADIOBUTTON, kFitCoverId);
    positionLabel_ = CreateControl(
        0, WC_STATICW, L"Position", WS_VISIBLE, kPositionLabelId);
    positionStartLabel_ = CreateControl(
        0,
        WC_STATICW,
        L"Top",
        WS_VISIBLE | SS_RIGHT,
        kPositionStartId);
    positionSlider_ = CreateControl(
        0,
        TRACKBAR_CLASSW,
        L"",
        WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        kPositionSliderId);
    positionEndLabel_ = CreateControl(
        0, WC_STATICW, L"Bottom", WS_VISIBLE, kPositionEndId);
    pauseCheckBox_ = CreateControl(
        0,
        WC_BUTTONW,
        L"Pause when fully covered",
        WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        kPauseCoveredId);
    statusLabel_ = CreateControl(
        0, WC_STATICW, L"Wallpaper is stopped", WS_VISIBLE, kStatusId);
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
        titleLabel_, imageLabel_, imagePathEdit_, chooseButton_,
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
    if (titleLabel_ == nullptr) {
        return;
    }

    const int margin = Scale(20);
    const int labelWidth = Scale(100);
    const int rowHeight = Scale(28);
    const int rowGap = Scale(10);
    const int contentX = margin + labelWidth;
    const int contentWidth = std::max(Scale(200), clientWidth - contentX - margin);

    int y = margin;
    MoveWindow(titleLabel_, margin, y, clientWidth - 2 * margin, Scale(28), TRUE);
    y += Scale(38);

    const int spaceBelowPreview = Scale(330);
    const int previewHeight =
        std::max(Scale(105), clientHeight - y - spaceBelowPreview);
    MoveWindow(
        preview_.Window(),
        margin,
        y,
        clientWidth - 2 * margin,
        previewHeight,
        TRUE);
    y += previewHeight + Scale(14);

    MoveWindow(imageLabel_, margin, y + Scale(5), labelWidth, rowHeight, TRUE);
    const int chooseWidth = Scale(116);
    MoveWindow(
        imagePathEdit_,
        contentX,
        y,
        std::max(Scale(50), contentWidth - chooseWidth - Scale(8)),
        rowHeight,
        TRUE);
    MoveWindow(
        chooseButton_,
        contentX + contentWidth - chooseWidth,
        y,
        chooseWidth,
        rowHeight,
        TRUE);
    y += rowHeight + rowGap;

    MoveWindow(directionLabel_, margin, y + Scale(5), labelWidth, rowHeight, TRUE);
    const int directionWidth = Scale(70);
    for (size_t index = 0; index < directionButtons_.size(); ++index) {
        MoveWindow(
            directionButtons_[index],
            contentX + static_cast<int>(index) * directionWidth,
            y,
            directionWidth,
            rowHeight,
            TRUE);
    }
    y += rowHeight + rowGap;

    MoveWindow(durationLabel_, margin, y + Scale(5), labelWidth, rowHeight, TRUE);
    const int durationEditWidth = Scale(66);
    const int secondsWidth = Scale(58);
    const int durationSliderWidth = std::max(
        Scale(80), contentWidth - durationEditWidth - secondsWidth - Scale(12));
    MoveWindow(durationSlider_, contentX, y, durationSliderWidth, rowHeight, TRUE);
    MoveWindow(
        durationEdit_,
        contentX + durationSliderWidth + Scale(8),
        y,
        durationEditWidth,
        rowHeight,
        TRUE);
    MoveWindow(
        secondsLabel_,
        contentX + durationSliderWidth + durationEditWidth + Scale(12),
        y + Scale(5),
        secondsWidth,
        rowHeight,
        TRUE);
    y += rowHeight + rowGap;

    MoveWindow(fitLabel_, margin, y + Scale(5), labelWidth, rowHeight, TRUE);
    MoveWindow(fitButtons_[0], contentX, y, Scale(80), rowHeight, TRUE);
    MoveWindow(fitButtons_[1], contentX + Scale(80), y, Scale(80), rowHeight, TRUE);
    y += rowHeight + rowGap;

    MoveWindow(positionLabel_, margin, y + Scale(5), labelWidth, rowHeight, TRUE);
    const int endpointWidth = Scale(48);
    const int positionSliderWidth =
        std::max(Scale(80), contentWidth - 2 * endpointWidth - Scale(12));
    MoveWindow(positionStartLabel_, contentX, y + Scale(5), endpointWidth, rowHeight, TRUE);
    MoveWindow(
        positionSlider_,
        contentX + endpointWidth + Scale(6),
        y,
        positionSliderWidth,
        rowHeight,
        TRUE);
    MoveWindow(
        positionEndLabel_,
        contentX + endpointWidth + Scale(12) + positionSliderWidth,
        y + Scale(5),
        endpointWidth,
        rowHeight,
        TRUE);
    y += rowHeight + Scale(8);

    MoveWindow(
        pauseCheckBox_, contentX, y, contentWidth, Scale(24), TRUE);
    y += Scale(40);

    const int buttonWidth = Scale(120);
    const int buttonHeight = Scale(32);
    MoveWindow(
        statusLabel_, margin, y + Scale(7), clientWidth - 2 * margin -
            2 * buttonWidth - Scale(20), buttonHeight, TRUE);
    MoveWindow(
        stopButton_,
        clientWidth - margin - 2 * buttonWidth - Scale(8),
        y,
        buttonWidth,
        buttonHeight,
        TRUE);
    MoveWindow(
        applyButton_,
        clientWidth - margin - buttonWidth,
        y,
        buttonWidth,
        buttonHeight,
        TRUE);
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
    LOGFONTW titleDescription = metrics.lfMessageFont;
    titleDescription.lfHeight = -MulDiv(16, dpi_, 72);
    titleDescription.lfWeight = FW_SEMIBOLD;
    HFONT newTitleFont = CreateFontIndirectW(&titleDescription);
    if (newUiFont == nullptr || newTitleFont == nullptr) {
        if (newUiFont != nullptr) {
            DeleteObject(newUiFont);
        }
        if (newTitleFont != nullptr) {
            DeleteObject(newTitleFont);
        }
        return;
    }

    const std::array controls{
        imageLabel_, imagePathEdit_, chooseButton_, directionLabel_,
        directionButtons_[0], directionButtons_[1], directionButtons_[2],
        directionButtons_[3], durationLabel_, durationSlider_, durationEdit_,
        durationSpinner_, secondsLabel_, fitLabel_, fitButtons_[0], fitButtons_[1],
        positionLabel_, positionStartLabel_, positionSlider_, positionEndLabel_,
        pauseCheckBox_, statusLabel_, stopButton_, applyButton_};
    for (HWND control : controls) {
        ApplyFont(control, newUiFont);
    }
    ApplyFont(titleLabel_, newTitleFont);

    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
    }
    uiFont_ = newUiFont;
    titleFont_ = newTitleFont;
}

void SettingsWindow::ApplyFont(HWND control, HFONT font) const {
    if (control != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void SettingsWindow::SynchronizeControlsFromEditedState() {
    updatingControls_ = true;
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
    UpdatePositionLabels();
    preview_.SetConfiguration(configuration);
    UpdateApplyAvailability();
}

void SettingsWindow::UpdateEditedConfigurationFromControls(int clickedControlId) {
    PanningConfiguration& configuration = state_.Edited().configuration;
    if (clickedControlId >= kDirectionLeftId &&
        clickedControlId <= kDirectionDownId) {
        configuration.direction = DirectionFromButtonId(CheckedDirectionButton());
        UpdatePositionLabels();
        preview_.SetConfiguration(configuration);
    } else if (clickedControlId == kFitPanId || clickedControlId == kFitCoverId) {
        configuration.fitMode = FitFromButtonId(CheckedFitButton());
        preview_.SetConfiguration(configuration);
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

void SettingsWindow::UpdatePositionLabels() {
    const PanDirection direction = state_.Edited().configuration.direction;
    const bool vertical =
        direction == PanDirection::Up || direction == PanDirection::Down;
    SetWindowTextW(positionStartLabel_, vertical ? L"Left" : L"Top");
    SetWindowTextW(positionEndLabel_, vertical ? L"Right" : L"Bottom");
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
    SetWallpaperRunning(true);
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
