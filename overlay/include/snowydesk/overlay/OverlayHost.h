#pragma once

// Per-monitor transparent overlay windows.
//
// Each monitor gets its own layered popup window:
//   WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
//   WS_EX_NOACTIVATE
// WS_EX_TRANSPARENT plus per-pixel alpha (UpdateLayeredWindow) means clicks,
// wheel and hover go straight to the windows underneath - the overlay is
// purely decorative.
//
// Rendering: Direct2D draws into a 32bpp premultiplied DIB, which is then
// handed to UpdateLayeredWindow. One window per monitor keeps DPI handling
// simple: every window is exactly the size of its monitor in physical pixels.

#include <windows.h>

#include <d2d1.h>

#include <string>
#include <vector>

#include "snowydesk/common/Types.h"
#include "snowydesk/garland/Garland.h"

namespace snowydesk::overlay {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    RECT rect{};           // physical pixels, virtual screen coordinates
    unsigned int dpi = 96;

    bool operator==(const MonitorInfo& other) const {
        return handle == other.handle && dpi == other.dpi && rect.left == other.rect.left &&
               rect.top == other.rect.top && rect.right == other.rect.right &&
               rect.bottom == other.rect.bottom;
    }
};

class OverlayHost {
public:
    OverlayHost();
    ~OverlayHost();

    OverlayHost(const OverlayHost&) = delete;
    OverlayHost& operator=(const OverlayHost&) = delete;

    bool Initialize(HINSTANCE instance, std::string& error);
    void Shutdown();

    // Rebuilds the window set when the monitor layout changed.
    // Returns true when the layout (and therefore the snow coordinate space) changed.
    bool RefreshMonitors();

    const std::vector<MonitorInfo>& monitors() const { return monitors_; }
    std::vector<garland::MonitorBand> GarlandBands() const;

    // Draws one frame on every monitor.
    void Render(const std::vector<Particle>& particles, const garland::Garland& garland);

    // Re-asserts HWND_TOPMOST; called by the UI every second.
    void ReassertTopmost();

    // Virtual screen rectangle in physical pixels.
    RECT VirtualRect() const;

    std::size_t windowCount() const { return windows_.size(); }

private:
    struct Canvas {
        HWND hwnd = nullptr;
        RECT rect{};
        unsigned int dpi = 96;

        HDC memoryDc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previousBitmap = nullptr;
        void* bits = nullptr;
        HDC screenDc = nullptr;

        ID2D1DCRenderTarget* target = nullptr;
        ID2D1SolidColorBrush* brush = nullptr;

        bool CreateResources(ID2D1Factory* factory, const RECT& monitorRect, unsigned int monitorDpi,
                             std::string& error);
        void ReleaseResources();
        void DrawFrame(const std::vector<Particle>& particles, const garland::Garland& garland,
                       bool garlandEnabled);
        void Present();
    };

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data);

    bool CreateWindowForCanvas(Canvas& canvas, const MonitorInfo& monitor, std::string& error);
    void DestroyWindows();

    HINSTANCE instance_ = nullptr;
    ID2D1Factory* factory_ = nullptr;
    std::vector<MonitorInfo> monitors_;
    std::vector<Canvas> windows_;
    bool classRegistered_ = false;
};

}  // namespace snowydesk::overlay
