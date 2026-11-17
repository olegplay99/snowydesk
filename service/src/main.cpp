#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "snowydesk/common/Log.h"
#include "snowydesk/common/Paths.h"
#include "snowydesk/service/ServiceHost.h"

namespace {

void PrintUsage() {
    std::wprintf(
        L"SnowyDeskService 1.0.0\n"
        L"  (no arguments)      run as a Windows service (started by the SCM)\n"
        L"  --install           create the service (elevated prompt required)\n"
        L"  --uninstall         stop and delete the service (elevated prompt required)\n"
        L"  --console [SECONDS] run in the foreground; stop on key press or after SECONDS\n"
        L"  --help              show this text\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring command = argc > 1 ? argv[1] : L"";

    if (command == L"--help" || command == L"-h" || command == L"/?") {
        PrintUsage();
        return 0;
    }

    if (command == L"--install" || command == L"--uninstall") {
        snowydesk::log::Init(snowydesk::paths::ServiceLogPath(), true);
        std::string error;
        const bool ok = command == L"--install"
                            ? snowydesk::service::ServiceHost::Install(error)
                            : snowydesk::service::ServiceHost::Uninstall(error);
        if (!ok) {
            std::printf("error: %s\n", error.c_str());
            return 1;
        }
        std::printf("%s\n", command == L"--install" ? "service installed" : "service removed");
        return 0;
    }

    if (command == L"--console") {
        int seconds = 0;
        if (argc > 2) {
            seconds = _wtoi(argv[2]);
        }
        snowydesk::log::Init(snowydesk::paths::ServiceLogPath(), true);
        snowydesk::service::ServiceHost host;
        return host.RunConsole(seconds);
    }

    // Normal service start: no console, file logging only.
    snowydesk::log::Init(snowydesk::paths::ServiceLogPath(), false);
    snowydesk::service::ServiceHost host;
    return host.Run();
}
