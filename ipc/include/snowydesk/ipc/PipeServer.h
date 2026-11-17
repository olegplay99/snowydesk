#pragma once

// Named-pipe server side, hosted by SnowyDeskService.
//
// Pipe name: \\.\pipe\SnowyDesk
// Security: SDDL "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)" - SYSTEM, built-in
// administrators and authenticated users may connect. The service runs as
// LocalSystem, the UI runs as the interactive user; the pipe therefore cannot
// use the default owner-only DACL.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "snowydesk/ipc/Protocol.h"

namespace snowydesk::ipc {

class NamedPipeServer {
public:
    using Handler = std::function<Response(const Request&)>;

    NamedPipeServer(std::wstring pipeName, Handler handler);
    ~NamedPipeServer();

    NamedPipeServer(const NamedPipeServer&) = delete;
    NamedPipeServer& operator=(const NamedPipeServer&) = delete;

    // Creates the first pipe instance and the accept thread.
    bool Start(std::string& error);
    void Stop();

    bool running() const { return running_.load(); }

    // Number of client connections handled since Start(), useful for smoke tests.
    std::uint64_t servedCount() const;

private:
    struct Shared {
        Handler handler;
        std::atomic<bool> running{true};
        std::atomic<std::uint64_t> served{0};
        std::wstring pipeName;
        std::mutex activeMutex;
        int activeWorkers = 0;
    };

    HANDLE CreateInstance(bool firstInstance, std::string& error);
    void AcceptLoop(std::shared_ptr<Shared> shared);
    static void ServeConnection(std::shared_ptr<Shared> shared, HANDLE pipe);

    static SECURITY_ATTRIBUTES* PipeSecurity();

    std::wstring pipeName_;
    Handler handler_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    HANDLE pendingInstance_ = INVALID_HANDLE_VALUE;
    std::shared_ptr<Shared> shared_;
};

}  // namespace snowydesk::ipc
