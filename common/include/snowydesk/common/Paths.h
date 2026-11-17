#pragma once

#include <filesystem>
#include <string>

namespace snowydesk::paths {

// %ProgramData%\SnowyDesk - machine wide data owned by the service.
std::filesystem::path ProgramDataDir();

// %ProgramData%\SnowyDesk\state.json
std::filesystem::path StatePath();

// %ProgramData%\SnowyDesk\service.log
std::filesystem::path ServiceLogPath();

// %LOCALAPPDATA%\SnowyDesk - per-user data the UI process may always write.
std::filesystem::path LocalAppDataDir();

// %LOCALAPPDATA%\SnowyDesk\ui.log
std::filesystem::path UiLogPath();

// Full name of the IPC pipe: \\.\pipe\SnowyDesk
std::wstring PipeName();

// Creates the directory (and its parents) when missing. Returns false on error.
bool EnsureDirectory(const std::filesystem::path& dir);

}  // namespace snowydesk::paths
