#include "snowydesk/overlay/OverlayHost.h"

#include <d2d1helper.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>

#include "snowydesk/common/Log.h"

#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "d2d1.lib")

namespace snowydesk::overlay {
namespace {

constexpr wchar_t kOverlayClass[] = L"SnowyDeskOverlayWindow";
constexpr float kSnowAlpha = 0.9f;
constexpr float kGarlandLineAlpha = 0.35f;

}  // namespace

OverlayHost::OverlayHost() = default;

OverlayHost::~OverlayHost() {
    Shutdown();
}

bool OverlayHost::Initialize(HINSTANCE instance, std::string& error) {
    instance_ = instance;

    const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
    if (FAILED(hr)) {
        error = "D2D1CreateFactory failed (hr=" + std::to_string(hr) + ")";
        return false;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &OverlayHost::OverlayWndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kOverlayClass;
    if (!RegisterClassExW(&windowClass)) {
        error = "RegisterClassEx failed for the overlay window";
        return false;
    }
    classRegistered_ = true;

    if (!RefreshMonitors()) {
        error = "no monitors were created for the overlay";
        return false;
    }
    return true;
}

void OverlayHost::Shutdown() {
    DestroyWindows();
    if (factory_ != nullptr) {
        factory_->Release();
        factory_ = nullptr;
    }
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kOverlayClass, instance_);
        classRegistered_ = false;
    }
}

LRESULT CALLBACK OverlayHost::OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam,
                                             LPARAM lparam) {
    // The overlay must never take input: no hit test, no activation, no paint.
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

BOOL CALLBACK OverlayHost::MonitorEnumProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc;
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(data);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    unsigned int dpiX = 96;
    unsigned int dpiY = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        dpiX = 96;
    }

    MonitorInfo entry;
    entry.handle = monitor;
    entry.rect = *rect;
    entry.dpi = dpiX;
    monitors->push_back(entry);
    return TRUE;
}

RECT OverlayHost::VirtualRect() const {
    RECT rect{};
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rect;
}

std::vector<garland::MonitorBand> OverlayHost::GarlandBands() const {
    std::vector<garland::MonitorBand> bands;
    bands.reserve(monitors_.size());
    for (const auto& monitor : monitors_) {
        garland::MonitorBand band;
        band.rect = monitor.rect;
        band.dpi = monitor.dpi;
        bands.push_back(band);
    }
    return bands;
}

bool OverlayHost::RefreshMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &OverlayHost::MonitorEnumProc,
                        reinterpret_cast<LPARAM>(&monitors));

    if (monitors.size() == monitors_.size() &&
        std::equal(monitors.begin(), monitors.end(), monitors_.begin())) {
        return true;  // nothing changed
    }

    DestroyWindows();
    monitors_ = std::move(monitors);

    for (const auto& monitor : monitors_) {
        Canvas canvas;
        std::string error;
        if (!CreateWindowForCanvas(canvas, monitor, error)) {
            spdlog::error("overlay window for monitor {} failed: {}",
                          static_cast<int>(monitor.rect.left), error);
            canvas.ReleaseResources();
            continue;
        }
        windows_.push_back(canvas);
    }

    spdlog::info("overlay: {} monitor(s), {} window(s)", monitors_.size(), windows_.size());
    return !windows_.empty();
}

bool OverlayHost::CreateWindowForCanvas(Canvas& canvas, const MonitorInfo& monitor,
                                        std::string& error) {
    const int width = monitor.rect.right - monitor.rect.left;
    const int height = monitor.rect.bottom - monitor.rect.top;

    const HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClass, L"SnowyDesk", WS_POPUP, monitor.rect.left, monitor.rect.top, width, height,
        nullptr, nullptr, instance_, nullptr);
    if (hwnd == nullptr) {
        error = "CreateWindowExW failed";
        return false;
    }

    canvas.hwnd = hwnd;
    canvas.rect = monitor.rect;
    canvas.dpi = monitor.dpi;

    // A layered window becomes visible only after the first UpdateLayeredWindow.
    if (!canvas.CreateResources(factory_, monitor.rect, monitor.dpi, error)) {
        DestroyWindow(hwnd);
        canvas.hwnd = nullptr;
        return false;
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd, HWND_TOPMOST, monitor.rect.left, monitor.rect.top, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void OverlayHost::DestroyWindows() {
    for (auto& canvas : windows_) {
        canvas.ReleaseResources();
        if (canvas.hwnd != nullptr) {
            DestroyWindow(canvas.hwnd);
            canvas.hwnd = nullptr;
        }
    }
    windows_.clear();
}

bool OverlayHost::Canvas::CreateResources(ID2D1Factory* factory, const RECT& monitorRect,
                                          unsigned int dpi, std::string& error) {
    const int width = monitorRect.right - monitorRect.left;
    const int height = monitorRect.bottom - monitorRect.top;

    screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        error = "GetDC(nullptr) failed";
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;  // top-down
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr) {
        error = "CreateDIBSection failed";
        return false;
    }

    memoryDc = CreateCompatibleDC(screenDc);
    if (memoryDc == nullptr) {
        error = "CreateCompatibleDC failed";
        return false;
    }
    previousBitmap = SelectObject(memoryDc, bitmap);

    const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

    if (FAILED(factory->CreateDCRenderTarget(&properties, &target))) {
        error = "CreateDCRenderTarget failed";
        return false;
    }

    // Drawing units are pixels: the DIB is in physical pixels.
    target->SetDpi(96.0f, 96.0f);

    const RECT binding{0, 0, width, height};
    if (FAILED(target->BindDC(memoryDc, &binding))) {
        error = "BindDC failed";
        return false;
    }

    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush))) {
        error = "CreateSolidColorBrush failed";
        return false;
    }

    (void)dpi;
    return true;
}

