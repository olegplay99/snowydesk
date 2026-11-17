#include "snowydesk/overlay/TrayIcon.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "snowydesk/common/Log.h"
#include "snowydesk/i18n/I18n.h"

namespace snowydesk::overlay {
namespace {

constexpr int kIconSize = 32;

// Snowflake icon drawn at runtime: no binary assets, scales with the tray DPI.
HICON CreateSnowflakeIcon() {
    const HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) return nullptr;

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = kIconSize;
    header.bV5Height = -kIconSize;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    const HBITMAP color = CreateDIBSection(screenDc, reinterpret_cast<BITMAPINFO*>(&header),
                                           DIB_RGB_COLORS, &bits, nullptr, 0);

    // A 1-bpp 32x32 mask: 4 bytes per row, all zeroes = "use the colour bitmap".
    const std::vector<std::uint8_t> maskBits(4 * kIconSize, 0);
    const HBITMAP mask = CreateBitmap(kIconSize, kIconSize, 1, 1, maskBits.data());
    if (color == nullptr || mask == nullptr) {
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        ReleaseDC(nullptr, screenDc);
        return nullptr;
    }

    const HDC dc = CreateCompatibleDC(screenDc);
    const HGDIOBJ previous = SelectObject(dc, color);
    const HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    auto* pixels = static_cast<std::uint32_t*>(bits);
    for (int i = 0; i < kIconSize * kIconSize; ++i) {
        pixels[i] = 0;  // fully transparent
    }

    const HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    const HGDIOBJ previousPen = SelectObject(dc, pen);

    const double center = kIconSize / 2.0;
    const double radius = kIconSize * 0.42;
    for (int arm = 0; arm < 3; ++arm) {
        const double angle = arm * 3.14159265358979 / 3.0;
        const int x1 = static_cast<int>(center - std::cos(angle) * radius);
        const int y1 = static_cast<int>(center - std::sin(angle) * radius);
        const int x2 = static_cast<int>(center + std::cos(angle) * radius);
        const int y2 = static_cast<int>(center + std::sin(angle) * radius);
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
    }

    const HBRUSH white = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    const HGDIOBJ previousBrush2 = SelectObject(dc, white);
    Ellipse(dc, 11, 11, 21, 21);
    SelectObject(dc, previousBrush2);

    SelectObject(dc, previousPen);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previous);
    DeleteObject(pen);
    DeleteDC(dc);
    ReleaseDC(nullptr, screenDc);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    const HICON icon = CreateIconIndirect(&info);

    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace

TrayIcon::~TrayIcon() {
    Destroy();
}

bool TrayIcon::Create(HWND owner, CommandHandler handler) {
    if (owner == nullptr) {
        return false;
    }
    handler_ = std::move(handler);

    icon_ = CreateSnowflakeIcon();
    if (icon_ == nullptr) {
        icon_ = LoadIconW(nullptr, IDI_APPLICATION);
        iconBorrowed_ = true;
    }

    data_ = NOTIFYICONDATAW{};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon_;
    wcscpy_s(data_.szTip, i18n::Tr(i18n::Str::TrayHint));

    if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
        spdlog::error("Shell_NotifyIcon(NIM_ADD) failed");
        return false;
    }
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
    created_ = true;
    return true;
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        created_ = false;
    }
    if (icon_ != nullptr) {
        if (!iconBorrowed_) {
            DestroyIcon(icon_);
        }
        icon_ = nullptr;
        iconBorrowed_ = false;
    }
}

void TrayIcon::Refresh() {
    if (!created_) return;
    wcscpy_s(data_.szTip, i18n::Tr(i18n::Str::TrayHint));
    data_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

bool TrayIcon::HandleCallback(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message != kCallbackMessage) {
        return false;
    }
    (void)wparam;

    switch (LOWORD(lparam)) {
        case WM_LBUTTONDBLCLK:
            if (handler_) handler_(kCmdShowMainWindow);
            return true;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu();
            return true;
        case WM_LBUTTONUP:
            if (handler_) handler_(kCmdShowMainWindow);
            return true;
        default:
            return true;
    }
}

void TrayIcon::ShowContextMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;

    AppendMenuW(menu, MF_STRING, kCmdShowMainWindow, i18n::Tr(i18n::Str::MenuOpen));
    AppendMenuW(menu, MF_STRING, kCmdSettings, i18n::Tr(i18n::Str::MenuSettings));
    AppendMenuW(menu, MF_STRING, kCmdClearSnow, i18n::Tr(i18n::Str::MenuClearSnow));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdAbout, i18n::Tr(i18n::Str::MenuAbout));
    AppendMenuW(menu, MF_STRING, kCmdExit, i18n::Tr(i18n::Str::MenuExit));

    // Required for a tray menu to close when the user clicks elsewhere.
    SetForegroundWindow(data_.hWnd);

    POINT cursor{};
    GetCursorPos(&cursor);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                       cursor.x, cursor.y, 0, data_.hWnd, nullptr);

    PostMessageW(data_.hWnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command != 0 && handler_) {
        handler_(command);
    }
}

}  // namespace snowydesk::overlay
