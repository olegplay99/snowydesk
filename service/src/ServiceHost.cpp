#include "snowydesk/service/ServiceHost.h"

#include <conio.h>

#include <chrono>
#include <exception>
#include <thread>
#include <vector>

#include "snowydesk/common/Json.h"
#include "snowydesk/common/Log.h"
#include "snowydesk/common/Paths.h"
#include "snowydesk/common/StateStore.h"
#include "snowydesk/service/SessionLaunch.h"

namespace snowydesk::service {
namespace {

constexpr wchar_t kServiceName[] = L"SnowyDesk";
constexpr wchar_t kServiceDisplayName[] = L"SnowyDesk state service";
constexpr wchar_t kServiceDescription[] =
    L"Stores SnowyDesk settings and snow state in %ProgramData%\\SnowyDesk\\state.json and "
    L"serves them over the named pipe \\\\.\\pipe\\SnowyDesk.";

// The service persists state every 30 seconds as required by the spec.
constexpr LONGLONG kSaveInterval100ns = -30ll * 10'000'000ll;

ServiceHost* g_host = nullptr;

}  // namespace

ServiceHost::ServiceHost()
    : server_(paths::PipeName(),
              [this](const ipc::Request& request) { return HandleRequest(request); }) {}

ServiceHost::~ServiceHost() {
    CloseSaveTimer();
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

int ServiceHost::Run() {
    g_host = this;
    consoleMode_ = false;

    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(kServiceName), &ServiceHost::ServiceMainStatic},
        {nullptr, nullptr},
    };

    if (!StartServiceCtrlDispatcherW(table)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            spdlog::warn("not started by the service control manager; use --console for a foreground run");
        } else {
            spdlog::error("StartServiceCtrlDispatcher failed with error {}", error);
        }
        return 1;
    }
    return 0;
}

void WINAPI ServiceHost::ServiceMainStatic(DWORD argc, LPWSTR* argv) {
    (void)argc;
    (void)argv;
    if (g_host == nullptr) return;

    g_host->statusHandle_ =
        RegisterServiceCtrlHandlerExW(kServiceName, &ServiceHost::HandlerExStatic, g_host);
    if (g_host->statusHandle_ == nullptr) {
        spdlog::error("RegisterServiceCtrlHandlerEx failed with error {}", GetLastError());
        return;
    }

    g_host->status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_host->status_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    g_host->status_.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_host->statusHandle_, &g_host->status_);

    g_host->OnStart();

    g_host->status_.dwCurrentState = SERVICE_RUNNING;
    g_host->status_.dwControlsAccepted =
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PRESHUTDOWN;
    SetServiceStatus(g_host->statusHandle_, &g_host->status_);

    HANDLE handles[2] = {g_host->stopEvent_, g_host->saveTimer_};
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 1) {
            std::lock_guard<std::mutex> lock(g_host->stateMutex_);
            g_host->SaveStateLocked();
            continue;
        }
        break;  // stop event or an unexpected wait failure
    }

    g_host->OnStop();

    g_host->status_.dwCurrentState = SERVICE_STOPPED;
    g_host->status_.dwWin32ExitCode = 0;
    SetServiceStatus(g_host->statusHandle_, &g_host->status_);
}

DWORD WINAPI ServiceHost::HandlerExStatic(DWORD control, DWORD eventType, LPVOID data,
                                          LPVOID context) {
    (void)eventType;
    (void)data;
    auto* host = static_cast<ServiceHost*>(context);
    if (host == nullptr) {
        host = g_host;
    }
    return host != nullptr ? host->HandleControl(control) : ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD ServiceHost::HandleControl(DWORD control) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_PRESHUTDOWN:
            if (!stopping_.exchange(true)) {
                status_.dwCurrentState = SERVICE_STOP_PENDING;
                status_.dwWaitHint = 5000;
                SetServiceStatus(statusHandle_, &status_);
                if (stopEvent_ != nullptr) {
                    SetEvent(stopEvent_);
                }
            }
            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(statusHandle_, &status_);
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

int ServiceHost::RunConsole(int seconds) {
    consoleMode_ = true;
    spdlog::info("SnowyDeskService console mode ({} s)", seconds > 0 ? seconds : -1);

    OnStart();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    for (;;) {
        HANDLE handles[2] = {stopEvent_, saveTimer_};
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 200);
        if (wait == WAIT_OBJECT_0 + 1) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            SaveStateLocked();
            continue;
        }
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (seconds > 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
        } else if (_kbhit()) {
            _getch();
            break;
        }
    }

    OnStop();
    spdlog::info("SnowyDeskService console mode finished");
    return 0;
}

