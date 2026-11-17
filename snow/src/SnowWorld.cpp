#include "snowydesk/snow/SnowWorld.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "snowydesk/snow/SnowTuning.h"

namespace snowydesk::snow {
namespace {

struct EnumContext {
    std::uint64_t selfPid = 0;
    std::vector<SurfaceInfo>* surfaces = nullptr;
    RECT virtualRect{};
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
    auto* context = reinterpret_cast<EnumContext*>(lparam);
    auto* surfaces = context->surfaces;

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == context->selfPid) {
        return TRUE;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0 || (exStyle & WS_EX_NOACTIVATE) != 0) {
        return TRUE;
    }

    // Windows on another virtual desktop are "cloaked"; they must not hold snow.
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked != 0) {
        return TRUE;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return TRUE;
    }
    if (rect.right - rect.left <= 1 || rect.bottom - rect.top <= 1) {
        return TRUE;
    }

    RECT intersection{};
    if (!IntersectRect(&intersection, &rect, &context->virtualRect)) {
        return TRUE;
    }

    SurfaceInfo info;
    info.hwnd = reinterpret_cast<std::uint64_t>(hwnd);
    info.rect = rect;
    info.kind = 1;
    surfaces->push_back(info);
    return TRUE;
}

std::uint64_t SurfaceKey(std::uint64_t hwnd, int edge) {
    return hwnd * 8ull + static_cast<std::uint64_t>(edge);
}

}  // namespace

SnowWorld::SnowWorld() {
    particles_.reserve(tuning::kMaxParticles);
}

void SnowWorld::SetSettings(const Settings& settings) {
    settings_ = settings;
    settings_.density = ClampDensity(settings_.density);
    if (!settings_.snowSticks) {
        meltAccumulator_ = 0.0f;
    }
}

void SnowWorld::SetVirtualRect(const RECT& rect) {
    virtual_ = rect;
}

void SnowWorld::AdoptState(const AppState& state) {
    SetSettings(state.settings);
    particles_.clear();
    particles_.reserve(std::max<std::size_t>(state.particles.size(), 64));
    for (auto particle : state.particles) {
        if (particles_.size() >= tuning::kMaxParticles) {
            break;
        }
        if (particle.baseX == 0.0f && particle.baseY == 0.0f) {
            particle.baseX = particle.x;
            particle.baseY = particle.y;
        }
        particle.x = particle.baseX;
        particle.y = particle.baseY;
        if (particle.state != ParticleState::Falling) {
            // Anchors are per-session. If the HWND is gone the next poll turns
            // this flake into a falling one (see ReactToSurfaceChanges).
            particle.vy = 0.0f;
            // Older state files have no lifetime: give the pile a staggered
            // expiry so a restored scene keeps snowing too.
            if (particle.lifetime <= 0.0f) {
                particle.lifetime = particle.age + RandomFloat(tuning::kAttachedLifetimeMin,
                                                              tuning::kAttachedLifetimeMax);
            }
        } else {
            particle.vy = std::max(particle.vy, tuning::kRestoreFallBoost);
        }
        particles_.push_back(particle);
    }
}

AppState SnowWorld::ToState() const {
    AppState state;
    state.settings = settings_;
    state.particles = particles_;
    return state;
}

void SnowWorld::Reseed(int count) {
    const int target = std::clamp(count, 0, tuning::kMaxParticles);
    particles_.clear();
    const float width = static_cast<float>(virtual_.right - virtual_.left);
    const float height = static_cast<float>(virtual_.bottom - virtual_.top);
    for (int i = 0; i < target; ++i) {
        Particle particle;
        particle.baseX = static_cast<float>(virtual_.left) + RandomFloat(0.0f, width);
        particle.baseY = static_cast<float>(virtual_.top) + RandomFloat(0.0f, height);
        particle.size = RandomFloat(tuning::kMinRadius, tuning::kMaxRadius);
        particle.swayPhase = RandomFloat(0.0f, 6.2831853f);
        particle.swaySpeed = RandomFloat(tuning::kSwaySpeedMin, tuning::kSwaySpeedMax);
        particle.swayAmplitude = RandomFloat(tuning::kSwayAmplitudeMin, tuning::kSwayAmplitudeMax);
        particle.vy = RandomFloat(10.0f, 40.0f);
        particle.x = particle.baseX;
        particle.y = particle.baseY;
        particles_.push_back(particle);
    }
}

