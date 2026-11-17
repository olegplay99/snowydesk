#pragma once

// The UI process: owns the tray icon, the main/settings windows, the snow
// simulation and the overlay. Talks to SnowyDeskService over the named pipe.

#include <windows.h>

#include <chrono>
#include <string>

#include "snowydesk/common/Types.h"
#include "snowydesk/garland/Garland.h"
#include "snowydesk/ipc/PipeClient.h"
#include "snowydesk/overlay/MainWindow.h"
#include "snowydesk/overlay/OverlayHost.h"
#include "snowydesk/overlay/TrayIcon.h"
#include "snowydesk/settings/SettingsWindow.h"
#include "snowydesk/snow/SnowWorld.h"

namespace snowydesk::overlay {

class App {
public:
    int Run(HINSTANCE instance);

private:
    static constexpr UINT_PTR kFrameTimerId = 1;  // fallback timer id
    static constexpr long long kFrameInterval100ns = 166667;  // ~60 FPS
    static constexpr UINT kPollIntervalMs = 100;  // window tracking at 10 Hz
    static constexpr UINT kTopmostIntervalMs = 1000;
    static constexpr UINT kServicePingIntervalMs = 5000;
    static constexpr UINT kStateSaveIntervalMs = 10000;

    bool Initialize();
    void Shutdown();

    static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void OnFrame();
    void OnCommand(int commandId);
    void OnDisplayChange();

    void LoadStateFromService(AppState& state, bool& serviceOnline);
    void SaveStateToService();
    bool ApplySettings(const Settings& settings);
    void RefreshAllTexts();
    void UpdateAutostart(const Settings& settings);
    void ClearTaskbarSnow();
    void ShowAbout();
    void ShowStatus();

    HINSTANCE instance_ = nullptr;
    HWND messageWindow_ = nullptr;
    bool classRegistered_ = false;
    HANDLE singleInstanceMutex_ = nullptr;
    bool duplicateInstance_ = false;

    OverlayHost overlay_;
    snow::SnowWorld snow_;
    garland::Garland garland_;
    ipc::NamedPipeClient client_{L"\\\\.\\pipe\\SnowyDesk"};

    MainWindow mainWindow_;
    settings::SettingsWindow settingsWindow_;
    TrayIcon tray_;

    Settings settings_{};
    AppState pendingState_{};  // particles + settings handed to the service

    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point lastPoll_{};
    std::chrono::steady_clock::time_point lastTopmost_{};
    std::chrono::steady_clock::time_point lastPing_{};
    std::chrono::steady_clock::time_point lastSave_{};

    bool serviceOnline_ = false;
    bool initialized_ = false;
    int frameCount_ = 0;
    bool fpsLogged_ = false;
    std::chrono::steady_clock::time_point loopStart_{};
    double totalFrameMs_ = 0.0;
    double totalRenderMs_ = 0.0;
};

}  // namespace snowydesk::overlay
