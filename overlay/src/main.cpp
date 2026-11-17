#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <timeapi.h>

#include "snowydesk/overlay/App.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine,
                      int showCommand) {
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    // The snow must not be blurry on scaled displays.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ~1 ms timer resolution keeps the 60 FPS loop honest.
    timeBeginPeriod(1);

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_LINK_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    snowydesk::overlay::App app;
    const int exitCode = app.Run(instance);

    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    timeEndPeriod(1);
    return exitCode;
}
