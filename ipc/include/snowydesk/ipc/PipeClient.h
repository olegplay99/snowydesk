#pragma once

// Named-pipe client used by SnowyDeskUI. Each request opens a fresh connection,
// writes one frame and reads one response - the simplest correct model for a
// handful of calls per user action.

#include <string>
#include <vector>

#include "snowydesk/ipc/Protocol.h"

namespace snowydesk::ipc {

class NamedPipeClient {
public:
    explicit NamedPipeClient(std::wstring pipeName);

    // Blocking send/receive with a bounded wait for the server.
    bool Call(const Request& request, Response& response, std::string& error,
              unsigned int timeoutMs = 3000);

    // Cheap liveness probe used by the UI status line.
    bool Ping(unsigned int timeoutMs = 500);

private:
    std::wstring pipeName_;
};

}  // namespace snowydesk::ipc
