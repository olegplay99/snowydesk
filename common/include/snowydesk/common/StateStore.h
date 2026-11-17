#pragma once

// Reads/writes %ProgramData%\SnowyDesk\state.json.
//
// Guarantees:
//   * writes are atomic (temp file + flush + rename), a crash never truncates
//     a previously valid state.json;
//   * a corrupt file is moved aside as state.json.corrupt-<timestamp>.json and
//     defaults are returned instead of failing the service start.

#include <filesystem>
#include <string>

#include "snowydesk/common/Types.h"

namespace snowydesk::state_store {

struct LoadResult {
    AppState state;                                  // defaults when the file is missing/corrupt
    bool existed = false;                            // state.json was present
    bool recoveredFromCorrupt = false;               // file was unreadable, backed up
    std::filesystem::path backupPath;                // path of the corrupt copy, if any
    std::string error;                               // diagnostic, empty on success
};

LoadResult Load(const std::filesystem::path& path);

bool Save(const std::filesystem::path& path, const AppState& state, std::string& error);

}  // namespace snowydesk::state_store
