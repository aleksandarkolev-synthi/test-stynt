/**
 * Protocol - wire format for the Movie Collection Manager.
 *
 * Declares the JSON message types that travel between server and clients.
 * Structural only: enum, structs, free encode/decode functions.
 */
#ifndef MCM_PROTOCOL_H
#define MCM_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

#include "data.h"

namespace mcm::protocol {

/**
 * MessageKind - high-level category of a protocol frame.
 */
enum class MessageKind {
    UNKNOWN,
    HELLO,          // server -> client   : session greeting with snapshot
    FULL_STATE,     // server -> client   : full re-sync snapshot
    EVENT_ADDED,    // server -> clients  : broadcast of a new movie
    EVENT_UPDATED,  // server -> clients  : broadcast of an updated movie
    EVENT_REMOVED,  // server -> clients  : broadcast of a removal
    REQUEST_ADD,    // client -> server   : ask to add a movie
    REQUEST_UPDATE, // client -> server   : ask to update a movie
    REQUEST_REMOVE, // client -> server   : ask to remove a movie
    REQUEST_SYNC,   // client -> server   : ask for a FULL_STATE frame
    ERROR_REPLY,    // server -> client   : report an invalid request
};

/**
 * Message - decoded representation of a protocol frame.
 * Only the fields relevant to `kind` will be populated.
 */
struct Message {
    MessageKind kind = MessageKind::UNKNOWN;
    std::uint64_t revision = 0;
    std::uint64_t sessionId = 0;
    std::uint64_t targetId = 0;        // id used by REQUEST_REMOVE / EVENT_REMOVED
    data::Movie payload;                // movie attached to add / update events
    std::vector<data::Movie> snapshot;  // used by HELLO and FULL_STATE
    std::string errorText;              // used by ERROR_REPLY
};

/**
 * encodeMessage - serialises a Message into a JSON string.
 *
 * @param message Structured message to serialise.
 * @return JSON text ready to send over the WebSocket.
 */
std::string encodeMessage(const Message& message);

/**
 * decodeMessage - parses a JSON string into a Message.
 *
 * @param text    Received JSON text.
 * @param out     Destination structure (populated on success).
 * @return true on success, false if the text was malformed.
 */
bool decodeMessage(const std::string& text, Message& out);

} // namespace mcm::protocol

#endif // MCM_PROTOCOL_H
