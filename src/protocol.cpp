/**
 * Protocol implementation - JSON <-> Message converters.
 */
#include "protocol.h"

#include <unordered_map>

#include <nlohmann/json.hpp>

namespace mcm::protocol {

namespace {

const std::unordered_map<MessageKind, std::string> KIND_TO_STRING = {
    {MessageKind::HELLO,          "hello"},
    {MessageKind::FULL_STATE,     "full_state"},
    {MessageKind::EVENT_ADDED,    "event_added"},
    {MessageKind::EVENT_UPDATED,  "event_updated"},
    {MessageKind::EVENT_REMOVED,  "event_removed"},
    {MessageKind::REQUEST_ADD,    "request_add"},
    {MessageKind::REQUEST_UPDATE, "request_update"},
    {MessageKind::REQUEST_REMOVE, "request_remove"},
    {MessageKind::REQUEST_SYNC,   "request_sync"},
    {MessageKind::ERROR_REPLY,    "error"},
};

/**
 * kindFromString - reverse lookup for the "type" field. Internal.
 */
MessageKind kindFromString(const std::string& text) {
    for (const auto& entry : KIND_TO_STRING) {
        if (entry.second == text) {
            return entry.first;
        }
    }
    return MessageKind::UNKNOWN;
}

/**
 * movieToJson - JSON encoder for a Movie. Internal.
 */
nlohmann::json movieToJson(const data::Movie& movie) {
    return nlohmann::json{
        {"id", movie.id},
        {"title", movie.title},
        {"year", movie.year},
        {"rating", movie.rating},
        {"durationMinutes", movie.durationMinutes},
    };
}

/**
 * movieFromJson - JSON decoder for a Movie. Internal.
 */
data::Movie movieFromJson(const nlohmann::json& node) {
    data::Movie movie;
    movie.id = node.value("id", std::uint64_t{0});
    movie.title = node.value("title", std::string{});
    movie.year = node.value("year", 0);
    movie.rating = node.value("rating", 0.0f);
    movie.durationMinutes = node.value("durationMinutes", 0);
    return movie;
}

} // namespace

std::string encodeMessage(const Message& message) {
    nlohmann::json root;
    auto kindEntry = KIND_TO_STRING.find(message.kind);
    root["type"] = (kindEntry != KIND_TO_STRING.end()) ? kindEntry->second : "unknown";
    root["revision"] = message.revision;
    root["sessionId"] = message.sessionId;

    switch (message.kind) {
        case MessageKind::HELLO:
        case MessageKind::FULL_STATE: {
            nlohmann::json array = nlohmann::json::array();
            for (const data::Movie& movie : message.snapshot) {
                array.push_back(movieToJson(movie));
            }
            root["movies"] = std::move(array);
            break;
        }
        case MessageKind::EVENT_ADDED:
        case MessageKind::EVENT_UPDATED:
        case MessageKind::REQUEST_ADD:
        case MessageKind::REQUEST_UPDATE:
            root["movie"] = movieToJson(message.payload);
            break;
        case MessageKind::EVENT_REMOVED:
        case MessageKind::REQUEST_REMOVE:
            root["targetId"] = message.targetId;
            break;
        case MessageKind::ERROR_REPLY:
            root["error"] = message.errorText;
            break;
        case MessageKind::REQUEST_SYNC:
        case MessageKind::UNKNOWN:
        default:
            break;
    }
    return root.dump();
}

bool decodeMessage(const std::string& text, Message& out) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
    if (!root.is_object() || !root.contains("type") || !root["type"].is_string()) {
        return false;
    }

    out = Message{};
    out.kind = kindFromString(root["type"].get<std::string>());
    out.revision = root.value("revision", std::uint64_t{0});
    out.sessionId = root.value("sessionId", std::uint64_t{0});

    switch (out.kind) {
        case MessageKind::HELLO:
        case MessageKind::FULL_STATE: {
            if (root.contains("movies") && root["movies"].is_array()) {
                for (const auto& node : root["movies"]) {
                    out.snapshot.push_back(movieFromJson(node));
                }
            }
            break;
        }
        case MessageKind::EVENT_ADDED:
        case MessageKind::EVENT_UPDATED:
        case MessageKind::REQUEST_ADD:
        case MessageKind::REQUEST_UPDATE: {
            if (root.contains("movie") && root["movie"].is_object()) {
                out.payload = movieFromJson(root["movie"]);
            }
            break;
        }
        case MessageKind::EVENT_REMOVED:
        case MessageKind::REQUEST_REMOVE:
            out.targetId = root.value("targetId", std::uint64_t{0});
            break;
        case MessageKind::ERROR_REPLY:
            out.errorText = root.value("error", std::string{});
            break;
        case MessageKind::REQUEST_SYNC:
        case MessageKind::UNKNOWN:
        default:
            break;
    }
    return true;
}

} // namespace mcm::protocol
