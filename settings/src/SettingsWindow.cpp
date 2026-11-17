#include "snowydesk/settings/SettingsWindow.h"

#include <commctrl.h>

#include <cstdlib>

#include "snowydesk/common/Log.h"
#include "snowydesk/i18n/I18n.h"

namespace snowydesk::settings {
namespace {

constexpr wchar_t kSettingsClass[] = L"SnowyDeskSettingsWindow";
constexpr int kWindowWidth = 520;
constexpr int kWindowHeight = 560;

HFONT CreateUiFont(int points) {
    const HDC dc = GetDC(nullptr);
    const int height = -MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, dc);

    LOGFONTW logFont{};
    logFont.lfHeight = height;
    logFont.lfWeight = FW_NORMAL;
    logFont.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(logFont.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&logFont);
}

}  // namespace

bool SettingsWindow::Create(HINSTANCE instance, ApplyHandler handler) {
    instance_ = instance;
    handler_ = std::move(handler);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &SettingsWindow::WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kSettingsClass;
    if (!RegisterClassExW(&windowClass)) {
        spdlog::error("RegisterClassEx failed for the settings window");
        return false;
    }
    classRegistered_ = true;

    RECT rect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);

    hwnd_ = CreateWindowExW(0, kSettingsClass, i18n::Tr(i18n::Str::SettingsTitle),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                            instance, this);
    if (hwnd_ == nullptr) {
        spdlog::error("CreateWindowEx failed for the settings window");
        return false;
    }

    CreateControls();
    RefreshTexts();
    return true;
}

void SettingsWindow::Destroy() {
    if (font_ != nullptr) {
        DeleteObject(font_);
        font_ = nullptr;
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kSettingsClass, instance_);
        classRegistered_ = false;
    }
}

