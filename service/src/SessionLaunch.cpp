#include "snowydesk/service/SessionLaunch.h"

#include <windows.h>
#include <userenv.h>
#include <wtsapi32.h>

#include <string>

#include "snowydesk/common/Log.h"

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")

namespace snowydesk::service {
namespace {

std::wstring UiExecutablePath() {
    wchar_t servicePath[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, servicePath,
                                            static_cast<DWORD>(MAX_PATH * 2));
    if (length == 0) {
        return {};
    }

    std::wstring path(servicePath, length);
    const auto slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, slash + 1) + L"SnowyDeskUI.exe";
}

}  // namespace

bool LaunchUiInActiveSession(std::string& error) {
    const DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) {
        error = "no active console session";
        return false;
    }

    HANDLE userToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &userToken)) {
        error = "WTSQueryUserToken failed with error " + std::to_string(GetLastError());
        return false;
    }

    const std::wstring exe = UiExecutablePath();
    if (exe.empty()) {
        CloseHandle(userToken);
        error = "cannot resolve the UI executable path";
        return false;
    }

    void* environment = nullptr;
    CreateEnvironmentBlock(&environment, userToken, FALSE);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = const_cast<LPWSTR>(L"winsta0\\default");
    PROCESS_INFORMATION process{};

    std::wstring commandLine = L"\"" + exe + L"\"";

    const BOOL created = CreateProcessAsUserW(
        userToken, exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, environment, nullptr, &startup, &process);

    const DWORD lastError = created ? ERROR_SUCCESS : GetLastError();

    if (environment != nullptr) {
        DestroyEnvironmentBlock(environment);
    }
    CloseHandle(userToken);

    if (!created) {
        error = "CreateProcessAsUser failed with error " + std::to_string(lastError);
        return false;
    }

    spdlog::info("UI launched in session {} (pid {})", sessionId, process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

}  // namespace snowydesk::service
