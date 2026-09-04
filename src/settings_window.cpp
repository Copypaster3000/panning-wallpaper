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

constexpr int kPreviewMaximumWidth = 1024;
constexpr int kPreviewMaximumHeight = 576;

constexpr COLORREF kWindowColor = RGB(247, 246, 244);
constexpr COLORREF kPanelColor = RGB(252, 251, 249);
constexpr COLORREF kControlColor = RGB(248, 247, 245);
constexpr COLORREF kControlHoverColor = RGB(241, 240, 237);
constexpr COLORREF kBorderColor = RGB(218, 216, 212);
constexpr COLORREF kPrimaryTextColor = RGB(55, 53, 51);
constexpr COLORREF kSecondaryTextColor = RGB(112, 109, 105);
constexpr COLORREF kSelectedColor = RGB(89, 87, 83);
constexpr COLORREF kSelectedHoverColor = RGB(74, 72, 69);
constexpr COLORREF kSelectedPressedColor = RGB(65, 63, 60);
constexpr COLORREF kDisabledFillColor = RGB(225, 223, 219);
constexpr COLORREF kDisabledTextColor = RGB(145, 142, 138);
constexpr COLORREF kWhiteColor = RGB(255, 255, 255);

constexpr UINT_PTR kControlSubclassId = 1;

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

