#pragma once

#include <string>

namespace snowydesk::service {

// Starts SnowyDeskUI.exe inside the active console session.
// Used when settings.autostart == true and autostartMode == Service.
// Requires LocalSystem (WTSQueryUserToken). Returns false when no interactive
// session exists yet (e.g. the machine is still booting to the logon screen).
bool LaunchUiInActiveSession(std::string& error);

}  // namespace snowydesk::service
