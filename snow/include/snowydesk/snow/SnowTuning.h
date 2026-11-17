#pragma once

// Every tunable number of the snow simulation lives here, with a comment that
// explains the behaviour it produces. Nothing else in snow/ hard-codes magic
// values, so tweaking the look never requires touching the simulation code.

namespace snowydesk::tuning {

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
// Target frame rate of the overlay. Rendering and simulation share the tick.
inline constexpr int kRenderFps = 60;
inline constexpr float kFrameSeconds = 1.0f / static_cast<float>(kRenderFps);

// Window tracking (resize/close/move reactions) runs at 10 Hz as specified.
inline constexpr int kWindowPollHz = 10;
inline constexpr float kWindowPollSeconds = 1.0f / static_cast<float>(kWindowPollHz);

// Upper bound for a single simulation step so a stall cannot teleport snow.
inline constexpr float kMaxStepSeconds = 0.05f;

// ---------------------------------------------------------------------------
// Particles
// ---------------------------------------------------------------------------
// Hard cap; protects the frame budget even with density=200.
inline constexpr int kMaxParticles = 1200;

// Gravity and terminal velocity, in DIPs per second.
inline constexpr float kGravity = 85.0f;
inline constexpr float kTerminalVelocity = 105.0f;

// Flake radius range in DIPs.
inline constexpr float kMinRadius = 1.1f;
inline constexpr float kMaxRadius = 3.2f;

// Sideways drift ("swaying"): a sine wave applied around the fall line.
inline constexpr float kSwayAmplitudeMin = 3.0f;
inline constexpr float kSwayAmplitudeMax = 14.0f;
inline constexpr float kSwaySpeedMin = 0.5f;  // radians / second
inline constexpr float kSwaySpeedMax = 1.7f;

// Fraction of the sway that survives once a flake is attached to a surface.
inline constexpr float kAttachedSwayFactor = 0.12f;

// ---------------------------------------------------------------------------
// Density
// ---------------------------------------------------------------------------
// One particle is requested per kSpawnIntervalSeconds until the density target
// is reached; this ramps the scene in instead of spawning 200 flakes at once.
inline constexpr float kSpawnIntervalSeconds = 0.015f;

// Density target is reached over this time after a restart (soft ramp).
inline constexpr float kDensityRampSeconds = 6.0f;

// ---------------------------------------------------------------------------
// Accumulation
// ---------------------------------------------------------------------------
// How many flakes a single surface (window edge or taskbar) may hold before the
// pile spills. Higher = thicker snow caps.
inline constexpr int kPileSpillThreshold = 55;

// How many flakes slide off when the threshold is crossed.
inline constexpr int kPileSpillCount = 16;

// Maximum thickness of a single pile in DIPs; the pile stops growing after that.
inline constexpr float kPileMaxThickness = 9.0f;

// Vertical jitter (DIPs) applied to flakes resting on an edge, so piles look
// random instead of a straight line.
inline constexpr float kPileJitter = 2.0f;

// Horizontal jitter (DIPs) for flakes resting on an edge.
inline constexpr float kPileSpread = 6.0f;

// A flake landing within this many DIPs above an edge sticks to it.
inline constexpr float kLandingTolerance = 14.0f;

// ---------------------------------------------------------------------------
// Turnover: why the scene keeps snowing instead of freezing into one line
// ---------------------------------------------------------------------------
// A flake that sticks to a surface eventually melts away after this long
// (randomized per flake), which keeps a steady stream of falling snow alive.
inline constexpr float kAttachedLifetimeMin = 9.0f;
inline constexpr float kAttachedLifetimeMax = 26.0f;

// Seconds before the end of its life where a resting flake fades out.
inline constexpr float kAttachedFadeSeconds = 2.0f;

// Hard limit of flakes allowed to accumulate on the virtual-screen floor, so
// the ground line cannot eat the whole particle budget.
inline constexpr int kGroundPileLimit = 24;

// ---------------------------------------------------------------------------
// Melting (setting "snow stays on windows and the taskbar" = off)
// ---------------------------------------------------------------------------
// A pile disappears at roughly this many flakes per second.
inline constexpr float kMeltRatePerSecond = 14.0f;

// ---------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------
// Extra downward speed given to restored flakes whose anchor is gone
// (graceful degradation when an HWND from state.json no longer resolves).
inline constexpr float kRestoreFallBoost = 30.0f;

}  // namespace snowydesk::tuning
