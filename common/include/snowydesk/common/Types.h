#pragma once

// Shared value types used by BOTH processes (SnowyDeskService and SnowyDeskUI).
// Nothing in this header may depend on the visual layer: the service links the
// same types so that state.json is written and read by identical code.

#include <cstdint>
#include <string>
#include <vector>

namespace snowydesk {

// Version of the wire protocol spoken over \\.\pipe\SnowyDesk.
// Bump when a message shape changes in an incompatible way.
inline constexpr int kProtocolVersion = 1;

// Version of the on-disk state document (%ProgramData%\SnowyDesk\state.json).
inline constexpr int kStateSchemaVersion = 1;

// Density is a target *particle count*, not a probability.
inline constexpr int kDensityMin = 0;
inline constexpr int kDensityMax = 200;
inline constexpr int kDensityDefault = 80;

enum class Language : int {
    Russian = 0,
    English = 1,
};

// How the UI is started together with Windows.
enum class AutostartMode : int {
    // HKCU\Software\Microsoft\Windows\CurrentVersion\Run value written by the UI.
    UserRunKey = 0,
    // The service launches the UI inside the active console session at boot.
    Service = 1,
};

enum class ParticleState : int {
    Falling = 0,
    OnTopEdge = 1,     // resting on the top edge of a window
    OnBottomEdge = 2,  // resting on the bottom edge of a window
    OnTaskbar = 3,     // resting on the taskbar
};

// What a particle is stuck to. hwnd == 0 means "nothing" (free / falling).
struct WindowAnchor {
    std::uint64_t hwnd = 0;
    int edge = 0;  // 0 none, 1 top edge, 2 bottom edge, 3 taskbar
};

struct Particle {
    float x = 0.0f;  // virtual-screen coordinates, DIPs (draw position)
    float y = 0.0f;
    float baseX = 0.0f;  // resting/fall line the sway is applied around
    float baseY = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float size = 2.0f;        // radius in DIPs
    float swayPhase = 0.0f;   // radians
    float swaySpeed = 1.0f;   // radians / second
    float swayAmplitude = 0.0f;  // DIPs
    float age = 0.0f;         // seconds alive
    float meltTime = 0.0f;    // seconds spent melting, -1 = not melting
    ParticleState state = ParticleState::Falling;
    WindowAnchor anchor;
};

struct Settings {
    Language language = Language::Russian;
    int density = kDensityDefault;
    bool snowSticks = true;         // snow stays on windows and the taskbar
    bool garlandEnabled = false;    // light chain along the top of every monitor
    bool autostart = false;         // start with Windows
    AutostartMode autostartMode = AutostartMode::UserRunKey;
    bool topmostOverGames = false;  // keep the overlay above borderless windows
};

// Full in-memory state. Persisted verbatim (schema v1) by the service.
struct AppState {
    int schemaVersion = kStateSchemaVersion;
    Settings settings;
    std::vector<Particle> particles;
    std::int64_t savedAtUnixMs = 0;
};

// Clamp a density value into the documented range.
inline int ClampDensity(int value) {
    if (value < kDensityMin) return kDensityMin;
    if (value > kDensityMax) return kDensityMax;
    return value;
}

}  // namespace snowydesk
