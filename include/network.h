/**
 * Network Layer - Movie Collection Manager
 *
 * WebSocket server (hub for all connected clients) and WebSocket client
 * (used by the desktop GUI) built on IXWebSocket. No TLS required.
 *
 * Strictly structural: no classes, no member functions. All state lives
 * in plain structs; IXWebSocket internals are stored via std::unique_ptr
 * to satisfy forward-declaration requirements, never subclassed.
 *
 * Thread-safety contract:
 *   - IXWebSocket drives its own I/O threads.  Callbacks run on those
 *     threads, so any field touched by both a callback and the main
 *     thread must be protected by a mutex or be std::atomic.
 *   - The data::Collection inside Server is protected by its own internal
 *     mutex (see data.h).
 */
#ifndef MCM_NETWORK_H
#define MCM_NETWORK_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include "data.h"
#include "protocol.h"

namespace mcm::network {

/**
 * Server - WebSocket hub state.
 *
 * wsServer      IXWebSocket server object (owns listener + per-client threads).
 * collection    Canonical movie collection.  All mutations use the data-layer
 *               helpers which serialise internally.
 * clients       Map from IXWebSocket connection-id string to raw WebSocket
 *               pointer.  The pointer is valid for the lifetime of the
 *               connection; it is inserted on Open and erased on Close.
 *               Access must be guarded by clientsMutex.
 * nextSessionId Monotonic id counter assigned to each new connection.
 * persistencePath  If non-empty, the collection is auto-saved here.
 * dirty         Flags that a save is needed; the persistence thread clears it.
 */
struct Server {
    std::unique_ptr<ix::WebSocketServer> wsServer;
    data::Collection collection;
    std::atomic<bool> running{false};

    std::mutex clientsMutex;
    std::map<std::string, ix::WebSocket*> clients;
    std::uint64_t nextSessionId = 1;

    std::string persistencePath;
    std::atomic<bool> dirty{false};
    std::thread persistenceThread;
};

/**
 * Client - WebSocket client state for the desktop GUI.
 *
 * ws            IXWebSocket client object (owns its I/O thread).
 * inbox         Buffered inbound text frames; drained by the UI thread each
 *               render frame via drainInbox().
 * connected     true after the Open event, false after Close or Error.
 * connectCv     Condition variable used in connectClient() to block until
 *               the handshake succeeds or the timeout expires.
 * sessionId     Session id assigned by the first HELLO frame from the server.
 */
struct Client {
    std::unique_ptr<ix::WebSocket> ws;

    std::mutex inboxMutex;
    std::deque<std::string> inbox;

    std::atomic<bool> connected{false};
    std::string lastError;
    std::mutex errorMutex;

    std::mutex connectMutex;
    std::condition_variable connectCv;

    std::uint64_t sessionId = 0;
    std::atomic<bool> running{false};
};

/**
 * startServer - binds to the given port, registers the per-client message
 * callback, and begins accepting WebSocket clients.  Returns once the
 * server is listening.
 *
 * @param server           Target server state (mutated).
 * @param port             TCP port number to listen on.
 * @param persistencePath  Optional JSON file to autosave on every change.
 * @return true on success.
 */
bool startServer(Server& server, std::uint16_t port, const std::string& persistencePath);

/**
 * stopServer - gracefully shuts down all sessions and the listener.
 *
 * @param server Target server.
 */
void stopServer(Server& server);

/**
 * broadcastMessage - sends a text frame to every currently-connected client.
 *
 * @param server  Source server.
 * @param text    Encoded JSON frame.
 */
void broadcastMessage(Server& server, const std::string& text);

/**
 * connectClient - starts the WebSocket client and blocks until the handshake
 * completes (or a 5-second timeout fires).
 *
 * @param client Target client state.
 * @param host   Hostname or IP of the server.
 * @param port   Port string.
 * @return true on success; client.lastError is populated on failure.
 */
bool connectClient(Client& client, const std::string& host, const std::string& port);

/**
 * disconnectClient - closes the socket and resets client state.
 *
 * @param client Target client.
 */
void disconnectClient(Client& client);

/**
 * sendClientMessage - transmits a single text frame to the server.
 *
 * @param client Target client.
 * @param text   Encoded JSON frame.
 * @return true on success.
 */
bool sendClientMessage(Client& client, const std::string& text);

/**
 * drainInbox - atomically moves every buffered inbound frame to the caller.
 *
 * @param client Source client.
 * @return Zero or more JSON strings in arrival order.
 */
std::vector<std::string> drainInbox(Client& client);

/**
 * lastClientError - returns the latest error captured by the I/O thread.
 *
 * @param client Source client.
 * @return Error text (possibly empty).
 */
std::string lastClientError(Client& client);

} // namespace mcm::network

#endif // MCM_NETWORK_H