bool SettingsWindow::Initialize(HINSTANCE instance, std::wstring& error) {
    instance_ = instance;

    windowBrush_ = CreateSolidBrush(kWindowColor);
    panelBrush_ = CreateSolidBrush(kPanelColor);
    controlBrush_ = CreateSolidBrush(kControlColor);
    invalidEditBrush_ = CreateSolidBrush(RGB(255, 232, 232));
    if (windowBrush_ == nullptr || panelBrush_ == nullptr ||
        controlBrush_ == nullptr || invalidEditBrush_ == nullptr) {
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
        SetWindowTextW(statusLabel_, L"\u25CF  Wallpaper is running");
    } else {
        state_.ClearApplied();
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
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintWindow();
        return 0;
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
        SetBkColor(reinterpret_cast<HDC>(wParam), kControlColor);
        SetTextColor(reinterpret_cast<HDC>(wParam), kPrimaryTextColor);
        return reinterpret_cast<LRESULT>(controlBrush_);
    case WM_CTLCOLORSTATIC: {
        const HWND control = reinterpret_cast<HWND>(lParam);
        const int id = GetDlgCtrlID(control);
        const HDC deviceContext = reinterpret_cast<HDC>(wParam);
        if (control == imagePathEdit_) {
            SetBkColor(deviceContext, kControlColor);
            SetTextColor(deviceContext, kSecondaryTextColor);
            return reinterpret_cast<LRESULT>(controlBrush_);
        }
        SetBkColor(deviceContext, kPanelColor);
        const bool secondary = id == kPositionStartId || id == kPositionEndId ||
            id == kSecondsLabelId || id == kStatusId || id == kImagePathId;
        SetTextColor(
            deviceContext,
            secondary ? kSecondaryTextColor : kPrimaryTextColor);
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
        0, WC_STATICW, L"Fit", WS_VISIBLE | SS_CENTERIMAGE, kFitLabelId);
    fitButtons_[0] = CreateControl(
        0,
        WC_BUTTONW,
        L"Pan",
        WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        kFitPanId);
    fitButtons_[1] = CreateControl(
        0, WC_BUTTONW, L"Cover", WS_VISIBLE | BS_AUTORADIOBUTTON, kFitCoverId);
    positionLabel_ = CreateControl(
        0, WC_STATICW, L"Position", WS_VISIBLE | SS_CENTERIMAGE, kPositionLabelId);
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
        L"Pause when fully covered",
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
        imageLabel_, imagePathEdit_, chooseButton_,
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
        chooseButton_, directionButtons_[0], directionButtons_[1],
        directionButtons_[2], directionButtons_[3], durationSlider_,
        fitButtons_[0], fitButtons_[1], positionSlider_, pauseCheckBox_,
        stopButton_, applyButton_};
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
    MoveWindow(
        imagePathEdit_,
        contentX,
        y,
        std::max(Scale(80), contentWidth - chooseWidth - Scale(8)),
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
        imageLabel_, imagePathEdit_, chooseButton_, directionLabel_,
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
        kPanelColor,
        kBorderColor,
        cardRadius);
    DrawRoundedBox(
        deviceContext,
        actionsPanelBounds_,
        kPanelColor,
        kBorderColor,
        cardRadius);
    DrawRoundedBox(
        deviceContext,
        directionGroupBounds_,
        kControlColor,
        kBorderColor,
        Scale(6));
    DrawRoundedBox(
        deviceContext,
        fitGroupBounds_,
        kControlColor,
        kBorderColor,
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
    if ((id >= kDirectionLeftId && id <= kDirectionDownId) ||
        id == kFitPanId || id == kFitCoverId) {
        DrawSegmentControl(control, drawingContext, bounds);
    } else if (id == kDurationSliderId || id == kPositionSliderId) {
        DrawSliderControl(control, drawingContext, bounds);
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

    COLORREF fillColor = kControlColor;
    COLORREF textColor = enabled ? kPrimaryTextColor : kDisabledTextColor;
    if (selected) {
        fillColor = pressed
            ? kSelectedPressedColor
            : (hovered ? kSelectedHoverColor : kSelectedColor);
        textColor = enabled ? kWhiteColor : kDisabledTextColor;
    } else if (hovered && enabled) {
        fillColor = kControlHoverColor;
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
        SetDCPenColor(deviceContext, kBorderColor);
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

    if (GetFocus() == control) {
        RECT focus = bounds;
        InflateRect(&focus, -Scale(4), -Scale(4));
        DrawFocusRect(deviceContext, &focus);
    }
}

void SettingsWindow::DrawSliderControl(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, panelBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool hovered = IsPointerOver(control);
    const int minimum = static_cast<int>(
        SendMessageW(control, TBM_GETRANGEMIN, 0, 0));
    const int maximum = static_cast<int>(
        SendMessageW(control, TBM_GETRANGEMAX, 0, 0));
    const int position = static_cast<int>(
        SendMessageW(control, TBM_GETPOS, 0, 0));
    const int thumbRadius = Scale(8);
    const int trackLeft = bounds.left + thumbRadius + Scale(2);
    const int trackRight = bounds.right - thumbRadius - Scale(2);
    const int trackY = (bounds.top + bounds.bottom) / 2;
    const double progress = maximum > minimum
        ? static_cast<double>(position - minimum) /
              static_cast<double>(maximum - minimum)
        : 0.0;
    const int thumbX = trackLeft + static_cast<int>(std::lround(
        progress * static_cast<double>(trackRight - trackLeft)));

    RECT track{
        trackLeft, trackY - Scale(1), trackRight, trackY + Scale(2)};
    const COLORREF trackColor = enabled ? kBorderColor : kDisabledFillColor;
    SetDCBrushColor(deviceContext, trackColor);
    FillRect(
        deviceContext,
        &track,
        reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCBrushColor(deviceContext, enabled ? kPanelColor : kControlColor);
    SetDCPenColor(
        deviceContext,
        !enabled ? kDisabledTextColor
                 : (hovered ? kSelectedColor : kBorderColor));
    Ellipse(
        deviceContext,
        thumbX - thumbRadius,
        trackY - thumbRadius,
        thumbX + thumbRadius + 1,
        trackY + thumbRadius + 1);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);

    if (GetFocus() == control) {
        RECT focus = bounds;
        InflateRect(&focus, -Scale(2), -Scale(2));
        DrawFocusRect(deviceContext, &focus);
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
        ? kDisabledFillColor
        : (checked ? (hovered ? kSelectedHoverColor : kSelectedColor)
                   : (hovered ? kControlHoverColor : kControlColor));
    DrawRoundedBox(
        deviceContext, toggle, fillColor, kBorderColor, toggleHeight);

    const int thumbRadius = Scale(7);
    const int thumbCenterX = checked
        ? toggle.right - Scale(10)
        : toggle.left + Scale(10);
    const int thumbCenterY = (toggle.top + toggle.bottom) / 2;
    const HGDIOBJ previousBrush = SelectObject(
        deviceContext, GetStockObject(DC_BRUSH));
    const HGDIOBJ previousPen = SelectObject(
        deviceContext, GetStockObject(DC_PEN));
    SetDCBrushColor(deviceContext, kWhiteColor);
    SetDCPenColor(deviceContext, checked ? kWhiteColor : kBorderColor);
    Ellipse(
        deviceContext,
        thumbCenterX - thumbRadius,
        thumbCenterY - thumbRadius,
        thumbCenterX + thumbRadius + 1,
        thumbCenterY + thumbRadius + 1);
    SelectObject(deviceContext, previousPen);
    SelectObject(deviceContext, previousBrush);

    wchar_t text[128]{};
    const int length = GetWindowTextW(
        control, text, static_cast<int>(std::size(text)));
    RECT textBounds = bounds;
    textBounds.left = toggle.right + Scale(9);
    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(control, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font != nullptr
        ? SelectObject(deviceContext, font)
        : nullptr;
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(
        deviceContext, enabled ? kPrimaryTextColor : kDisabledTextColor);
    DrawTextW(
        deviceContext,
        text,
        std::max(length, 0),
        &textBounds,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (previousFont != nullptr) {
        SelectObject(deviceContext, previousFont);
    }
    if (GetFocus() == control) {
        RECT focus = bounds;
        InflateRect(&focus, -Scale(2), -Scale(2));
        DrawFocusRect(deviceContext, &focus);
    }
}

void SettingsWindow::DrawActionButton(
    HWND control, HDC deviceContext, const RECT& bounds) {
    FillRect(deviceContext, &bounds, panelBrush_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool hovered = IsPointerOver(control);
    const bool pressed =
        (Button_GetState(control) & BST_PUSHED) == BST_PUSHED;
    const bool primary = GetDlgCtrlID(control) == kApplyId;

    COLORREF fillColor = kControlColor;
    COLORREF borderColor = kBorderColor;
    COLORREF textColor = enabled ? kPrimaryTextColor : kDisabledTextColor;
    if (!enabled) {
        fillColor = kDisabledFillColor;
    } else if (primary) {
        fillColor = pressed
            ? kSelectedPressedColor
            : (hovered ? kSelectedHoverColor : kSelectedColor);
        borderColor = fillColor;
        textColor = kWhiteColor;
    } else if (pressed || hovered) {
        fillColor = pressed ? kDisabledFillColor : kControlHoverColor;
        borderColor = kSecondaryTextColor;
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

    if (GetFocus() == control) {
        RECT focus = buttonBounds;
        InflateRect(&focus, -Scale(4), -Scale(4));
        DrawFocusRect(deviceContext, &focus);
    }
}

void SettingsWindow::InvalidateStyledControls() const {
    const std::array controls{
        chooseButton_, directionButtons_[0], directionButtons_[1],
        directionButtons_[2], directionButtons_[3], durationSlider_,
        fitButtons_[0], fitButtons_[1], positionSlider_, pauseCheckBox_,
        stopButton_, applyButton_};
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
        self->PaintStyledControl(control);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
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
        break;
    case WM_MOUSELEAVE:
        RemovePropW(control, L"PanningWallpaper.PointerInside");
        InvalidateRect(control, nullptr, FALSE);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case BM_SETCHECK: {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        InvalidateRect(control, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        InvalidateRect(control, nullptr, FALSE);
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
