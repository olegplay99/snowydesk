#pragma once

// Garland: a chain of light bulbs along the top edge of every monitor.
//
// Multi-monitor / DPI: the UI passes one band per monitor (its rectangle plus
// the monitor DPI). Bulbs are spaced in DIPs and the radius is scaled by
// dpi/96, so the chain looks identical on a 4K display and on a 1080p one.
// Every monitor gets its own chain, staggered by a per-monitor phase so they do
// not blink in lockstep.

#include <windows.h>

#include <vector>

namespace snowydesk::garland {

struct MonitorBand {
    RECT rect{};
    unsigned int dpi = 96;
};

struct Rgb {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct Bulb {
    float x = 0.0f;  // virtual screen coordinates, DIPs
    float y = 0.0f;
    float radius = 4.0f;
    float phase = 0.0f;       // radians
    float speed = 1.0f;       // radians / second
    float intensity = 1.0f;   // 0..1, refreshed by Update()
    int paletteIndex = 0;
};

class Garland {
public:
    Garland();

    void SetEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    // Rebuilds the chain for the given monitors. Called on monitor/DPI change
    // and when the setting is switched on.
    void Rebuild(const std::vector<MonitorBand>& bands);

    // Advances the flicker animation. Safe to call while disabled.
    void Update(float dt);

    const std::vector<Bulb>& bulbs() const { return bulbs_; }

    // Colour of a bulb at its current intensity (flicker is a sine wave).
    static Rgb ColorOf(const Bulb& bulb);

private:
    bool enabled_ = false;
    float time_ = 0.0f;
    std::vector<Bulb> bulbs_;
    std::vector<MonitorBand> bands_;
};

}  // namespace snowydesk::garland
