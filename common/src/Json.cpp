#include "snowydesk/common/Json.h"

#include <cmath>
#include <limits>

namespace snowydesk {
namespace {

bool GetBool(const nlohmann::json& object, const char* key, bool fallback) {
    if (!object.contains(key)) return fallback;
    const auto& value = object.at(key);
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<long long>() != 0;
    return fallback;
}

int GetInt(const nlohmann::json& object, const char* key, int fallback) {
    if (!object.contains(key)) return fallback;
    const auto& value = object.at(key);
    if (value.is_number_integer()) return static_cast<int>(value.get<long long>());
    if (value.is_number_unsigned()) return static_cast<int>(value.get<unsigned long long>());
    if (value.is_number_float()) return static_cast<int>(value.get<double>());
    return fallback;
}

std::int64_t GetInt64(const nlohmann::json& object, const char* key, std::int64_t fallback) {
    if (!object.contains(key)) return fallback;
    const auto& value = object.at(key);
    if (value.is_number_integer()) return value.get<long long>();
    if (value.is_number_unsigned()) return static_cast<std::int64_t>(value.get<unsigned long long>());
    return fallback;
}

float GetFloat(const nlohmann::json& object, const char* key, float fallback) {
    if (!object.contains(key)) return fallback;
    const auto& value = object.at(key);
    if (value.is_number()) return static_cast<float>(value.get<double>());
    return fallback;
}

int GetEnum(const nlohmann::json& object, const char* key, int fallback, int maxValue) {
    const int raw = GetInt(object, key, fallback);
    if (raw < 0 || raw > maxValue) return fallback;
    return raw;
}

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

}  // namespace

nlohmann::json SettingsToJson(const Settings& settings) {
    return nlohmann::json{
        {"language", static_cast<int>(settings.language)},
        {"density", settings.density},
        {"snowSticks", settings.snowSticks},
        {"garlandEnabled", settings.garlandEnabled},
        {"autostart", settings.autostart},
        {"autostartMode", static_cast<int>(settings.autostartMode)},
        {"topmostOverGames", settings.topmostOverGames},
    };
}

Settings SettingsFromJson(const nlohmann::json& value) {
    Settings settings;
    if (!value.is_object()) return settings;

    settings.language =
        static_cast<Language>(GetEnum(value, "language", static_cast<int>(settings.language), 1));
    settings.density = ClampDensity(GetInt(value, "density", settings.density));
    settings.snowSticks = GetBool(value, "snowSticks", settings.snowSticks);
    settings.garlandEnabled = GetBool(value, "garlandEnabled", settings.garlandEnabled);
    settings.autostart = GetBool(value, "autostart", settings.autostart);
    settings.autostartMode = static_cast<AutostartMode>(
        GetEnum(value, "autostartMode", static_cast<int>(settings.autostartMode), 1));
    settings.topmostOverGames = GetBool(value, "topmostOverGames", settings.topmostOverGames);
    return settings;
}

nlohmann::json ParticleToJson(const Particle& particle) {
    return nlohmann::json{
        {"x", particle.x},
        {"y", particle.y},
        {"baseX", particle.baseX},
        {"baseY", particle.baseY},
        {"vx", particle.vx},
        {"vy", particle.vy},
        {"size", particle.size},
        {"swayPhase", particle.swayPhase},
        {"swaySpeed", particle.swaySpeed},
        {"swayAmplitude", particle.swayAmplitude},
        {"age", particle.age},
        {"meltTime", particle.meltTime},
        {"state", static_cast<int>(particle.state)},
        {"anchorHwnd", particle.anchor.hwnd},
        {"anchorEdge", particle.anchor.edge},
    };
}

Particle ParticleFromJson(const nlohmann::json& value) {
    Particle particle;
    if (!value.is_object()) return particle;

    particle.x = FiniteOr(GetFloat(value, "x", 0.0f), 0.0f);
    particle.y = FiniteOr(GetFloat(value, "y", 0.0f), 0.0f);
    particle.baseX = FiniteOr(GetFloat(value, "baseX", particle.x), particle.x);
    particle.baseY = FiniteOr(GetFloat(value, "baseY", particle.y), particle.y);
    particle.vx = FiniteOr(GetFloat(value, "vx", 0.0f), 0.0f);
    particle.vy = FiniteOr(GetFloat(value, "vy", 0.0f), 0.0f);
    particle.size = FiniteOr(GetFloat(value, "size", 2.0f), 2.0f);
    particle.swayPhase = FiniteOr(GetFloat(value, "swayPhase", 0.0f), 0.0f);
    particle.swaySpeed = FiniteOr(GetFloat(value, "swaySpeed", 1.0f), 1.0f);
    particle.swayAmplitude = FiniteOr(GetFloat(value, "swayAmplitude", 0.0f), 0.0f);
    particle.age = FiniteOr(GetFloat(value, "age", 0.0f), 0.0f);
    particle.meltTime = FiniteOr(GetFloat(value, "meltTime", -1.0f), -1.0f);
    particle.state = static_cast<ParticleState>(GetEnum(value, "state", 0, 3));
    particle.anchor.hwnd = static_cast<std::uint64_t>(GetInt64(value, "anchorHwnd", 0));
    particle.anchor.edge = GetEnum(value, "anchorEdge", 0, 3);

    // A particle must not claim an anonymous edge anchor: normalize to "free".
    if (particle.anchor.edge != 0 && particle.anchor.hwnd == 0 && particle.anchor.edge != 3) {
        particle.anchor.edge = 0;
        particle.state = ParticleState::Falling;
    }
    if (particle.size < 0.2f) particle.size = 0.2f;
    if (particle.size > 40.0f) particle.size = 40.0f;
    return particle;
}

nlohmann::json StateToJson(const AppState& state) {
    nlohmann::json particles = nlohmann::json::array();
    for (const auto& particle : state.particles) {
        particles.push_back(ParticleToJson(particle));
    }

    return nlohmann::json{
        {"schemaVersion", state.schemaVersion},
        {"protocolVersion", kProtocolVersion},
        {"savedAtUnixMs", state.savedAtUnixMs},
        {"settings", SettingsToJson(state.settings)},
        {"particles", std::move(particles)},
    };
}

bool StateFromJson(const nlohmann::json& value, AppState& out, std::string& error) {
    if (!value.is_object()) {
        error = "state document is not a JSON object";
        return false;
    }

    const int schema = GetInt(value, "schemaVersion", 0);
    if (schema != kStateSchemaVersion) {
        error = "unsupported schemaVersion " + std::to_string(schema) + " (expected " +
                std::to_string(kStateSchemaVersion) + ")";
        return false;
    }

    AppState state;
    state.schemaVersion = schema;
    state.savedAtUnixMs = GetInt64(value, "savedAtUnixMs", 0);

    if (value.contains("settings")) {
        state.settings = SettingsFromJson(value.at("settings"));
    }

    if (value.contains("particles")) {
        const auto& particles = value.at("particles");
        if (!particles.is_array()) {
            error = "\"particles\" must be an array";
            return false;
        }
        state.particles.reserve(particles.size());
        for (const auto& entry : particles) {
            state.particles.push_back(ParticleFromJson(entry));
        }
    }

    out = std::move(state);
    return true;
}

std::string SerializeState(const AppState& state, int indent) {
    return StateToJson(state).dump(indent);
}

bool ParseStateText(std::string_view text, AppState& out, std::string& error) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(text.begin(), text.end());
    } catch (const std::exception& ex) {
        error = std::string("JSON parse error: ") + ex.what();
        return false;
    }
    return StateFromJson(parsed, out, error);
}

std::string ValidateSettings(const Settings& settings) {
    if (settings.density < kDensityMin || settings.density > kDensityMax) {
        return "density must be within [0, 200]";
    }
    const int language = static_cast<int>(settings.language);
    if (language < 0 || language > 1) {
        return "language must be 0 (ru) or 1 (en)";
    }
    const int autostartMode = static_cast<int>(settings.autostartMode);
    if (autostartMode < 0 || autostartMode > 1) {
        return "autostartMode must be 0 (run key) or 1 (service)";
    }
    return {};
}

Settings SanitizeSettings(Settings settings) {
    settings.density = ClampDensity(settings.density);
    if (static_cast<int>(settings.language) < 0 || static_cast<int>(settings.language) > 1) {
        settings.language = Language::Russian;
    }
    if (static_cast<int>(settings.autostartMode) < 0 ||
        static_cast<int>(settings.autostartMode) > 1) {
        settings.autostartMode = AutostartMode::UserRunKey;
    }
    return settings;
}

}  // namespace snowydesk
