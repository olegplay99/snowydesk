#pragma once

// SnowyDeskService: a SERVICE_WIN32_OWN_PROCESS service running as LocalSystem.
//
// Why LocalSystem:
//   * it writes %ProgramData%\SnowyDesk\state.json before any user logs on;
//   * it can start the UI inside the interactive session at boot
//     (WTSQueryUserToken needs SYSTEM privileges) - the "service" autostart mode;
//   * it never renders anything, so running with high privileges is safe.
// The service does not draw, does not load user settings into the desktop and
// only listens on one named pipe restricted to SYSTEM / Administrators /
// Authenticated Users (see ipc/PipeServer.h).

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

#include "snowydesk/common/Types.h"
#include "snowydesk/ipc/PipeServer.h"
#include "snowydesk/ipc/Protocol.h"

namespace snowydesk::service {

class ServiceHost {
public:
    ServiceHost();
    ~ServiceHost();

    ServiceHost(const ServiceHost&) = delete;
    ServiceHost& operator=(const ServiceHost&) = delete;

    // Connects to the SCM and runs until stopped.
    int Run();

    // Same logic without the SCM (smoke tests, debugging). Blocks until the
    // user presses Enter, Ctrl+C is received, or `seconds` elapses (0 = wait
    // for a key). Returns a process exit code.
    int RunConsole(int seconds);

    static bool Install(std::string& error);
    static bool Uninstall(std::string& error);

private:
    static void WINAPI ServiceMainStatic(DWORD argc, LPWSTR* argv);
    static DWORD WINAPI HandlerExStatic(DWORD control, DWORD eventType, LPVOID data,
                                        LPVOID context);

    void OnStart();
    void OnStop();
    DWORD HandleControl(DWORD control);
    void SaveStateLocked();
    void CreateSaveTimer();
    void CloseSaveTimer();

    ipc::Response HandleRequest(const ipc::Request& request);
    void MaybeLaunchUi();

    ipc::NamedPipeServer server_;
    AppState state_;
    std::mutex stateMutex_;

    HANDLE stopEvent_ = nullptr;
    HANDLE saveTimer_ = nullptr;
    SERVICE_STATUS_HANDLE statusHandle_ = nullptr;
    SERVICE_STATUS status_{};
    std::atomic<bool> stopping_{false};
    bool consoleMode_ = false;
};

}  // namespace snowydesk::service
