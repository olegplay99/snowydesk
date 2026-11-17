#include "snowydesk/garland/Garland.h"

#include <algorithm>
#include <cmath>

namespace snowydesk::garland {
namespace {

// Festive palette: warm white, red, green, gold, ice blue.
constexpr Rgb kPalette[] = {
    {1.00f, 0.96f, 0.82f},
    {1.00f, 0.28f, 0.24f},
    {0.36f, 1.00f, 0.45f},
    {1.00f, 0.78f, 0.25f},
    {0.45f, 0.75f, 1.00f},
};
constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

// Spacing between bulbs, in DIPs.
constexpr float kSpacingDip = 42.0f;

// Distance from the monitor top edge to the wire, in DIPs.
constexpr float kTopInsetDip = 9.0f;

// Bulb radius, in DIPs.
constexpr float kRadiusDip = 4.2f;

// Minimum intensity so bulbs never fully switch off (less distracting).
constexpr float kMinIntensity = 0.18f;

}  // namespace

Garland::Garland() = default;

void Garland::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        bulbs_.clear();
    } else {
        Rebuild(bands_);
    }
}

void Garland::Rebuild(const std::vector<MonitorBand>& bands) {
    bands_ = bands;
    bulbs_.clear();
    if (!enabled_) {
        return;
    }

    for (std::size_t bandIndex = 0; bandIndex < bands.size(); ++bandIndex) {
        const auto& band = bands[bandIndex];
        const float scale = static_cast<float>(band.dpi) / 96.0f;
        const float left = static_cast<float>(band.rect.left);
        const float right = static_cast<float>(band.rect.right);
        const float top = static_cast<float>(band.rect.top);

        const float spacing = kSpacingDip * scale;
        const float width = right - left;
        const int count = std::max(1, static_cast<int>(width / spacing));

        // Center the chain and keep a half-spacing margin on both ends.
        const float total = static_cast<float>(count) * spacing;
        const float start = left + (width - total) * 0.5f + spacing * 0.5f;

        for (int i = 0; i < count; ++i) {
            Bulb bulb;
            bulb.x = start + static_cast<float>(i) * spacing;
            bulb.y = top + kTopInsetDip * scale;
            bulb.radius = kRadiusDip * scale;
            bulb.phase = static_cast<float>(i) * 0.55f +
                         static_cast<float>(bandIndex) * 0.9f;
            bulb.speed = 1.6f + 0.25f * static_cast<float>((i + static_cast<int>(bandIndex)) % 3);
            bulb.paletteIndex = (i + static_cast<int>(bandIndex)) % kPaletteSize;
            bulb.intensity = 1.0f;
            bulbs_.push_back(bulb);
        }
    }
}

void Garland::Update(float dt) {
    time_ += dt;
    for (auto& bulb : bulbs_) {
        const float wave = std::sin(time_ * bulb.speed + bulb.phase);
        bulb.intensity = kMinIntensity + (1.0f - kMinIntensity) * (0.5f + 0.5f * wave);
    }
}

Rgb Garland::ColorOf(const Bulb& bulb) {
    const Rgb base = kPalette[bulb.paletteIndex % kPaletteSize];
    return Rgb{base.r * bulb.intensity, base.g * bulb.intensity, base.b * bulb.intensity};
}

}  // namespace snowydesk::garland
