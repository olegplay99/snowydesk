#include "snowydesk/common/Log.h"

#include "snowydesk/common/Paths.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>

namespace snowydesk::log {

void Init(const std::filesystem::path& file, bool console, spdlog::level::level_enum level) {
    try {
        std::vector<spdlog::sink_ptr> sinks;

        const bool created = paths::EnsureDirectory(file.parent_path());
        if (created) {
            // 1 MiB per file, 3 files, UTF-8 names.
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                file.string(), 1024 * 1024, 3);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
            sinks.push_back(file_sink);
        }

        if (console) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_pattern("[%H:%M:%S.%e] [%l] %v");
            sinks.push_back(console_sink);
        }

        if (sinks.empty()) {
            spdlog::set_default_logger(std::make_shared<spdlog::logger>("null"));
            return;
        }

        auto logger = std::make_shared<spdlog::logger>("snowydesk", sinks.begin(), sinks.end());
        logger->set_level(level);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger);
        spdlog::set_level(level);
        spdlog::flush_every(std::chrono::seconds(2));
    } catch (const std::exception&) {
        // Logging must never take the process down.
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("null"));
    }
}

}  // namespace snowydesk::log
