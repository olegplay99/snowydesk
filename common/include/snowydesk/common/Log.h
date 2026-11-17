#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace snowydesk::log {

// Installs a rotating file logger (and an optional console logger).
// Safe to call once per process; calling it twice replaces the logger.
void Init(const std::filesystem::path& file, bool console, spdlog::level::level_enum level =
                                                             spdlog::level::info);

}  // namespace snowydesk::log