void SettingsWindow::CreateControls() {
    font_ = CreateUiFont(10);

    auto createStatic = [this](ControlId id) {
        return CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
                               hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               instance_, nullptr);
    };

    languageLabel_ = createStatic(kLanguageLabel);
    languageCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                         WS_VSCROLL,
                                     0, 0, 0, 0, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLanguageCombo)), instance_, nullptr);

    densityLabel_ = createStatic(kDensityLabel);
    densitySlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ,
                                     0, 0, 0, 0, hwnd_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDensitySlider)), instance_, nullptr);
    densityEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_RIGHT, 0, 0,
                                   0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDensityEdit)), instance_,
                                   nullptr);

    auto createCheck = [this](ControlId id) {
        return CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                               0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               instance_, nullptr);
    };

    snowSticksCheck_ = createCheck(kSnowSticksCheck);
    garlandCheck_ = createCheck(kGarlandCheck);
    autostartCheck_ = createCheck(kAutostartCheck);
    autostartServiceCheck_ = createCheck(kAutostartServiceCheck);
    topmostCheck_ = createCheck(kTopmostCheck);
    topmostHint_ = createStatic(kTopmostHint);

    applyButton_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   0, 0, 0, 0, hwnd_,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdApply)), instance_, nullptr);
    saveButton_ = CreateWindowExW(0, L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0,
                                  0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdSave)), instance_, nullptr);
    cancelButton_ = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0,
                                    0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdCancel)), instance_,
                                    nullptr);

    for (HWND control : {languageLabel_, languageCombo_, densityLabel_, densitySlider_,
                         densityEdit_, snowSticksCheck_, garlandCheck_, autostartCheck_,
                         autostartServiceCheck_, topmostCheck_, topmostHint_, applyButton_,
                         saveButton_, cancelButton_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    SendMessageW(densitySlider_, TBM_SETRANGE, TRUE, MAKELPARAM(kDensityMin, kDensityMax));
    SendMessageW(densitySlider_, TBM_SETTICFREQ, 20, 0);

    RECT client{};
    GetClientRect(hwnd_, &client);
    LayoutControls(client.right - client.left, client.bottom - client.top);
}

void SettingsWindow::LayoutControls(int clientWidth, int clientHeight) {
    const int margin = 20;
    const int width = clientWidth - margin * 2;
    int y = margin;

    MoveWindow(languageLabel_, margin, y, 180, 20, TRUE);
    MoveWindow(languageCombo_, margin, y + 22, width, 200, TRUE);
    y += 66;

    MoveWindow(densityLabel_, margin, y, 220, 20, TRUE);
    MoveWindow(densityEdit_, margin + width - 80, y - 4, 80, 26, TRUE);
    y += 26;
    MoveWindow(densitySlider_, margin, y, width, 40, TRUE);
    y += 48;

    MoveWindow(snowSticksCheck_, margin, y, width, 24, TRUE);
    y += 30;
    MoveWindow(garlandCheck_, margin, y, width, 24, TRUE);
    y += 30;
    MoveWindow(autostartCheck_, margin, y, width, 24, TRUE);
    y += 26;
    MoveWindow(autostartServiceCheck_, margin + 22, y, width - 22, 22, TRUE);
    y += 28;
    MoveWindow(topmostCheck_, margin, y, width, 24, TRUE);
    y += 24;
    MoveWindow(topmostHint_, margin + 22, y, width - 22, 34, TRUE);
    y += 44;

    const int buttonWidth = (width - 20) / 3;
    const int buttonY = clientHeight - margin - 34;
    MoveWindow(applyButton_, margin, buttonY, buttonWidth, 32, TRUE);
    MoveWindow(saveButton_, margin + buttonWidth + 10, buttonY, buttonWidth, 32, TRUE);
    MoveWindow(cancelButton_, margin + (buttonWidth + 10) * 2, buttonY, buttonWidth, 32, TRUE);

    (void)y;
}

void SettingsWindow::RefreshTexts() {
    if (hwnd_ == nullptr) return;

    SetWindowTextW(hwnd_, i18n::Tr(i18n::Str::SettingsTitle));
    SetWindowTextW(languageLabel_, i18n::Tr(i18n::Str::LblLanguage));
    SetWindowTextW(densityLabel_, i18n::Tr(i18n::Str::LblDensity));
    SetWindowTextW(snowSticksCheck_, i18n::Tr(i18n::Str::LblSnowSticks));
    SetWindowTextW(garlandCheck_, i18n::Tr(i18n::Str::LblGarland));
    SetWindowTextW(autostartCheck_, i18n::Tr(i18n::Str::LblAutostart));
    SetWindowTextW(autostartServiceCheck_, i18n::Tr(i18n::Str::LblAutostartService));
    SetWindowTextW(topmostCheck_, i18n::Tr(i18n::Str::LblTopmost));
    SetWindowTextW(topmostHint_, i18n::Tr(i18n::Str::LblTopmostHint));
    SetWindowTextW(applyButton_, i18n::Tr(i18n::Str::BtnApply));
    SetWindowTextW(saveButton_, i18n::Tr(i18n::Str::BtnSave));
    SetWindowTextW(cancelButton_, i18n::Tr(i18n::Str::BtnCancel));

    // Language combo keeps both names in their own language on purpose.
    const LRESULT selected = SendMessageW(languageCombo_, CB_GETCURSEL, 0, 0);
    SendMessageW(languageCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(languageCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(i18n::Tr(i18n::Str::LangRussian)));
    SendMessageW(languageCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(i18n::Tr(i18n::Str::LangEnglish)));
    SendMessageW(languageCombo_, CB_SETCURSEL, selected == CB_ERR ? 0 : selected, 0);
}

void SettingsWindow::Show(const Settings& current) {
    if (hwnd_ == nullptr) return;
    pristine_ = current;
    WriteToControls(current);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::Hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool SettingsWindow::visible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void SettingsWindow::WriteToControls(const Settings& settings) {
    updatingControls_ = true;

    SendMessageW(languageCombo_, CB_SETCURSEL,
                 settings.language == Language::English ? 1 : 0, 0);
    SendMessageW(densitySlider_, TBM_SETPOS, TRUE, settings.density);

    wchar_t densityText[16] = {};
    swprintf_s(densityText, L"%d", settings.density);
    SetWindowTextW(densityEdit_, densityText);

    SendMessageW(snowSticksCheck_, BM_SETCHECK,
                 settings.snowSticks ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(garlandCheck_, BM_SETCHECK,
                 settings.garlandEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(autostartCheck_, BM_SETCHECK,
                 settings.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(autostartServiceCheck_, BM_SETCHECK,
                 settings.autostartMode == AutostartMode::Service ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(topmostCheck_, BM_SETCHECK,
                 settings.topmostOverGames ? BST_CHECKED : BST_UNCHECKED, 0);

    EnableWindow(autostartServiceCheck_, settings.autostart ? TRUE : FALSE);

    updatingControls_ = false;
}

Settings SettingsWindow::ReadFromControls() const {
    Settings settings;
    const LRESULT language = SendMessageW(languageCombo_, CB_GETCURSEL, 0, 0);
    settings.language = language == 1 ? Language::English : Language::Russian;

    settings.density = static_cast<int>(SendMessageW(densitySlider_, TBM_GETPOS, 0, 0));

    settings.snowSticks =
        SendMessageW(snowSticksCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.garlandEnabled = SendMessageW(garlandCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.autostart = SendMessageW(autostartCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.autostartMode =
        SendMessageW(autostartServiceCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED
            ? AutostartMode::Service
            : AutostartMode::UserRunKey;
    settings.topmostOverGames = SendMessageW(topmostCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;

    settings.density = ClampDensity(settings.density);
    return settings;
}

void SettingsWindow::SyncDensityEditFromSlider() {
    if (updatingControls_) return;
    updatingControls_ = true;
    const int value = static_cast<int>(SendMessageW(densitySlider_, TBM_GETPOS, 0, 0));
    wchar_t text[16] = {};
    swprintf_s(text, L"%d", value);
    SetWindowTextW(densityEdit_, text);
    updatingControls_ = false;
}

void SettingsWindow::SyncSliderFromDensityEdit() {
    if (updatingControls_) return;
    wchar_t text[16] = {};
    GetWindowTextW(densityEdit_, text, 16);
    const int value = ClampDensity(_wtoi(text));
    updatingControls_ = true;
    SendMessageW(densitySlider_, TBM_SETPOS, TRUE, value);
    updatingControls_ = false;
}

void SettingsWindow::Apply(bool closeAfterApply) {
    const Settings settings = ReadFromControls();
    const bool accepted = handler_ ? handler_(settings) : false;
    if (accepted) {
        pristine_ = settings;
        WriteToControls(settings);
    }
    if (closeAfterApply && accepted) {
        Hide();
    }
}

void SettingsWindow::Cancel() {
    // Roll back to the snapshot taken when the window was opened: an earlier
    // Apply click may already have changed the live scene.
    WriteToControls(pristine_);
    if (handler_) {
        handler_(pristine_);
    }
    Hide();
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }

    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return self->HandleMessage(hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_SIZE:
            LayoutControls(LOWORD(lparam), HIWORD(lparam));
            return 0;

        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lparam) == densitySlider_) {
                SyncDensityEditFromSlider();
            }
            return 0;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int notification = HIWORD(wparam);

            if (id == kCmdApply) {
                Apply(false);
                return 0;
            }
            if (id == kCmdSave) {
                Apply(true);
                return 0;
            }
            if (id == kCmdCancel) {
                Cancel();
                return 0;
            }
            if (id == kDensityEdit && notification == EN_CHANGE) {
                SyncSliderFromDensityEdit();
                return 0;
            }
            if (id == kAutostartCheck && notification == BN_CLICKED) {
                const bool enabled =
                    SendMessageW(autostartCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                EnableWindow(autostartServiceCheck_, enabled ? TRUE : FALSE);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            Cancel();
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace snowydesk::settings
