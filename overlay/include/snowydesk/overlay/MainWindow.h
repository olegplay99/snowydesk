#pragma once

// The small "about / status" window: description, clear-taskbar button,
// settings button, version and the MIT license link.

#include <windows.h>

#include <functional>

#include "snowydesk/common/Types.h"

namespace snowydesk::overlay {

class MainWindow {
public:
    static constexpr int kCmdOpenSettings = 2001;
    static constexpr int kCmdClearTaskbar = 2002;

    using CommandHandler = std::function<void(int commandId)>;

    bool Create(HINSTANCE instance, CommandHandler handler);
    void Destroy();

    void Show();
    void Hide();
    bool visible() const;

    // Re-applies every localized string (language switch is live).
    void RefreshTexts();

    void SetServiceOnline(bool online);

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void CreateControls();
    void LayoutControls(int clientWidth, int clientHeight);
    void OpenLicense();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    CommandHandler handler_;

    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND description_ = nullptr;
    HWND status_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND settingsButton_ = nullptr;
    HWND version_ = nullptr;
    HWND license_ = nullptr;

    HFONT titleFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    bool serviceOnline_ = false;
    bool classRegistered_ = false;
};

}  // namespace snowydesk::overlay
