#include "snowydesk/ipc/PipeClient.h"

#include <windows.h>

#include <chrono>
#include <thread>
#include <vector>

namespace snowydesk::ipc {
namespace {

constexpr DWORD kReadChunk = 8192;

bool ReadFrame(HANDLE pipe, std::string& out, std::string& error) {
    std::vector<char> buffer(kReadChunk);
    out.clear();

    for (;;) {
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            const DWORD code = GetLastError();
            error = code == ERROR_BROKEN_PIPE ? "service closed the pipe"
                                              : "ReadFile failed with error " + std::to_string(code);
            return false;
        }
        if (read == 0) {
            error = "service closed the pipe";
            return false;
        }
        out.append(buffer.data(), read);
        if (out.size() > kMaxFrameBytes) {
            error = "response exceeds the maximum allowed size";
            return false;
        }
        if (out.find('\n') != std::string::npos) {
            return true;
        }
    }
}

}  // namespace

NamedPipeClient::NamedPipeClient(std::wstring pipeName) : pipeName_(std::move(pipeName)) {}

bool NamedPipeClient::Call(const Request& request, Response& response, std::string& error,
                           unsigned int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        pipe = CreateFileW(pipeName_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) break;

        const DWORD code = GetLastError();
        if (code != ERROR_PIPE_BUSY) {
            error = "cannot connect to \\\\.\\pipe\\SnowyDesk (error " + std::to_string(code) + ")";
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            error = "service is busy (timeout)";
            return false;
        }
        WaitNamedPipeW(pipeName_.c_str(), 200);
    }

    // Byte mode, blocking reads: no SetNamedPipeHandleState needed.

    std::string frame = EncodeRequest(request);
    frame.push_back('\n');

    DWORD written = 0;
    if (!WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr)) {
        error = "WriteFile failed with error " + std::to_string(GetLastError());
        CloseHandle(pipe);
        return false;
    }
    FlushFileBuffers(pipe);

    std::string incoming;
    const bool readOk = ReadFrame(pipe, incoming, error);
    CloseHandle(pipe);
    if (!readOk) {
        return false;
    }

    const auto newline = incoming.find('\n');
    const std::string_view one = newline == std::string::npos
                                     ? std::string_view(incoming)
                                     : std::string_view(incoming).substr(0, newline);

    std::string parseError;
    const auto decoded = DecodeResponse(one, parseError);
    if (!decoded) {
        error = parseError;
        return false;
    }

    response = *decoded;
    if (!response.ok) {
        error = response.error;
    }
    return response.ok;
}

bool NamedPipeClient::Ping(unsigned int timeoutMs) {
    Response response;
    std::string error;
    return Call(MakePing(0), response, error, timeoutMs);
}

}  // namespace snowydesk::ipc