void ServiceHost::CreateSaveTimer() {
    saveTimer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (saveTimer_ == nullptr) {
        spdlog::error("CreateWaitableTimerEx failed with error {}", GetLastError());
        return;
    }
    LARGE_INTEGER due{};
    due.QuadPart = kSaveInterval100ns;
    if (!SetWaitableTimer(saveTimer_, &due, 30'000, nullptr, nullptr, FALSE)) {
        spdlog::error("SetWaitableTimer failed with error {}", GetLastError());
    }
}

void ServiceHost::CloseSaveTimer() {
    if (saveTimer_ != nullptr) {
        CancelWaitableTimer(saveTimer_);
        CloseHandle(saveTimer_);
        saveTimer_ = nullptr;
    }
}

void ServiceHost::OnStart() {
    if (!paths::EnsureDirectory(paths::ProgramDataDir())) {
        spdlog::error("cannot create {}", paths::ProgramDataDir().string());
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CreateSaveTimer();

    const auto loaded = state_store::Load(paths::StatePath());
    state_ = loaded.state;
    if (loaded.recoveredFromCorrupt) {
        spdlog::warn("state.json was corrupt ({}); defaults restored", loaded.error);
        std::lock_guard<std::mutex> lock(stateMutex_);
        SaveStateLocked();
    } else if (!loaded.existed) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        SaveStateLocked();
    }

    std::string error;
    if (!server_.Start(error)) {
        spdlog::error("IPC server failed to start: {}", error);
    }

    if (state_.settings.autostart && state_.settings.autostartMode == AutostartMode::Service) {
        MaybeLaunchUi();
    }

    spdlog::info("SnowyDeskService started");
}

void ServiceHost::MaybeLaunchUi() {
    std::string error;
    if (!LaunchUiInActiveSession(error)) {
        spdlog::warn("UI was not launched at start: {}", error);
    }
}

void ServiceHost::OnStop() {
    server_.Stop();
    CloseSaveTimer();

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        SaveStateLocked();
    }

    if (stopEvent_ != nullptr) {
        SetEvent(stopEvent_);
    }
    spdlog::info("SnowyDeskService stopped");
}

void ServiceHost::SaveStateLocked() {
    state_.settings = SanitizeSettings(state_.settings);
    std::string error;
    if (!state_store::Save(paths::StatePath(), state_, error)) {
        spdlog::error("failed to save state.json: {}", error);
    }
}

