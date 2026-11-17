#include "snowydesk/overlay/MainWindow.h"

#include <commctrl.h>
#include <shellapi.h>

#include "snowydesk/common/Log.h"
#include "snowydesk/i18n/I18n.h"

namespace snowydesk::overlay {
namespace {

constexpr wchar_t kMainClass[] = L"SnowyDeskMainWindow";
constexpr int kWindowWidth = 560;
constexpr int kWindowHeight = 380;
constexpr wchar_t kLicenseUrl[] = L"https://opensource.org/licenses/MIT";

HFONT CreateUiFont(int points, bool bold) {
    const HDC dc = GetDC(nullptr);
    const int height = -MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, dc);

    LOGFONTW logFont{};
    logFont.lfHeight = height;
    logFont.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    logFont.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(logFont.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&logFont);
}

}  // namespace

bool MainWindow::Create(HINSTANCE instance, CommandHandler handler) {
    instance_ = instance;
    handler_ = std::move(handler);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &MainWindow::WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kMainClass;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&windowClass)) {
        spdlog::error("RegisterClassEx failed for the main window");
        return false;
    }
    classRegistered_ = true;

    RECT rect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    hwnd_ = CreateWindowExW(0, kMainClass, i18n::Tr(i18n::Str::AppTitle),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                            rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        spdlog::error("CreateWindowEx failed for the main window");
        return false;
    }

    CreateControls();
    RefreshTexts();
    return true;
}

void MainWindow::Destroy() {
    if (titleFont_ != nullptr) {
        DeleteObject(titleFont_);
        titleFont_ = nullptr;
    }
    if (bodyFont_ != nullptr) {
        DeleteObject(bodyFont_);
        bodyFont_ = nullptr;
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kMainClass, instance_);
        classRegistered_ = false;
    }
}

void MainWindow::CreateControls() {
    titleFont_ = CreateUiFont(17, true);
    bodyFont_ = CreateUiFont(10, false);

    const DWORD staticStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;
    title_ = CreateWindowExW(0, L"STATIC", L"", staticStyle, 0, 0, 0, 0, hwnd_, nullptr,
                             instance_, nullptr);
    subtitle_ = CreateWindowExW(0, L"STATIC", L"", staticStyle, 0, 0, 0, 0, hwnd_, nullptr,
                                instance_, nullptr);
    description_ = CreateWindowExW(0, L"STATIC", L"", staticStyle | SS_EDITCONTROL, 0, 0, 0, 0,
                                   hwnd_, nullptr, instance_, nullptr);
    status_ = CreateWindowExW(0, L"STATIC", L"", staticStyle, 0, 0, 0, 0, hwnd_, nullptr,
                              instance_, nullptr);
    version_ = CreateWindowExW(0, L"STATIC", L"", staticStyle, 0, 0, 0, 0, hwnd_, nullptr,
                               instance_, nullptr);
    license_ = CreateWindowExW(0, WC_LINK, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                               hwnd_, nullptr, instance_, nullptr);
    clearButton_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdClearTaskbar)),
                                   instance_, nullptr);
    settingsButton_ = CreateWindowExW(0, L"BUTTON", L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0,
                                      0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdOpenSettings)),
                                      instance_, nullptr);

    SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    for (HWND control : {subtitle_, description_, status_, version_, license_, clearButton_,
                         settingsButton_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    LayoutControls(client.right - client.left, client.bottom - client.top);
}

void MainWindow::LayoutControls(int clientWidth, int clientHeight) {
    const int margin = 20;
    const int contentWidth = clientWidth - margin * 2;
    int y = margin;

    MoveWindow(title_, margin, y, contentWidth, 30, TRUE);
    y += 30;
    MoveWindow(subtitle_, margin, y, contentWidth, 20, TRUE);
    y += 30;
    MoveWindow(description_, margin, y, contentWidth, 70, TRUE);
    y += 62;
    MoveWindow(status_, margin, y, contentWidth, 20, TRUE);
    y += 34;

    const int buttonWidth = (contentWidth - 12) / 2;
    MoveWindow(clearButton_, margin, y, buttonWidth, 32, TRUE);
    MoveWindow(settingsButton_, margin + buttonWidth + 12, y, buttonWidth, 32, TRUE);
    y += 48;

    MoveWindow(version_, margin, y, 200, 20, TRUE);
    MoveWindow(license_, margin + 200, y, contentWidth - 200, 20, TRUE);

    (void)clientHeight;
}

void MainWindow::RefreshTexts() {
    if (hwnd_ == nullptr) return;

    SetWindowTextW(hwnd_, i18n::Tr(i18n::Str::AppTitle));
    SetWindowTextW(title_, i18n::Tr(i18n::Str::AppTitle));
    SetWindowTextW(subtitle_, i18n::Tr(i18n::Str::AppSubtitle));
    SetWindowTextW(description_, i18n::Tr(i18n::Str::MainDescription));
    SetWindowTextW(clearButton_, i18n::Tr(i18n::Str::BtnClearTaskbar));
    SetWindowTextW(settingsButton_, i18n::Tr(i18n::Str::BtnSettings));

    wchar_t versionText[128] = {};
    swprintf_s(versionText, L"%s 1.0.0", i18n::Tr(i18n::Str::VersionLabel));
    SetWindowTextW(version_, versionText);

    wchar_t licenseText[256] = {};
    swprintf_s(licenseText, L"<a href=\"%s\">%s</a>", kLicenseUrl,
               i18n::Tr(i18n::Str::LicenseLink));
    SetWindowTextW(license_, licenseText);

    SetServiceOnline(serviceOnline_);
}

void MainWindow::SetServiceOnline(bool online) {
    serviceOnline_ = online;
    if (status_ == nullptr) return;
    SetWindowTextW(status_, i18n::Tr(online ? i18n::Str::ServiceOnline
                                            : i18n::Str::ServiceOffline));
}

void MainWindow::Show() {
    if (hwnd_ == nullptr) return;
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void MainWindow::Hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool MainWindow::visible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void MainWindow::OpenLicense() {
    ShellExecuteW(nullptr, L"open", kLicenseUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_SIZE:
            LayoutControls(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (id == kCmdOpenSettings || id == kCmdClearTaskbar) {
                if (handler_) handler_(id);
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header != nullptr && header->code == NM_CLICK) {
                auto* link = reinterpret_cast<PNMLINK>(lparam);
                if (link != nullptr && link->item.szUrl[0] != L'\0') {
                    OpenLicense();
                    return 0;
                }
            }
            break;
        }
        case WM_CLOSE:
            Hide();
            return 0;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace snowydesk::overlay
