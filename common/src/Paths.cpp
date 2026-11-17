#include "snowydesk/common/Paths.h"

#include <windows.h>

#include <shlobj.h>

namespace snowydesk::paths {
namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw);
    std::filesystem::path result;
    if (SUCCEEDED(hr) && raw != nullptr) {
        result = std::filesystem::path(raw);
    }
    if (raw != nullptr) {
        CoTaskMemFree(raw);
    }
    return result;
}

}  // namespace

std::filesystem::path ProgramDataDir() {
    // %ProgramData% is not redirected per user; KF_FLAG_CREATE is intentionally
    // not used here - the service creates the folder with its own ACL.
    std::filesystem::path base = KnownFolder(FOLDERID_ProgramData);
    if (base.empty()) {
        base = L"C:\\ProgramData";
    }
    return base / L"SnowyDesk";
}

std::filesystem::path StatePath() {
    return ProgramDataDir() / L"state.json";
}

std::filesystem::path ServiceLogPath() {
    return ProgramDataDir() / L"service.log";
}

std::filesystem::path LocalAppDataDir() {
    return KnownFolder(FOLDERID_LocalAppData) / L"SnowyDesk";
}

std::filesystem::path UiLogPath() {
    return LocalAppDataDir() / L"ui.log";
}

std::wstring PipeName() {
    return L"\\\\.\\pipe\\SnowyDesk";
}

bool EnsureDirectory(const std::filesystem::path& dir) {
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        return std::filesystem::is_directory(dir, ec);
    }
    return std::filesystem::create_directories(dir, ec);
}

}  // namespace snowydesk::paths