void SnowWorld::Tick(float dt) {
    dt = std::clamp(dt, 0.0f, tuning::kMaxStepSeconds);
    if (dt <= 0.0f) return;

    ApplyDensity(dt);

    for (auto& particle : particles_) {
        if (particle.state == ParticleState::Falling) {
            UpdateFalling(particle, dt);
            TryLand(particle);
        } else {
            UpdateAttached(particle, dt);
        }
    }

    MeltPiles(dt);
    SpillOverfullPiles(dt);

    // Flakes that left the virtual screen are recycled by the density controller.
    const float bottom = static_cast<float>(virtual_.bottom) + 80.0f;
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [bottom](const Particle& particle) {
                           if (particle.state == ParticleState::Falling) {
                               return particle.baseY > bottom;
                           }
                           // A resting flake melts away at the end of its
                           // lifetime; the density controller refills the sky.
                           return particle.lifetime > 0.0f &&
                                  particle.age > particle.lifetime;
                       }),
        particles_.end());
}

void SnowWorld::PollWindows() {
    RefreshSurfaces();
    ReactToSurfaceChanges();
    ++pollCount_;
}

void SnowWorld::ClearTaskbarSnow() {
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& particle) {
                                        return particle.state == ParticleState::OnTaskbar ||
                                               particle.anchor.edge == 3;
                                    }),
                     particles_.end());
}

std::size_t SnowWorld::taskbarPileCount() const {
    return static_cast<std::size_t>(std::count_if(
        particles_.begin(), particles_.end(),
        [](const Particle& particle) { return particle.state == ParticleState::OnTaskbar; }));
}

std::size_t SnowWorld::countInState(ParticleState state) const {
    return static_cast<std::size_t>(
        std::count_if(particles_.begin(), particles_.end(),
                      [state](const Particle& particle) { return particle.state == state; }));
}

void SnowWorld::ReleaseAnchor(std::uint64_t hwnd) {
    for (auto& particle : particles_) {
        if (particle.anchor.hwnd == hwnd && particle.state != ParticleState::Falling) {
            particle.state = ParticleState::Falling;
            particle.anchor = {};
            particle.vy = tuning::kRestoreFallBoost;
            particle.baseY = particle.y;
        }
    }
    anchorRects_.erase(hwnd);
}

void SnowWorld::SpawnFalling() {
    if (particles_.size() >= static_cast<std::size_t>(tuning::kMaxParticles)) {
        return;
    }

    const float left = static_cast<float>(virtual_.left);
    const float right = static_cast<float>(virtual_.right);
    const float top = static_cast<float>(virtual_.top);

    Particle particle;
    particle.baseX = RandomFloat(left - 40.0f, right + 40.0f);
    particle.baseY = top - RandomFloat(0.0f, 60.0f);
    particle.x = particle.baseX;
    particle.y = particle.baseY;
    particle.size = RandomFloat(tuning::kMinRadius, tuning::kMaxRadius);
    particle.swayPhase = RandomFloat(0.0f, 6.2831853f);
    particle.swaySpeed = RandomFloat(tuning::kSwaySpeedMin, tuning::kSwaySpeedMax);
    particle.swayAmplitude = RandomFloat(tuning::kSwayAmplitudeMin, tuning::kSwayAmplitudeMax);
    particle.vy = RandomFloat(5.0f, 35.0f);
    particles_.push_back(particle);
}

void SnowWorld::UpdateFalling(Particle& particle, float dt) const {
    particle.vy = std::min(particle.vy + tuning::kGravity * dt, tuning::kTerminalVelocity);
    particle.baseY += particle.vy * dt;
    particle.swayPhase += particle.swaySpeed * dt;
    particle.x = particle.baseX + std::sin(particle.swayPhase) * particle.swayAmplitude;
    particle.y = particle.baseY;
    particle.age += dt;
}