ipc::Response ServiceHost::HandleRequest(const ipc::Request& request) {
    ipc::Response response;
    response.id = request.id;
    response.ok = true;

    switch (request.type) {
        case ipc::MessageType::Ping:
            response.payload = {
                {"pong", true},
                {"service", "SnowyDeskService"},
                {"protocol", kProtocolVersion},
                {"schema", kStateSchemaVersion},
            };
            return response;

        case ipc::MessageType::GetState: {
            std::lock_guard<std::mutex> lock(stateMutex_);
            response.payload = {{"state", StateToJson(state_)}};
            return response;
        }

        case ipc::MessageType::SetState: {
            std::string error;
            const auto incoming = ipc::ReadStatePayload(request.payload, error);
            if (!incoming) {
                response.ok = false;
                response.error = error;
                return response;
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_.settings = SanitizeSettings(incoming->settings);
            state_.particles = incoming->particles;
            SaveStateLocked();
            response.payload = {{"saved", true},
                                {"particles", state_.particles.size()}};
            return response;
        }

        case ipc::MessageType::ApplySettings: {
            std::string error;
            const auto incoming = ipc::ReadSettingsPayload(request.payload, error);
            if (!incoming) {
                response.ok = false;
                response.error = error;
                return response;
            }
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_.settings = SanitizeSettings(*incoming);
            SaveStateLocked();
            response.payload = {{"saved", true},
                                {"settings", SettingsToJson(state_.settings)}};
            return response;
        }

        case ipc::MessageType::ClearTaskbarSnow: {
            std::lock_guard<std::mutex> lock(stateMutex_);
            const auto before = state_.particles.size();
            std::erase_if(state_.particles, [](const Particle& particle) {
                return particle.state == ParticleState::OnTaskbar || particle.anchor.edge == 3;
            });
            SaveStateLocked();
            response.payload = {{"removed", before - state_.particles.size()}};
            return response;
        }

        case ipc::MessageType::Unknown:
            break;
    }

    response.ok = false;
    response.error = "unsupported message type";
    return response;
}

bool ServiceHost::Install(std::string& error) {
    wchar_t modulePath[MAX_PATH * 2] = {};
    const DWORD length =
        GetModuleFileNameW(nullptr, modulePath, static_cast<DWORD>(MAX_PATH * 2));
    if (length == 0) {
        error = "GetModuleFileName failed";
        return false;
    }
    const std::wstring command = L"\"" + std::wstring(modulePath, length) + L"\"";

    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (manager == nullptr) {
        error = "OpenSCManager failed with error " + std::to_string(GetLastError()) +
                " (run from an elevated prompt)";
        return false;
    }

    SC_HANDLE service =
        CreateServiceW(manager, kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
                       SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                       command.c_str(), nullptr, nullptr, nullptr, L"LocalSystem", nullptr);

    if (service == nullptr) {
        const DWORD createError = GetLastError();
        if (createError == ERROR_SERVICE_EXISTS) {
            service = OpenServiceW(manager, kServiceName, SERVICE_ALL_ACCESS);
            if (service != nullptr) {
                if (!ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_AUTO_START,
                                          SERVICE_NO_CHANGE, command.c_str(), nullptr, nullptr,
                                          nullptr, nullptr, nullptr, kServiceDisplayName)) {
                    error = "ChangeServiceConfig failed with error " +
                            std::to_string(GetLastError());
                    CloseServiceHandle(service);
                    CloseServiceHandle(manager);
                    return false;
                }
            }
        }
        if (service == nullptr) {
            error = "CreateService failed with error " + std::to_string(createError);
            CloseServiceHandle(manager);
            return false;
        }
    }

    SERVICE_DESCRIPTIONW description{};
    description.lpDescription = const_cast<LPWSTR>(kServiceDescription);
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);

    if (!StartServiceW(service, 0, nullptr)) {
        const DWORD startError = GetLastError();
        if (startError != ERROR_SERVICE_ALREADY_RUNNING) {
            spdlog::warn("StartService failed with error {}", startError);
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    spdlog::info("service 'SnowyDesk' installed");
    return true;
}

bool ServiceHost::Uninstall(std::string& error) {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager == nullptr) {
        error = "OpenSCManager failed with error " + std::to_string(GetLastError()) +
                " (run from an elevated prompt)";
        return false;
    }

    SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        const DWORD openError = GetLastError();
        CloseServiceHandle(manager);
        if (openError == ERROR_SERVICE_DOES_NOT_EXIST) {
            return true;  // nothing to remove
        }
        error = "OpenService failed with error " + std::to_string(openError);
        return false;
    }

    SERVICE_STATUS status{};
    ControlService(service, SERVICE_CONTROL_STOP, &status);
    for (int attempt = 0; attempt < 50; ++attempt) {
        SERVICE_STATUS_PROCESS processStatus{};
        DWORD needed = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<LPBYTE>(&processStatus),
                                  sizeof(processStatus), &needed)) {
            break;
        }
        if (processStatus.dwCurrentState == SERVICE_STOPPED) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!DeleteService(service)) {
        error = "DeleteService failed with error " + std::to_string(GetLastError());
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return true;
}

}  // namespace snowydesk::service
