#include "settings_store.h"

#include <format>
#include <utility>

namespace panning_wallpaper {
namespace {

struct RegistryKey {
    HKEY handle = nullptr;
    ~RegistryKey() { if (handle != nullptr) RegCloseKey(handle); }
};

bool ReportResult(LSTATUS result, std::wstring& error) {
    if (result == ERROR_SUCCESS) return true;
    error = std::format(L"Settings could not be saved or read (registry error {}).", result);
    return false;
}

bool OpenForWrite(const std::wstring& path, RegistryKey& key, std::wstring& error) {
    error.clear();
    return ReportResult(RegCreateKeyExW(
        HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key.handle, nullptr), error);
}

std::optional<DWORD> ReadNumber(HKEY key, const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD,
                    nullptr, &value, &size) != ERROR_SUCCESS ||
        size != sizeof(value)) return {};
    return value;
}

std::wstring ReadString(HKEY key, const wchar_t* name) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ,
                    nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t) || size > 32768 * sizeof(wchar_t) ||
        size % sizeof(wchar_t) != 0) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ,
                    nullptr, value.data(), &size) != ERROR_SUCCESS) return {};
    // The size-only query may reserve extra space for a missing terminator.
    value.resize(size / sizeof(wchar_t));
    const auto terminator = value.find(L'\0');
    if (terminator == std::wstring::npos ||
        value.find_first_not_of(L'\0', terminator) != std::wstring::npos) return {};
    value.resize(terminator);
    return value;
}

bool WriteNumber(HKEY key, const wchar_t* name, DWORD value, std::wstring& error) {
    return ReportResult(RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)), error);
}

bool WriteString(HKEY key, const wchar_t* name, const std::wstring& value,
                 std::wstring& error) {
    return ReportResult(RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))), error);
}

}  // namespace

SettingsStore::SettingsStore(std::wstring keyPath) : keyPath_(std::move(keyPath)) {}

SavedSettings SettingsStore::Load(std::wstring& error) const {
    error.clear();
    SavedSettings saved;
    RegistryKey key;
    const LSTATUS result = RegOpenKeyExW(
        HKEY_CURRENT_USER, keyPath_.c_str(), 0, KEY_QUERY_VALUE, &key.handle);
    if (result == ERROR_FILE_NOT_FOUND) return saved;
    if (!ReportResult(result, error)) return saved;
    const auto version = ReadNumber(key.handle, L"SchemaVersion");
    if (version && *version != 1) return saved;

    if (ReadString(key.handle, L"Theme") == L"Dark") saved.theme = UiTheme::Dark;
    saved.wallpaperEnabled = ReadNumber(key.handle, L"WallpaperEnabled") == DWORD{1};
    WallpaperSettings settings;
    settings.imagePath = ReadString(key.handle, L"ImagePath");
    if (settings.imagePath.empty()) return saved;
    auto& config = settings.configuration;
    const auto direction = ReadString(key.handle, L"Direction");
    if (direction == L"Right") config.direction = PanDirection::Right;
    else if (direction == L"Up") config.direction = PanDirection::Up;
    else if (direction == L"Down") config.direction = PanDirection::Down;
    if (ReadString(key.handle, L"Fit") == L"Cover") config.fitMode = FitMode::Cover;
    const auto duration = ReadNumber(key.handle, L"DurationSeconds");
    if (duration && *duration >= kMinimumGuiDurationSeconds &&
        *duration <= kMaximumGuiDurationSeconds) config.loopDurationSeconds = *duration;
    const auto position = ReadNumber(key.handle, L"PositionPercent");
    if (position && *position <= kMaximumPositionSliderValue)
        config.position = PositionFromSlider(static_cast<int>(*position));
    const auto pause = ReadNumber(key.handle, L"PauseWhenCovered");
    if (pause && *pause <= 1) config.pauseWhenCovered = *pause != 0;
    saved.applied = std::move(settings);
    return saved;
}

bool SettingsStore::SaveApplied(
    const WallpaperSettings& settings, std::wstring& error) const {
    if (!CanApplyEditedSettings(settings, true) || settings.imagePath.size() >= 32768 ||
        settings.imagePath.find(L'\0') != std::wstring::npos) {
        error = L"The applied settings could not be saved because they are invalid.";
        return false;
    }
    RegistryKey key;
    if (!OpenForWrite(keyPath_, key, error)) return false;
    const auto& config = settings.configuration;
    const wchar_t* direction = L"Left";
    switch (config.direction) {
    case PanDirection::Left: break;
    case PanDirection::Right: direction = L"Right"; break;
    case PanDirection::Up: direction = L"Up"; break;
    case PanDirection::Down: direction = L"Down"; break;
    }
    return WriteNumber(key.handle, L"SchemaVersion", 1, error) &&
        WriteString(key.handle, L"ImagePath", settings.imagePath, error) &&
        WriteString(key.handle, L"Direction", direction, error) &&
        WriteNumber(key.handle, L"DurationSeconds",
            static_cast<DWORD>(config.loopDurationSeconds), error) &&
        WriteString(key.handle, L"Fit", config.fitMode == FitMode::Pan ? L"Pan" : L"Cover", error) &&
        WriteNumber(key.handle, L"PositionPercent", PositionToSlider(config.position), error) &&
        WriteNumber(key.handle, L"PauseWhenCovered", config.pauseWhenCovered ? 1 : 0, error) &&
        WriteNumber(key.handle, L"WallpaperEnabled", 1, error);
}

bool SettingsStore::SaveEnabled(bool enabled, std::wstring& error) const {
    RegistryKey key;
    return OpenForWrite(keyPath_, key, error) &&
        WriteNumber(key.handle, L"WallpaperEnabled", enabled ? 1 : 0, error);
}

bool SettingsStore::SaveTheme(UiTheme theme, std::wstring& error) const {
    RegistryKey key;
    return OpenForWrite(keyPath_, key, error) &&
        WriteString(key.handle, L"Theme", theme == UiTheme::Dark ? L"Dark" : L"Light", error);
}

}  // namespace panning_wallpaper
