#pragma once

// The snow simulation. Pure Win32 + plain data: no D2D, no UI, so it can be
// unit-tested from snowydesk_tests and driven either by the overlay or by a
// headless smoke test.
//
// Coordinate space: virtual screen, in DIPs at 96 DPI (the overlay maps them
// to each monitor's own DPI when drawing).

#include <windows.h>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "snowydesk/common/Types.h"

namespace snowydesk::snow {

// A visible window that can hold snow, captured at 10 Hz.
struct SurfaceInfo {
    std::uint64_t hwnd = 0;  // 0 = taskbar / virtual screen edge
    RECT rect{};
    int kind = 0;  // 1 window, 3 taskbar
};

class SnowWorld {
public:
    SnowWorld();

    // --- configuration ---
    void SetSettings(const Settings& settings);
    const Settings& settings() const { return settings_; }
    void SetVirtualRect(const RECT& rect);
    RECT virtualRect() const { return virtual_; }

    // --- lifecycle ---
    // Takes over a persisted state. Anchors that no longer resolve are released
    // on the next PollWindows() call (graceful degradation).
    void AdoptState(const AppState& state);
    AppState ToState() const;

    // Fills the scene immediately (used after AdoptState for missing flakes).
    void Reseed(int count);

    // --- walking ---
    void Tick(float dt);   // called every frame (60 Hz)
    void PollWindows();    // called at 10 Hz: rebuilds surfaces, reacts to changes

    // --- user actions ---
    void ClearTaskbarSnow();  // returns how many were removed via taskbarPileCount()

    // --- inspection (rendering + tests) ---
    const std::vector<Particle>& particles() const { return particles_; }
    std::size_t taskbarPileCount() const;
    std::size_t countInState(ParticleState state) const;
    std::uint64_t pollCount() const { return pollCount_; }
    const std::vector<SurfaceInfo>& surfaces() const { return surfaces_; }

    // Drops the pile on every surface of a window (used when a window closes).
    void ReleaseAnchor(std::uint64_t hwnd);

private:
    void SpawnFalling();
    void UpdateFalling(Particle& particle, float dt) const;
    void UpdateAttached(Particle& particle, float dt);
    void ApplyDensity(float dt);
    void SpillOverfullPiles(float dt);
    void MeltPiles(float dt);
    bool TryLand(Particle& particle) const;
    void RefreshSurfaces();
    void ReactToSurfaceChanges();
    void AttachTo(Particle& particle, const SurfaceInfo& surface) const;
    float RandomFloat(float min, float max) const;
    // Height (DIPs) of the drift that already exists near `x` on one anchor.
    float DriftHeightAt(std::uint64_t hwnd, int edge, float x) const;

    Settings settings_{};
    RECT virtual_{0, 0, 1920, 1080};

    std::vector<Particle> particles_;
    std::vector<SurfaceInfo> surfaces_;
    std::unordered_map<std::uint64_t, RECT> anchorRects_;  // rect captured when the pile formed

    std::uint64_t pollCount_ = 0;
    float spawnAccumulator_ = 0.0f;
    float densityRamp_ = 0.0f;
    float meltAccumulator_ = 0.0f;

    mutable std::mt19937 rng_{std::random_device{}()};
};

}  // namespace snowydesk::snow
