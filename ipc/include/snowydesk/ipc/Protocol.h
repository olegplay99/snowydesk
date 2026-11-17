#pragma once

// Wire protocol for \\.\pipe\SnowyDesk.
//
// Framing: one JSON object per line, UTF-8, terminated by '\n'.
// Every frame carries "v" (protocol version) and "id" (request correlation id).
//
// Request:  {"v":1,"id":3,"type":"get_state","payload":{}}
// Response: {"v":1,"id":3,"ok":true,"payload":{...}}
//           {"v":1,"id":3,"ok":false,"error":"reason"}
//
// Message types (v1):
//   ping              payload {} -> {"pong":true,"service":"SnowyDeskService","protocol":1}
//   get_state         payload {} -> {"state":{<schema v1 document>}}
//   set_state         payload {"state":{...}} -> {"saved":true}
//   apply_settings    payload {"settings":{...}} -> {"saved":true,"settings":{...}}
//   clear_taskbar_snow payload {} -> {"removed":<n>}

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "snowydesk/common/Types.h"

namespace snowydesk::ipc {

enum class MessageType {
    Ping,
    GetState,
    SetState,
    ApplySettings,
    ClearTaskbarSnow,
    Unknown,
};

// Hard cap for a single frame; protects both peers from memory abuse.
inline constexpr std::size_t kMaxFrameBytes = 4u * 1024u * 1024u;

struct Request {
    int v = kProtocolVersion;
    int id = 0;
    MessageType type = MessageType::Unknown;
    std::string typeName = "unknown";
    nlohmann::json payload = nlohmann::json::object();
};

struct Response {
    int v = kProtocolVersion;
    int id = 0;
    bool ok = false;
    std::string error;
    nlohmann::json payload = nlohmann::json::object();
};

const char* ToString(MessageType type);
MessageType MessageTypeFromString(std::string_view name);

std::string EncodeRequest(const Request& request);
std::string EncodeResponse(const Response& response);

std::optional<Request> DecodeRequest(std::string_view frame, std::string& error);
std::optional<Response> DecodeResponse(std::string_view frame, std::string& error);

// Builders shared by the service and the UI so both sides cannot disagree.
Request MakePing(int id);
Request MakeGetState(int id);
Request MakeSetState(int id, const AppState& state);
Request MakeApplySettings(int id, const Settings& settings);
Request MakeClearTaskbarSnow(int id);

// Validates and extracts the "state" member of a set_state payload.
std::optional<AppState> ReadStatePayload(const nlohmann::json& payload, std::string& error);
std::optional<Settings> ReadSettingsPayload(const nlohmann::json& payload, std::string& error);

}  // namespace snowydesk::ipc