void SnowWorld::UpdateAttached(Particle& particle, float dt) {
    particle.swayPhase += particle.swaySpeed * dt;
    particle.x = particle.baseX + std::sin(particle.swayPhase) * particle.swayAmplitude *
                                       tuning::kAttachedSwayFactor;
    particle.y = particle.baseY;
    particle.age += dt;
}

bool SnowWorld::TryLand(Particle& particle) const {
    const float radius = particle.size;
    const float centerX = particle.baseX;
    const float centerY = particle.baseY;

    const bool sticks = settings_.snowSticks;

    // Taskbar first: it is the surface users look at.
    for (const auto& surface : surfaces_) {
        if (surface.kind != 3) continue;
        const float top = static_cast<float>(surface.rect.top);
        if (centerY + radius >= top && centerY - radius <= top + tuning::kLandingTolerance &&
            centerX >= static_cast<float>(surface.rect.left) - 8.0f &&
            centerX <= static_cast<float>(surface.rect.right) + 8.0f) {
            if (sticks) {
                AttachTo(particle, surface);
            }
            return true;
        }
    }

    for (const auto& surface : surfaces_) {
        if (surface.kind != 1) continue;
        const float left = static_cast<float>(surface.rect.left);
        const float right = static_cast<float>(surface.rect.right);
        if (centerX < left - 4.0f || centerX > right + 4.0f) continue;

        const float top = static_cast<float>(surface.rect.top);
        if (centerY + radius >= top && centerY - radius <= top + tuning::kLandingTolerance) {
            if (sticks) {
                AttachTo(particle, surface);
            }
            return true;
        }

        // Bottom edges collect a thin shelf of snow that has drifted underneath.
        const float bottom = static_cast<float>(surface.rect.bottom);
        if (centerY - radius <= bottom && centerY + radius >= bottom - tuning::kLandingTolerance) {
            if (sticks && RandomFloat(0.0f, 1.0f) < 0.18f) {
                SurfaceInfo bottomEdge = surface;
                bottomEdge.rect.top = static_cast<LONG>(bottom);
                bottomEdge.rect.bottom = static_cast<LONG>(bottom);
                particle.state = ParticleState::OnBottomEdge;
                particle.anchor.hwnd = surface.hwnd;
                particle.anchor.edge = 2;
                particle.baseY = bottom + particle.size * 0.5f;
                particle.vy = 0.0f;
                return true;
            }
            return true;
        }
    }

    // Virtual screen bottom: a thin snow line, capped so it cannot swallow the
    // whole particle budget (the rest falls off-screen and is recycled).
    const float screenBottom = static_cast<float>(virtual_.bottom);
    if (centerY + radius >= screenBottom) {
        if (sticks) {
            const auto onGround = std::count_if(
                particles_.begin(), particles_.end(), [](const Particle& other) {
                    return other.state == ParticleState::OnBottomEdge &&
                           other.anchor.hwnd == 0 && other.anchor.edge == 2;
                });
            if (static_cast<int>(onGround) < tuning::kGroundPileLimit) {
                particle.state = ParticleState::OnBottomEdge;
                particle.anchor.hwnd = 0;
                particle.anchor.edge = 2;
                particle.baseY = screenBottom - particle.size * 0.5f;
                particle.vy = 0.0f;
                particle.lifetime = particle.age + RandomFloat(tuning::kAttachedLifetimeMin,
                                                              tuning::kAttachedLifetimeMax);
                return true;
            }
        }
        return false;
    }

    return false;
}

