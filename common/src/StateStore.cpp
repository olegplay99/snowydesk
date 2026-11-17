#include "snowydesk/common/StateStore.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <io.h>
#include <iterator>
#include <string>

#include "snowydesk/common/Json.h"
#include "snowydesk/common/Log.h"
#include "snowydesk/common/Paths.h"

namespace snowydesk::state_store {
namespace {

std::string Timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}

std::int64_t NowUnixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool WriteAllBytes(const std::filesystem::path& path, const std::string& bytes,
                   std::string& error) {
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) {
        error = "cannot open temp file for writing: " + path.string();
        return false;
    }

    const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    const bool flushOk = std::fflush(file) == 0;
    const bool syncOk = _commit(_fileno(file)) == 0;
    std::fclose(file);

    if (written != bytes.size() || !flushOk || !syncOk) {
        error = "short write while saving state";
        return false;
    }
    return true;
}

}  // namespace

LoadResult Load(const std::filesystem::path& path) {
    LoadResult result;
    result.state.settings = SanitizeSettings(result.state.settings);

    std::error_code ec;
    result.existed = std::filesystem::exists(path, ec);
    if (!result.existed) {
        spdlog::info("state.json not found, using defaults ({})", path.string());
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "cannot open state file";
        spdlog::error("state.json exists but cannot be opened: {}", path.string());
        return result;
    }

    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();

    std::string parseError;
    if (ParseStateText(text, result.state, parseError)) {
        result.state.settings = SanitizeSettings(result.state.settings);
        spdlog::info("state.json loaded: density={} particles={}",
                     result.state.settings.density,
                     result.state.particles.size());
        return result;
    }

    // Corrupt: keep a forensic copy and fall back to defaults (spec: backup + defaults).
    result.recoveredFromCorrupt = true;
    result.error = parseError;
    const std::string stamp = Timestamp();
    const auto backup = path.parent_path() / (path.stem().wstring() + L".corrupt-" +
                                              std::wstring(stamp.begin(), stamp.end()) +
                                              L".json");
    std::error_code moveEc;
    std::filesystem::rename(path, backup, moveEc);
    if (moveEc) {
        spdlog::error("failed to back up corrupt state.json: {}", moveEc.message());
    } else {
        result.backupPath = backup;
        spdlog::warn("corrupt state.json backed up to {}", backup.string());
    }

    AppState defaults;
    defaults.settings = SanitizeSettings(defaults.settings);
    result.state = defaults;
    return result;
}

bool Save(const std::filesystem::path& path, const AppState& state, std::string& error) {
    std::error_code ec;
    if (!paths::EnsureDirectory(path.parent_path())) {
        error = "cannot create data directory: " + path.parent_path().string();
        return false;
    }

    AppState copy = state;
    copy.schemaVersion = kStateSchemaVersion;
    copy.savedAtUnixMs = NowUnixMs();
    const std::string bytes = SerializeState(copy, 2);

    const auto temp = path.parent_path() / (path.filename().wstring() + L".tmp");
    if (!WriteAllBytes(temp, bytes, error)) {
        std::filesystem::remove(temp, ec);
        return false;
    }

    // ReplaceFile keeps the destination's ACL/creation time and is atomic on NTFS.
    if (ReplaceFileW(path.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr,
                     nullptr)) {
        return true;
    }

    const DWORD replaceError = GetLastError();
    if (replaceError == ERROR_FILE_NOT_FOUND || replaceError == ERROR_PATH_NOT_FOUND) {
        if (MoveFileExW(temp.c_str(), path.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        error = "MoveFileEx failed with error " + std::to_string(GetLastError());
        std::filesystem::remove(temp, ec);
        return false;
    }

    error = "ReplaceFile failed with error " + std::to_string(replaceError);
    std::filesystem::remove(temp, ec);
    return false;
}

}  // namespace snowydesk::state_store
