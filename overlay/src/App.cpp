#include "snowydesk/overlay/App.h"

#include <commctrl.h>

#include <sstream>

#include "snowydesk/common/Json.h"
#include "snowydesk/common/Log.h"
#include "snowydesk/common/Paths.h"
#include "snowydesk/common/StateStore.h"
#include "snowydesk/common/Types.h"
#include "snowydesk/i18n/I18n.h"
#include "snowydesk/snow/SnowTuning.h"

#include <cmath>

namespace snowydesk::overlay {
namespace {

constexpr wchar_t kMessageClass[] = L"SnowyDeskMessageWindow";
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunKeyValue[] = L"SnowyDeskUI";

bool SetRunKey(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enable) {
        wchar_t path[MAX_PATH * 2] = {};
        if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(MAX_PATH * 2)) > 0) {
            std::wstring command = L"\"" + std::wstring(path) + L"\"";
            ok = RegSetValueExW(key, kRunKeyValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) ==
                 ERROR_SUCCESS;
        }
    } else {
        const LSTATUS status = RegDeleteValueW(key, kRunKeyValue);
        ok = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    RegCloseKey(key);
    return ok;
}

}  // namespace

int App::Run(HINSTANCE instance) {
    instance_ = instance;
    if (!Initialize()) {
        // A duplicate copy is not a failure: it just brought up the running one.
        return duplicateInstance_ ? 0 : 1;
    }

    // A high-resolution waitable timer drives the animation instead of WM_TIMER:
    // coalesced timer messages were the reason the fall looked steppy.
    HANDLE frameTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                               CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                               TIMER_ALL_ACCESS);

    auto armFrameTimer = [frameTimer]() {
        if (frameTimer == nullptr) return;
        LARGE_INTEGER due{};
        due.QuadPart = -kFrameInterval100ns;
        SetWaitableTimer(frameTimer, &due, 0, nullptr, nullptr, FALSE);
    };

    int exitCode = 0;
    MSG message{};

    if (frameTimer == nullptr) {
        // Fallback: classic timer pump, still animated.
        spdlog::warn("high-resolution timer unavailable, falling back to WM_TIMER");
        SetTimer(messageWindow_, kFrameTimerId, 16, nullptr);
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        exitCode = static_cast<int>(message.wParam);
    } else {
        armFrameTimer();
        for (;;) {
            const DWORD wait =
                MsgWaitForMultipleObjects(1, &frameTimer, FALSE, INFINITE, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) {
                // Re-arm first: arming after rendering made the period
                // (interval + frame time) instead of the interval itself.
                armFrameTimer();
                OnFrame();
                continue;
            }
            if (wait != WAIT_OBJECT_0 + 1) {
                exitCode = 1;
                break;
            }

            bool quitting = false;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    exitCode = static_cast<int>(message.wParam);
                    quitting = true;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (quitting) break;
        }
        CancelWaitableTimer(frameTimer);
        CloseHandle(frameTimer);
    }

    Shutdown();
    return exitCode;
}

bool App::Initialize() {
    if (!paths::EnsureDirectory(paths::LocalAppDataDir())) {
        // Logging to %ProgramData% is not allowed for a plain user; keep going.
    }
    log::Init(paths::UiLogPath(), false);
    spdlog::info("SnowyDeskUI starting");

    // One overlay only: a second copy paints over the first, splits the timer
    // between them and halves the frame rate. Bring up the existing window
    // instead of starting another instance.
    singleInstanceMutex_ =
        CreateMutexW(nullptr, TRUE, L"Local\\SnowyDeskUI.SingleInstance");
    if (singleInstanceMutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        spdlog::warn("another SnowyDeskUI instance is already running; focusing it");
        if (HWND existing = FindWindowW(L"SnowyDeskMainWindow", nullptr)) {
            ShowWindow(existing, SW_SHOWNORMAL);
            SetForegroundWindow(existing);
        }
        duplicateInstance_ = true;
        return false;
    }

    // Hidden top-level window: receives tray callbacks and the frame timer.
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &App::MessageWndProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kMessageClass;
    if (!RegisterClassExW(&windowClass)) {
        spdlog::error("cannot register the message window class");
        return false;
    }
    classRegistered_ = true;

    messageWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMessageClass, L"SnowyDesk",
                                     WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (messageWindow_ == nullptr) {
        spdlog::error("cannot create the message window");
        return false;
    }

    std::string overlayError;
    if (!overlay_.Initialize(instance_, overlayError)) {
        spdlog::error("overlay initialization failed: {}", overlayError);
        return false;
    }

    snow_.SetVirtualRect(overlay_.VirtualRect());

    AppState state;
    bool serviceOnline = false;
    LoadStateFromService(state, serviceOnline);
    serviceOnline_ = serviceOnline;
    settings_ = state.settings;
    i18n::SetLanguage(settings_.language);

    snow_.SetSettings(settings_);
    snow_.AdoptState(state);
    if (snow_.particles().empty() && settings_.density > 0) {
        snow_.Reseed(settings_.density);
    }

    garland_.SetEnabled(settings_.garlandEnabled);
    garland_.Rebuild(overlay_.GarlandBands());

    if (!mainWindow_.Create(instance_, [this](int command) { OnCommand(command); })) {
        return false;
    }
    if (!settingsWindow_.Create(instance_,
                                [this](const Settings& settings) { return ApplySettings(settings); })) {
        return false;
    }
    if (tray_.Create(messageWindow_, [this](int command) { OnCommand(command); })) {
        spdlog::info("tray icon created");
    } else {
        spdlog::warn("tray icon could not be created (explorer may be restarting)");
    }

    RefreshAllTexts();

    // The main window is the visible entry point: the tray icon alone is easy
    // to miss (it can live in the overflow area).
    mainWindow_.Show();
    spdlog::info("main window shown (visible: {})", mainWindow_.visible() ? "yes" : "no");

    lastFrame_ = std::chrono::steady_clock::now();
    loopStart_ = lastFrame_;
    lastPoll_ = lastFrame_;
    lastTopmost_ = lastFrame_;
    lastPing_ = lastFrame_;
    lastSave_ = lastFrame_;

    initialized_ = true;
    spdlog::info("SnowyDeskUI initialized (service online: {})", serviceOnline_ ? "yes" : "no");
    return true;
}