void SnowWorld::AttachTo(Particle& particle, const SurfaceInfo& surface) const {
    // A pile that already reached its maximum thickness no longer accepts snow;
    // the flake keeps falling and eventually leaves the scene.
    const int pileSize = static_cast<int>(
        std::count_if(particles_.begin(), particles_.end(),
                      [&surface](const Particle& other) {
                          return other.state != ParticleState::Falling &&
                                 other.anchor.hwnd == surface.hwnd &&
                                 other.anchor.edge == (surface.kind == 3 ? 3 : 1);
                      }));
    if (pileSize >= tuning::kPileSpillThreshold) {
        return;
    }

    particle.state = surface.kind == 3 ? ParticleState::OnTaskbar : ParticleState::OnTopEdge;
    // Rest for a while, then melt away so the sky gets refilled.
    particle.lifetime =
        particle.age + RandomFloat(tuning::kAttachedLifetimeMin, tuning::kAttachedLifetimeMax);
    particle.anchor.hwnd = surface.hwnd;
    particle.anchor.edge = surface.kind == 3 ? 3 : 1;
    particle.vy = 0.0f;
    particle.baseY = static_cast<float>(surface.rect.top) - particle.size * 0.45f +
                     RandomFloat(0.0f, tuning::kPileJitter);
    particle.baseX += RandomFloat(-tuning::kPileSpread, tuning::kPileSpread);
    particle.x = particle.baseX;
    particle.y = particle.baseY;
}

void SnowWorld::ApplyDensity(float dt) {
    const int target = ClampDensity(settings_.density);

    if (target == 0) {
        if (!particles_.empty()) {
            particles_.clear();
        }
        spawnAccumulator_ = 0.0f;
        return;
    }

    densityRamp_ = std::min(densityRamp_ + dt, tuning::kDensityRampSeconds);

    // Density is the number of flakes in the air. Piles are capped separately
    // (spill threshold + lifetime), so a full taskbar can never stop the snow.
    const int falling = static_cast<int>(countInState(ParticleState::Falling));
    if (falling < target) {
        spawnAccumulator_ += dt;
        int remaining = target - falling;
        while (spawnAccumulator_ >= tuning::kSpawnIntervalSeconds && remaining > 0 &&
               particles_.size() < static_cast<std::size_t>(tuning::kMaxParticles)) {
            spawnAccumulator_ -= tuning::kSpawnIntervalSeconds;
            SpawnFalling();
            --remaining;
        }
        return;
    }

    // Density lowered: remove falling flakes from the newest ones.
    spawnAccumulator_ = 0.0f;
    int surplus = falling - target;
    while (surplus > 0) {
        const auto it = std::find_if(particles_.rbegin(), particles_.rend(),
                                     [](const Particle& particle) {
                                         return particle.state == ParticleState::Falling;
                                     });
        if (it == particles_.rend()) {
            break;
        }
        particles_.erase(std::next(it).base());
        --surplus;
    }
}

void SnowWorld::MeltPiles(float dt) {
    if (settings_.snowSticks) {
        return;
    }

    meltAccumulator_ += tuning::kMeltRatePerSecond * dt;
    while (meltAccumulator_ >= 1.0f) {
        meltAccumulator_ -= 1.0f;
        const auto it = std::find_if(particles_.begin(), particles_.end(),
                                     [](const Particle& particle) {
                                         return particle.state != ParticleState::Falling;
                                     });
        if (it == particles_.end()) {
            meltAccumulator_ = 0.0f;
            break;
        }
        particles_.erase(it);
    }
}

void SnowWorld::SpillOverfullPiles(float dt) {
    (void)dt;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> piles;
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        const auto& particle = particles_[i];
        if (particle.state == ParticleState::Falling) continue;
        piles[SurfaceKey(particle.anchor.hwnd, particle.anchor.edge)].push_back(i);
    }

    int processed = 0;
    for (auto& [key, indices] : piles) {
        (void)key;
        if (indices.size() <= static_cast<std::size_t>(tuning::kPileSpillThreshold)) continue;
        if (processed >= 4) break;  // spread the work over several frames
        ++processed;

        const std::size_t spill = std::min<std::size_t>(
            tuning::kPileSpillCount, indices.size() - tuning::kPileSpillThreshold);
        // Oldest flakes slide off first.
        std::sort(indices.begin(), indices.end(), [this](std::size_t a, std::size_t b) {
            return particles_[a].age > particles_[b].age;
        });

        for (std::size_t n = 0; n < spill; ++n) {
            Particle& particle = particles_[indices[n]];
            particle.state = ParticleState::Falling;
            particle.anchor = {};
            particle.baseY = particle.y + 1.0f;
            particle.vy = 25.0f;
        }
    }
}

