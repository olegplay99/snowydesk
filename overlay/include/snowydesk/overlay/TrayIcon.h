#pragma once

// Tray icon and its popup menu: Settings / Clear snow / About / Exit.

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

namespace snowydesk::overlay {

class TrayIcon {
public:
    // Menu commands, forwarded to the handler.
    static constexpr int kCmdSettings = 1001;
    static constexpr int kCmdClearSnow = 1002;
    static constexpr int kCmdAbout = 1003;
    static constexpr int kCmdExit = 1004;
    static constexpr int kCmdShowMainWindow = 1005;

    // Sent to the owner window when the user interacts with the icon.
    static constexpr UINT kCallbackMessage = WM_APP + 41;

    using CommandHandler = std::function<void(int commandId)>;

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HWND owner, CommandHandler handler);
    void Destroy();

    // Rebuilds the tooltip after a language change.
    void Refresh();

    // Handles kCallbackMessage; returns true when the message was consumed.
    bool HandleCallback(UINT message, WPARAM wparam, LPARAM lparam);

private:
    void ShowContextMenu();

    NOTIFYICONDATAW data_{};
    CommandHandler handler_;
    HICON icon_ = nullptr;
    bool iconBorrowed_ = false;
    bool created_ = false;
};

}  // namespace snowydesk::overlay