void App::Shutdown() {
    if (initialized_) {
        SaveStateToService();
    }
    KillTimer(messageWindow_, kFrameTimerId);
    tray_.Destroy();
    settingsWindow_.Destroy();
    mainWindow_.Destroy();
    overlay_.Shutdown();

    if (messageWindow_ != nullptr) {
        DestroyWindow(messageWindow_);
        messageWindow_ = nullptr;
    }
    if (classRegistered_) {
        UnregisterClassW(kMessageClass, instance_);
        classRegistered_ = false;
    }
    if (singleInstanceMutex_ != nullptr) {
        CloseHandle(singleInstanceMutex_);
        singleInstanceMutex_ = nullptr;
    }
    spdlog::info("SnowyDeskUI stopped");
}

void App::LoadStateFromService(AppState& state, bool& serviceOnline) {
    serviceOnline = false;

    ipc::Response response;
    std::string error;
    if (client_.Call(ipc::MakeGetState(1), response, error, 800)) {
        serviceOnline = true;
        if (response.payload.contains("state")) {
            std::string parseError;
            AppState parsed;
            if (StateFromJson(response.payload.at("state"), parsed, parseError)) {
                state = std::move(parsed);
                return;
            }
            spdlog::warn("service returned an invalid state: {}", parseError);
        }
        return;
    }

    spdlog::warn("service is not reachable ({}); falling back to a read-only state.json load",
                 error);
    const auto loaded = state_store::Load(paths::StatePath());
    if (loaded.existed) {
        state = loaded.state;
    }
}

LRESULT CALLBACK App::MessageWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT App::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (tray_.HandleCallback(message, wparam, lparam)) {
        return 0;
    }

    switch (message) {
        case WM_TIMER:
            // Only used by the fallback pump when no high-resolution timer exists.
            if (wparam == kFrameTimerId) {
                OnFrame();
                return 0;
            }
            break;

        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            OnDisplayChange();
            return 0;

        case WM_ENDSESSION:
            SaveStateToService();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void App::OnFrame() {
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastFrame_).count();
    lastFrame_ = now;
    if (dt <= 0.0f || dt > 0.25f) {
        dt = tuning::kFrameSeconds;
    }

    // One-time diagnostic: the measured frame rate of the first five seconds.
    ++frameCount_;
    if (!fpsLogged_ && std::chrono::duration<double>(now - loopStart_).count() >= 5.0) {
        fpsLogged_ = true;
        const double seconds = std::chrono::duration<double>(now - loopStart_).count();
        spdlog::info("render loop: {:.1f} fps, frame {:.2f} ms, render {:.2f} ms",
                     frameCount_ / seconds, totalFrameMs_ / frameCount_,
                     totalRenderMs_ / frameCount_);
    }

    // Window tracking at 10 Hz.
    if (now - lastPoll_ >= std::chrono::milliseconds(kPollIntervalMs)) {
        lastPoll_ = now;
        snow_.PollWindows();
    }

    // Keep the overlay above borderless windows/games that went fullscreen.
    if (now - lastTopmost_ >= std::chrono::milliseconds(kTopmostIntervalMs)) {
        lastTopmost_ = now;
        overlay_.ReassertTopmost();
    }

    // Service liveness for the status line.
    if (now - lastPing_ >= std::chrono::milliseconds(kServicePingIntervalMs)) {
        lastPing_ = now;
        const bool online = client_.Ping(250);
        if (online != serviceOnline_) {
            serviceOnline_ = online;
            mainWindow_.SetServiceOnline(online);
        }
    }

    // Persist the particle field so a reboot continues the same snow.
    if (now - lastSave_ >= std::chrono::milliseconds(kStateSaveIntervalMs)) {
        lastSave_ = now;
        SaveStateToService();
    }

    snow_.Tick(dt);
    garland_.Update(dt);

    const auto renderStart = std::chrono::steady_clock::now();
    overlay_.Render(snow_.particles(), garland_);
    const double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
    const double renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart).count();
    totalFrameMs_ += frameMs;
    totalRenderMs_ += renderMs;
}

