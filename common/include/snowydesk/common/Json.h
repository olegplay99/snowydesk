#pragma once

// JSON mapping for the shared types. Both state.json and the IPC payloads use
// these functions so the shapes can never drift apart.

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

#include "snowydesk/common/Types.h"

namespace snowydesk {

nlohmann::json SettingsToJson(const Settings& settings);
Settings SettingsFromJson(const nlohmann::json& value);

nlohmann::json ParticleToJson(const Particle& particle);
Particle ParticleFromJson(const nlohmann::json& value);

nlohmann::json StateToJson(const AppState& state);

// Strict-ish reader: returns false and fills `error` when the document does not
// validate against schema v1. Unknown extra fields are ignored on purpose so an
// older build can still read a newer file.
bool StateFromJson(const nlohmann::json& value, AppState& out, std::string& error);

std::string SerializeState(const AppState& state, int indent = -1);
bool ParseStateText(std::string_view text, AppState& out, std::string& error);

// Empty string when valid, otherwise a human readable reason.
std::string ValidateSettings(const Settings& settings);

// Clamps/normalizes out-of-range values instead of rejecting the whole document.
Settings SanitizeSettings(Settings settings);

}  // namespace snowydesk