void SnowWorld::RefreshSurfaces() {
    surfaces_.clear();

    EnumContext context;
    DWORD selfPid = GetCurrentProcessId();
    context.selfPid = selfPid;
    context.surfaces = &surfaces_;
    context.virtualRect = virtual_;
    EnumWindows(&EnumWindowsProc, reinterpret_cast<LPARAM>(&context));

    if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        RECT rect{};
        RECT frame{};
        if (SUCCEEDED(DwmGetWindowAttribute(taskbar, DWMWA_EXTENDED_FRAME_BOUNDS, &frame,
                                            sizeof(frame)))) {
            rect = frame;
        } else if (!GetWindowRect(taskbar, &rect)) {
            rect = RECT{};
        }
        if (rect.right > rect.left && rect.bottom > rect.top) {
            SurfaceInfo info;
            info.hwnd = 0;
            info.rect = rect;
            info.kind = 3;
            surfaces_.push_back(info);
        }
    }
}

void SnowWorld::ReactToSurfaceChanges() {
    std::unordered_map<std::uint64_t, RECT> windows;
    RECT taskbar{};
    bool hasTaskbar = false;
    for (const auto& surface : surfaces_) {
        if (surface.kind == 1) {
            windows[surface.hwnd] = surface.rect;
        } else if (surface.kind == 3) {
            taskbar = surface.rect;
            hasTaskbar = true;
        }
    }

    for (auto& particle : particles_) {
        if (particle.state == ParticleState::Falling) continue;

        if (particle.state == ParticleState::OnTaskbar) {
            if (!hasTaskbar) {
                particle.state = ParticleState::Falling;
                particle.anchor = {};
                particle.vy = tuning::kRestoreFallBoost;
                continue;
            }
            const auto previous = anchorRects_.find(0);
            if (previous != anchorRects_.end()) {
                particle.baseY += static_cast<float>(taskbar.top - previous->second.top);
            }
            continue;
        }

        if (particle.anchor.hwnd == 0) {
            // Pile on the virtual screen bottom: keep it aligned with the bottom.
            particle.baseY = static_cast<float>(virtual_.bottom) - particle.size * 0.5f;
            continue;
        }

        const auto found = windows.find(particle.anchor.hwnd);
        if (found == windows.end()) {
            // Window closed (or hidden): snow has nothing left to hold on to.
            particle.state = ParticleState::Falling;
            particle.anchor = {};
            particle.vy = tuning::kRestoreFallBoost;
            continue;
        }

        const RECT current = found->second;
        const auto previous = anchorRects_.find(particle.anchor.hwnd);
        if (previous != anchorRects_.end()) {
            const float dx = static_cast<float>(current.left - previous->second.left);
            const float dy = static_cast<float>(current.top - previous->second.top);
            // Window moved: the pile travels with it.
            particle.baseX += dx;
            particle.baseY += dy;
        }

        const float left = static_cast<float>(current.left) - 2.0f;
        const float right = static_cast<float>(current.right) + 2.0f;
        bool outside = particle.baseX < left || particle.baseX > right;

        if (particle.state == ParticleState::OnTopEdge) {
            // Window shrank vertically: the old top edge no longer exists.
            outside = outside || particle.baseY > static_cast<float>(current.bottom) + 4.0f;
            if (std::abs(particle.baseY - static_cast<float>(current.top)) > 60.0f) {
                outside = true;
            }
        } else {
            outside = outside || particle.baseY < static_cast<float>(current.top) - 4.0f;
        }

        if (outside) {
            particle.state = ParticleState::Falling;
            particle.anchor = {};
            particle.baseY = particle.y;
            particle.vy = 10.0f;
        }
    }

    anchorRects_.clear();
    for (const auto& [hwnd, rect] : windows) {
        anchorRects_[hwnd] = rect;
    }
    if (hasTaskbar) {
        anchorRects_[0] = taskbar;
    }
}

float SnowWorld::RandomFloat(float min, float max) const {
    if (max <= min) return min;
    std::uniform_real_distribution<float> distribution(min, max);
    return distribution(rng_);
}

}  // namespace snowydesk::snow
