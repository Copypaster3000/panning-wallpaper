#include "settings_store.h"

#include <iostream>
#include <string>

using namespace panning_wallpaper;

namespace {
int failures = 0;
void Check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

// Every run owns a fresh volatile key; never touch the application's real settings.
struct TestKey {
    std::wstring path = L"Software\\PanningWallpaper.Test." +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HKEY handle = nullptr;
    ~TestKey() {
        if (handle != nullptr) {
            RegCloseKey(handle);
            RegDeleteKeyW(HKEY_CURRENT_USER, path.c_str());
        }
    }
};

void Number(HKEY key, const wchar_t* name, DWORD value) {
    Check(RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS,
        "write test number");
}
void String(HKEY key, const wchar_t* name, const std::wstring& value) {
    Check(RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS,
        "write test string");
}
}  // namespace

int main() {
    TestKey key;
    SettingsStore store(key.path);
    std::wstring error;
    auto loaded = store.Load(error);
    Check(error.empty() && !loaded.applied && !loaded.wallpaperEnabled &&
          loaded.theme == UiTheme::Light, "missing key uses defaults");
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.path.c_str(), 0, nullptr,
        REG_OPTION_VOLATILE, KEY_ALL_ACCESS, nullptr, &key.handle, &disposition) != ERROR_SUCCESS ||
        disposition != REG_CREATED_NEW_KEY) {
        std::cerr << "Cannot create isolated test key.\n";
        key.handle = nullptr;
        return 1;
    }
    WallpaperSettings settings{L"C:\\wallpapers with spaces\\山と湖.png",
        {PanDirection::Down, 200, FitMode::Cover, 0.73, false}};
    Check(store.SaveApplied(settings, error), "save valid applied settings");
    Check(store.SaveTheme(UiTheme::Dark, error), "save theme independently");
    loaded = store.Load(error);
    Check(loaded.applied && loaded.applied->imagePath == settings.imagePath,
          "spaces and unicode round-trip");
    if (!loaded.applied) return 1;
    const auto& config = loaded.applied->configuration;
    Check(config.direction == PanDirection::Down && config.loopDurationSeconds == 200 &&
        config.fitMode == FitMode::Cover && config.position == 0.73 && !config.pauseWhenCovered,
        "configuration round-trip");
    Check(loaded.wallpaperEnabled && loaded.theme == UiTheme::Dark, "enabled and theme round-trip");
    Check(store.SaveEnabled(false, error), "save stopped preference");
    loaded = store.Load(error);
    Check(!loaded.wallpaperEnabled && loaded.applied->imagePath == settings.imagePath,
        "stop retains applied settings");
    Check(store.SaveTheme(UiTheme::Light, error), "light theme round-trip save");
    Check(store.Load(error).theme == UiTheme::Light, "light theme round-trip load");
    auto invalid = settings;
    invalid.configuration.loopDurationSeconds = 0;
    Check(!store.SaveApplied(invalid, error), "invalid save rejected");
    Check(!store.Load(error).wallpaperEnabled, "rejected save does not enable wallpaper");

    for (const auto direction : {PanDirection::Left, PanDirection::Right,
                                PanDirection::Up, PanDirection::Down}) {
        settings.configuration.direction = direction;
        Check(store.SaveApplied(settings, error) &&
            store.Load(error).applied->configuration.direction == direction,
            "each direction string round-trips");
    }
    settings.configuration.fitMode = FitMode::Pan;
    settings.configuration.position = 0;
    settings.configuration.pauseWhenCovered = true;
    Check(store.SaveApplied(settings, error), "save alternate settings");
    loaded = store.Load(error);
    Check(loaded.applied->configuration.fitMode == FitMode::Pan &&
        loaded.applied->configuration.position == 0 &&
        loaded.applied->configuration.pauseWhenCovered, "pan, position zero and pause enabled round-trip");

    String(key.handle, L"Direction", L"unknown");
    String(key.handle, L"Fit", L"unknown");
    Number(key.handle, L"DurationSeconds", 601);
    Number(key.handle, L"PositionPercent", 101);
    Number(key.handle, L"PauseWhenCovered", 2);
    Number(key.handle, L"WallpaperEnabled", 2);
    String(key.handle, L"Theme", L"unknown");
    loaded = store.Load(error);
    Check(loaded.applied->configuration.direction == PanDirection::Left, "invalid direction default");
    Check(loaded.applied->configuration.fitMode == FitMode::Pan, "invalid fit default");
    Check(loaded.applied->configuration.loopDurationSeconds == 90, "invalid duration default");
    Check(loaded.applied->configuration.position == 0.5, "invalid position default");
    Check(loaded.applied->configuration.pauseWhenCovered, "invalid pause default");
    Check(!loaded.wallpaperEnabled && loaded.theme == UiTheme::Light, "invalid enabled/theme defaults");

    String(key.handle, L"DurationSeconds", L"200");
    Number(key.handle, L"Direction", 2);
    RegDeleteValueW(key.handle, L"PositionPercent");
    loaded = store.Load(error);
    Check(loaded.applied->configuration.loopDurationSeconds == 90 &&
        loaded.applied->configuration.direction == PanDirection::Left &&
        loaded.applied->configuration.position == 0.5, "wrong types and missing fields default");
    for (const DWORD duration : {10U, 600U}) {
        Number(key.handle, L"DurationSeconds", duration);
        Check(store.Load(error).applied->configuration.loopDurationSeconds == duration,
            "duration endpoints accepted");
    }
    Number(key.handle, L"DurationSeconds", 9);
    Check(store.Load(error).applied->configuration.loopDurationSeconds == 90,
        "duration below minimum defaults");
    Number(key.handle, L"PositionPercent", 100);
    Check(store.Load(error).applied->configuration.position == 1, "position endpoint accepted");
    const BYTE shortNumber = 200;
    Check(RegSetValueExW(key.handle, L"DurationSeconds", 0, REG_DWORD,
        &shortNumber, sizeof(shortNumber)) == ERROR_SUCCESS, "write malformed short DWORD");
    Check(store.Load(error).applied->configuration.loopDurationSeconds == 90,
        "malformed DWORD size defaults");
    String(key.handle, L"ImagePath", std::wstring(L"valid\0trailing", 14));
    Check(!store.Load(error).applied, "embedded-null image path rejected");
    Number(key.handle, L"ImagePath", 1);
    Check(!store.Load(error).applied, "wrong image-path type rejected");
    Check(store.SaveApplied(settings, error), "restore valid test settings");
    Number(key.handle, L"SchemaVersion", 99);
    Check(!store.Load(error).applied, "future schema safely defaults");
    std::cout << (failures == 0 ? "All settings store tests passed.\n" : "Settings store tests failed.\n");
    return failures == 0 ? 0 : 1;
}
