#include "snowydesk/ipc/Protocol.h"

#include "snowydesk/common/Json.h"

namespace snowydesk::ipc {

const char* ToString(MessageType type) {
    switch (type) {
        case MessageType::Ping:
            return "ping";
        case MessageType::GetState:
            return "get_state";
        case MessageType::SetState:
            return "set_state";
        case MessageType::ApplySettings:
            return "apply_settings";
        case MessageType::ClearTaskbarSnow:
            return "clear_taskbar_snow";
        case MessageType::Unknown:
            break;
    }
    return "unknown";
}

MessageType MessageTypeFromString(std::string_view name) {
    if (name == "ping") return MessageType::Ping;
    if (name == "get_state") return MessageType::GetState;
    if (name == "set_state") return MessageType::SetState;
    if (name == "apply_settings") return MessageType::ApplySettings;
    if (name == "clear_taskbar_snow") return MessageType::ClearTaskbarSnow;
    return MessageType::Unknown;
}

std::string EncodeRequest(const Request& request) {
    const nlohmann::json frame{
        {"v", kProtocolVersion},
        {"id", request.id},
        {"type", ToString(request.type)},
        {"payload", request.payload.is_null() ? nlohmann::json::object() : request.payload},
    };
    return frame.dump();
}

std::string EncodeResponse(const Response& response) {
    nlohmann::json frame{
        {"v", kProtocolVersion},
        {"id", response.id},
        {"ok", response.ok},
        {"payload", response.payload.is_null() ? nlohmann::json::object() : response.payload},
    };
    if (!response.ok) {
        frame["error"] = response.error;
    }
    return frame.dump();
}

std::optional<Request> DecodeRequest(std::string_view frame, std::string& error) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(frame.begin(), frame.end());
    } catch (const std::exception& ex) {
        error = std::string("malformed JSON frame: ") + ex.what();
        return std::nullopt;
    }

    if (!parsed.is_object()) {
        error = "frame is not a JSON object";
        return std::nullopt;
    }

    const int version = parsed.value("v", 0);
    if (version != kProtocolVersion) {
        error = "unsupported protocol version " + std::to_string(version) + " (expected " +
                std::to_string(kProtocolVersion) + ")";
        return std::nullopt;
    }

    Request request;
    request.v = version;
    request.id = parsed.value("id", 0);
    request.typeName = parsed.value("type", std::string("unknown"));
    request.type = MessageTypeFromString(request.typeName);
    if (request.type == MessageType::Unknown) {
        error = "unknown message type '" + request.typeName + "'";
        return std::nullopt;
    }

    if (parsed.contains("payload") && parsed.at("payload").is_object()) {
        request.payload = parsed.at("payload");
    }
    return request;
}

std::optional<Response> DecodeResponse(std::string_view frame, std::string& error) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(frame.begin(), frame.end());
    } catch (const std::exception& ex) {
        error = std::string("malformed JSON frame: ") + ex.what();
        return std::nullopt;
    }

    if (!parsed.is_object()) {
        error = "frame is not a JSON object";
        return std::nullopt;
    }

    Response response;
    response.v = parsed.value("v", 0);
    if (response.v != kProtocolVersion) {
        error = "unsupported protocol version " + std::to_string(response.v);
        return std::nullopt;
    }

    response.id = parsed.value("id", 0);
    response.ok = parsed.value("ok", false);
    response.error = parsed.value("error", std::string{});
    if (parsed.contains("payload") && parsed.at("payload").is_object()) {
        response.payload = parsed.at("payload");
    }
    return response;
}

Request MakePing(int id) {
    Request request;
    request.id = id;
    request.type = MessageType::Ping;
    return request;
}

Request MakeGetState(int id) {
    Request request;
    request.id = id;
    request.type = MessageType::GetState;
    return request;
}

Request MakeSetState(int id, const AppState& state) {
    Request request;
    request.id = id;
    request.type = MessageType::SetState;
    request.payload = nlohmann::json{{"state", StateToJson(state)}};
    return request;
}

Request MakeApplySettings(int id, const Settings& settings) {
    Request request;
    request.id = id;
    request.type = MessageType::ApplySettings;
    request.payload = nlohmann::json{{"settings", SettingsToJson(settings)}};
    return request;
}

Request MakeClearTaskbarSnow(int id) {
    Request request;
    request.id = id;
    request.type = MessageType::ClearTaskbarSnow;
    return request;
}

std::optional<AppState> ReadStatePayload(const nlohmann::json& payload, std::string& error) {
    if (!payload.contains("state")) {
        error = "payload is missing \"state\"";
        return std::nullopt;
    }
    AppState state;
    if (!StateFromJson(payload.at("state"), state, error)) {
        return std::nullopt;
    }
    return state;
}

std::optional<Settings> ReadSettingsPayload(const nlohmann::json& payload, std::string& error) {
    if (!payload.contains("settings")) {
        error = "payload is missing \"settings\"";
        return std::nullopt;
    }
    Settings settings = SettingsFromJson(payload.at("settings"));
    const std::string validation = ValidateSettings(settings);
    if (!validation.empty()) {
        error = validation;
        return std::nullopt;
    }
    return settings;
}

}  // namespace snowydesk::ipc