void App::OnDisplayChange() {
    if (overlay_.RefreshMonitors()) {
        snow_.SetVirtualRect(overlay_.VirtualRect());
    }
    garland_.Rebuild(overlay_.GarlandBands());
    spdlog::info("display layout changed, overlay refreshed");
}

void App::OnCommand(int commandId) {
    using tray_cmd = TrayIcon;
    switch (commandId) {
        case tray_cmd::kCmdSettings:
        case MainWindow::kCmdOpenSettings:
            settingsWindow_.Show(settings_);
            break;

        case tray_cmd::kCmdClearSnow:
        case MainWindow::kCmdClearTaskbar:
            ClearTaskbarSnow();
            break;

        case tray_cmd::kCmdAbout:
            ShowAbout();
            break;

        case tray_cmd::kCmdShowMainWindow:
            mainWindow_.Show();
            break;

        case tray_cmd::kCmdExit:
            SaveStateToService();
            PostQuitMessage(0);
            break;

        default:
            break;
    }
}

void App::ClearTaskbarSnow() {
    snow_.ClearTaskbarSnow();

    ipc::Response response;
    std::string error;
    client_.Call(ipc::MakeClearTaskbarSnow(2), response, error, 500);
    SaveStateToService();

    ShowStatus();
}

void App::ShowStatus() {
    MessageBoxW(messageWindow_, i18n::Tr(i18n::Str::ClearDone), i18n::Tr(i18n::Str::AppTitle),
                MB_OK | MB_ICONINFORMATION);
}

void App::ShowAbout() {
    MessageBoxW(mainWindow_.hwnd(), i18n::Tr(i18n::Str::AboutText),
                i18n::Tr(i18n::Str::AboutTitle), MB_OK | MB_ICONINFORMATION);
}

void App::RefreshAllTexts() {
    mainWindow_.RefreshTexts();
    settingsWindow_.RefreshTexts();
    tray_.Refresh();
}

bool App::ApplySettings(const Settings& settings) {
    Settings next = SanitizeSettings(settings);
    const std::string validation = ValidateSettings(next);
    if (!validation.empty()) {
        spdlog::warn("rejected settings: {}", validation);
        return false;
    }

    const bool languageChanged = next.language != settings_.language;
    settings_ = next;
    (void)languageChanged;

    i18n::SetLanguage(settings_.language);
    snow_.SetSettings(settings_);
    if (settings_.garlandEnabled != garland_.enabled()) {
        garland_.SetEnabled(settings_.garlandEnabled);
    }
    garland_.Rebuild(overlay_.GarlandBands());
    UpdateAutostart(settings_);

    // Language switch is live: every window re-reads its strings immediately.
    RefreshAllTexts();

    // Persist through the service (single writer of state.json).
    pendingState_ = snow_.ToState();
    pendingState_.settings = settings_;

    ipc::Response response;
    std::string error;
    const bool applied = client_.Call(ipc::MakeApplySettings(3, settings_), response, error, 800);
    if (!applied) {
        spdlog::warn("apply_settings failed: {}", error);
        serviceOnline_ = false;
        mainWindow_.SetServiceOnline(false);

        // Never report success when nothing reached the disk: the settings
        // window stays open and the user is told what to do.
        MessageBoxW(settingsWindow_.hwnd(), i18n::Tr(i18n::Str::StatusServiceHint),
                    i18n::Tr(i18n::Str::StatusSaveFailed), MB_OK | MB_ICONWARNING);
        return false;
    }

    serviceOnline_ = true;
    mainWindow_.SetServiceOnline(true);
    SaveStateToService();
    return true;
}

void App::UpdateAutostart(const Settings& settings) {
    // "Run key" mode is handled here; "service" mode is handled by the service
    // (it launches the UI inside the active console session at boot).
    if (settings.autostart && settings.autostartMode == AutostartMode::UserRunKey) {
        if (!SetRunKey(true)) {
            spdlog::warn("could not write the HKCU Run key");
        }
    } else {
        SetRunKey(false);
    }
}

void App::SaveStateToService() {
    pendingState_ = snow_.ToState();
    pendingState_.settings = settings_;

    ipc::Response response;
    std::string error;
    if (!client_.Call(ipc::MakeSetState(4, pendingState_), response, error, 800)) {
        spdlog::debug("set_state failed: {}", error);
    }
}

}  // namespace snowydesk::overlay