void OverlayHost::Canvas::ReleaseResources() {
    if (brush != nullptr) {
        brush->Release();
        brush = nullptr;
    }
    if (target != nullptr) {
        target->Release();
        target = nullptr;
    }
    if (memoryDc != nullptr) {
        if (previousBitmap != nullptr) {
            SelectObject(memoryDc, previousBitmap);
            previousBitmap = nullptr;
        }
        DeleteDC(memoryDc);
        memoryDc = nullptr;
    }
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
    if (screenDc != nullptr) {
        ReleaseDC(nullptr, screenDc);
        screenDc = nullptr;
    }
    bits = nullptr;
}

void OverlayHost::Render(const std::vector<Particle>& particles, const garland::Garland& garland) {
    for (auto& canvas : windows_) {
        canvas.DrawFrame(particles, garland, garland.enabled());
        canvas.Present();
    }
}

void OverlayHost::Canvas::DrawFrame(const std::vector<Particle>& particles,
                                    const garland::Garland& garland, bool garlandEnabled) {
    if (target == nullptr) return;

    const float scale = static_cast<float>(dpi) / 96.0f;
    const float offsetX = static_cast<float>(rect.left);
    const float offsetY = static_cast<float>(rect.top);
    const float width = static_cast<float>(rect.right - rect.left);
    const float height = static_cast<float>(rect.bottom - rect.top);

    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // --- garland -----------------------------------------------------------
    if (garlandEnabled) {
        const auto& bulbs = garland.bulbs();
        brush->SetColor(D2D1::ColorF(0.55f, 0.62f, 0.72f, kGarlandLineAlpha));
        // Wire between the first and the last bulb of this monitor.
        float firstX = -1.0f;
        float lastX = -1.0f;
        float wireY = 0.0f;
        for (const auto& bulb : bulbs) {
            if (bulb.x < offsetX - 1.0f || bulb.x > offsetX + width + 1.0f) continue;
            if (firstX < 0.0f) {
                firstX = bulb.x - offsetX;
                wireY = bulb.y - offsetY;
            }
            lastX = bulb.x - offsetX;
        }
        if (firstX >= 0.0f && lastX > firstX) {
            target->FillRectangle(D2D1::RectF(firstX, wireY - 0.75f, lastX, wireY + 0.75f), brush);
        }

        for (const auto& bulb : bulbs) {
            if (bulb.x < offsetX - 20.0f || bulb.x > offsetX + width + 20.0f) continue;
            const auto color = garland::Garland::ColorOf(bulb);
            brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, 0.95f));
            const float x = bulb.x - offsetX;
            const float y = bulb.y - offsetY;
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), bulb.radius, bulb.radius),
                                brush);

            // Soft halo.
            brush->SetColor(D2D1::ColorF(color.r, color.g, color.b, 0.22f));
            target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(x, y), bulb.radius * 2.4f, bulb.radius * 2.4f), brush);
        }
    }

    // --- snow --------------------------------------------------------------
    // Each flake is drawn twice: a wide, faint halo (soft edge) and a small
    // bright core. Fast falling flakes get a vertical streak so motion reads as
    // motion instead of a dot. Resting flakes are drawn slightly larger, which
    // turns piles into soft snow caps.
    std::size_t drawn = 0;
    for (const auto& particle : particles) {
        const float x = particle.x - offsetX;
        const float y = particle.y - offsetY;
        if (x < -12.0f || y < -12.0f || x > width + 12.0f || y > height + 12.0f) continue;

        const bool attached = particle.state != ParticleState::Falling;
        const float radius = std::max(0.7f, particle.size * scale) * (attached ? 1.35f : 1.0f);

        brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, attached ? 0.30f : 0.17f));
        target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(x, y), radius * 2.2f, radius * (attached ? 1.8f : 2.2f)),
            brush);

        if (!attached && particle.vy > 60.0f) {
            brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.22f));
            const float streak = radius * (1.0f + particle.vy / 90.0f);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius * 0.55f, streak), brush);
        }

        brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, kSnowAlpha));
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius * 0.95f, radius * 0.95f),
                            brush);
        ++drawn;
    }

    const HRESULT hr = target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        spdlog::warn("D2D asked for a new render target; ignoring frame");
    }
    (void)drawn;
}

void OverlayHost::Canvas::Present() {
    if (hwnd == nullptr || screenDc == nullptr || memoryDc == nullptr) return;

    POINT destination{rect.left, rect.top};
    SIZE size{rect.right - rect.left, rect.bottom - rect.top};
    POINT source{0, 0};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, screenDc, &destination, &size, memoryDc, &source, 0, &blend,
                        ULW_ALPHA);
}

void OverlayHost::ReassertTopmost() {
    for (auto& canvas : windows_) {
        if (canvas.hwnd == nullptr) continue;
        SetWindowPos(canvas.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    }
}

}  // namespace snowydesk::overlay
