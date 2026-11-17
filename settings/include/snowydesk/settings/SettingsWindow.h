#pragma once

// Settings window. Button semantics (documented in README):
//   Apply  - validate, apply live and persist, window stays open;
//   Save   - same as Apply, then close;
//   Cancel - restore the settings captured when the window was opened, apply
//            them again (so unsaved Apply clicks are rolled back) and close.

#include <windows.h>

#include <functional>

#include "snowydesk/common/Types.h"

namespace snowydesk::settings {

class SettingsWindow {
public:
    static constexpr int kCmdApply = 3020;
    static constexpr int kCmdSave = 3021;
    static constexpr int kCmdCancel = 3022;

    // Returns true when the settings were accepted (window stays in sync).
    using ApplyHandler = std::function<bool(const Settings& settings)>;

    bool Create(HINSTANCE instance, ApplyHandler handler);
    void Destroy();

    void Show(const Settings& current);
    void Hide();
    bool visible() const;

    void RefreshTexts();

    HWND hwnd() const { return hwnd_; }

private:
    enum ControlId {
        kLanguageCombo = 3001,
        kDensityLabel = 3002,
        kDensitySlider = 3003,
        kDensityEdit = 3004,
        kSnowSticksCheck = 3005,
        kGarlandCheck = 3006,
        kAutostartCheck = 3007,
        kAutostartServiceCheck = 3008,
        kTopmostCheck = 3009,
        kTopmostHint = 3010,
        kLanguageLabel = 3011,
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void CreateControls();
    void LayoutControls(int clientWidth, int clientHeight);

    Settings ReadFromControls() const;
    void WriteToControls(const Settings& settings);
    void SyncDensityEditFromSlider();
    void SyncSliderFromDensityEdit();
    void Apply(bool closeAfterApply);
    void Cancel();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    ApplyHandler handler_;

    HWND languageLabel_ = nullptr;
    HWND languageCombo_ = nullptr;
    HWND densityLabel_ = nullptr;
    HWND densitySlider_ = nullptr;
    HWND densityEdit_ = nullptr;
    HWND snowSticksCheck_ = nullptr;
    HWND garlandCheck_ = nullptr;
    HWND autostartCheck_ = nullptr;
    HWND autostartServiceCheck_ = nullptr;
    HWND topmostCheck_ = nullptr;
    HWND topmostHint_ = nullptr;
    HWND applyButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND cancelButton_ = nullptr;

    HFONT font_ = nullptr;
    Settings pristine_;   // captured on Show(), restored by Cancel
    bool updatingControls_ = false;
    bool classRegistered_ = false;
};

}  // namespace snowydesk::settings
