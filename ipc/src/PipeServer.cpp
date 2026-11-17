#include "snowydesk/ipc/PipeServer.h"

#include <sddl.h>

#include <chrono>
#include <vector>

#include "snowydesk/common/Log.h"

namespace snowydesk::ipc {
namespace {

constexpr DWORD kPipeBufferSize = 64 * 1024;
constexpr DWORD kReadChunk = 8192;

// Reads one '\n' terminated frame. Returns false on EOF or error.
bool ReadFrame(HANDLE pipe, std::string& out, std::string& error) {
    out.clear();
    std::vector<char> buffer(kReadChunk);

    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
        if (!ok) {
            const DWORD code = GetLastError();
            if (code == ERROR_BROKEN_PIPE || code == ERROR_PIPE_NOT_CONNECTED) {
                error = "client disconnected";
            } else {
                error = "ReadFile failed with error " + std::to_string(code);
            }
            return false;
        }
        if (read == 0) {
            error = "client closed the pipe";
            return false;
        }

        out.append(buffer.data(), read);
        if (out.size() > kMaxFrameBytes) {
            error = "frame exceeds the maximum allowed size";
            return false;
        }
        if (out.find('\n') != std::string::npos) {
            return true;
        }
    }
}

bool WriteFrame(HANDLE pipe, const std::string& frame, std::string& error) {
    std::string payload = frame;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }

    DWORD written = 0;
    if (!WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)) {
        error = "WriteFile failed with error " + std::to_string(GetLastError());
        return false;
    }
    FlushFileBuffers(pipe);
    return written == payload.size();
}

SECURITY_ATTRIBUTES* MakeSecurity() {
    // Static: the descriptor lives for the whole process lifetime.
    static SECURITY_ATTRIBUTES attributes{};
    static bool initialized = false;
    static PSECURITY_DESCRIPTOR descriptor = nullptr;

    if (!initialized) {
        initialized = true;
        // SYSTEM + Administrators: full access, Authenticated Users: full access.
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)", SDDL_REVISION_1, &descriptor,
                nullptr)) {
            attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
            attributes.lpSecurityDescriptor = descriptor;
            attributes.bInheritHandle = FALSE;
        }
    }
    return descriptor != nullptr ? &attributes : nullptr;
}

}  // namespace

NamedPipeServer::NamedPipeServer(std::wstring pipeName, Handler handler)
    : pipeName_(std::move(pipeName)), handler_(std::move(handler)) {}

NamedPipeServer::~NamedPipeServer() {
    Stop();
}

SECURITY_ATTRIBUTES* NamedPipeServer::PipeSecurity() {
    return MakeSecurity();
}

HANDLE NamedPipeServer::CreateInstance(bool firstInstance, std::string& error) {
    const DWORD openMode = PIPE_ACCESS_DUPLEX |
                           (firstInstance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0u);
    const HANDLE pipe = CreateNamedPipeW(
        pipeName_.c_str(), openMode, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, kPipeBufferSize, kPipeBufferSize, 0, PipeSecurity());

    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_ACCESS_DENIED) {
            // Another process already owns the first instance name.
            error = "pipe name is already in use (another service instance is running?)";
        } else {
            error = "CreateNamedPipe failed with error " + std::to_string(code);
        }
    }
    return pipe;
}

bool NamedPipeServer::Start(std::string& error) {
    if (running_.load()) {
        error = "server is already running";
        return false;
    }

    std::string createError;
    const HANDLE first = CreateInstance(true, createError);
    if (first == INVALID_HANDLE_VALUE) {
        error = createError;
        return false;
    }

    shared_ = std::make_shared<Shared>();
    shared_->handler = handler_;
    shared_->pipeName = pipeName_;
    shared_->running.store(true);
    running_.store(true);
    pendingInstance_ = first;

    acceptThread_ = std::thread([this] { AcceptLoop(shared_); });
    spdlog::info("named pipe server started");
    return true;
}

void NamedPipeServer::AcceptLoop(std::shared_ptr<Shared> shared) {
    HANDLE instance = pendingInstance_;
    pendingInstance_ = INVALID_HANDLE_VALUE;

    while (shared->running.load()) {
        if (instance == INVALID_HANDLE_VALUE) {
            std::string createError;
            instance = CreateInstance(false, createError);
            if (instance == INVALID_HANDLE_VALUE) {
                spdlog::error("cannot create pipe instance: {}", createError);
                break;
            }
        }

        const BOOL connected = ConnectNamedPipe(instance, nullptr);
        const DWORD connectError = connected ? ERROR_SUCCESS : GetLastError();

        if (!shared->running.load()) {
            DisconnectNamedPipe(instance);
            CloseHandle(instance);
            break;
        }

        if (!connected && connectError != ERROR_PIPE_CONNECTED) {
            spdlog::warn("ConnectNamedPipe failed with error {}", connectError);
            CloseHandle(instance);
            instance = INVALID_HANDLE_VALUE;
            continue;
        }

        // Hand the connected instance to a worker and create the next one so
        // that a second client never sees ERROR_PIPE_BUSY.
        std::string createError;
        HANDLE next = CreateInstance(false, createError);
        if (next == INVALID_HANDLE_VALUE) {
            spdlog::warn("only one pipe instance available: {}", createError);
        }

        std::thread worker([shared, instance] { ServeConnection(shared, instance); });
        worker.detach();
        instance = next;
    }

    if (instance != INVALID_HANDLE_VALUE) {
        CloseHandle(instance);
    }
}

void NamedPipeServer::ServeConnection(std::shared_ptr<Shared> shared, HANDLE pipe) {    {
        std::lock_guard<std::mutex> lock(shared->activeMutex);
        ++shared->activeWorkers;
    }

    std::string frame;
    std::string error;
    while (shared->running.load() && ReadFrame(pipe, frame, error)) {
        const auto newline = frame.find('\n');
        const std::string_view one = newline == std::string::npos
                                         ? std::string_view(frame)
                                         : std::string_view(frame).substr(0, newline);

        std::string parseError;
        Response response;
        if (const auto request = DecodeRequest(one, parseError)) {
            response.id = request->id;
            try {
                response = shared->handler(*request);
                response.id = request->id;
            } catch (const std::exception& ex) {
                response.ok = false;
                response.error = std::string("handler failed: ") + ex.what();
            }
        } else {
            response.ok = false;
            response.error = parseError;
            response.id = 0;
        }

        std::string writeError;
        if (!WriteFrame(pipe, EncodeResponse(response), writeError)) {
            spdlog::warn("failed to answer IPC request: {}", writeError);
            break;
        }
        shared->served.fetch_add(1);
        frame.clear();
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);

    std::lock_guard<std::mutex> lock(shared->activeMutex);
    --shared->activeWorkers;
}

void NamedPipeServer::Stop() {    if (!running_.exchange(false)) {
        return;
    }

    if (shared_) {
        shared_->running.store(false);
    }

    // Unblock ConnectNamedPipe by opening the pipe as a client would.
    HANDLE probe = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        // Closing immediately makes ConnectNamedPipe return ERROR_PIPE_CONNECTED.
        CloseHandle(probe);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // Give workers a moment to finish their current request.
    if (shared_) {
        for (int i = 0; i < 100; ++i) {
            std::lock_guard<std::mutex> lock(shared_->activeMutex);
            if (shared_->activeWorkers == 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    spdlog::info("named pipe server stopped");
}

std::uint64_t NamedPipeServer::servedCount() const {
    return shared_ ? shared_->served.load() : 0;
}

}  // namespace snowydesk::ipc
