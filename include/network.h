/**
 * Network Layer - Movie Collection Manager
 *
 * WebSocket server (hub for all connected clients) and WebSocket client
 * (used by the desktop GUI) built on Boost.Beast and Boost.Asio.
 *
 * Strictly structural: no classes, no member functions. Sessions, clients
 * and server state are plain structs shared via std::shared_ptr where
 * required for asynchronous lifetime management.
 */
#ifndef MCM_NETWORK_H
#define MCM_NETWORK_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket.hpp>

#include "data.h"
#include "protocol.h"

namespace mcm::network {

/**
 * Session - per-client connection record on the server side.
 * Owned via std::shared_ptr so the reader thread may keep it alive.
 */
struct Session {
    std::uint64_t id = 0;
    std::unique_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> stream;
    std::thread readerThread;
    std::mutex writeMutex;
    std::atomic<bool> alive{true};
};

/**
 * Server - WebSocket server state. Holds the canonical collection and
 * the set of live sessions. All mutation is serialized by the data layer.
 */
struct Server {
    boost::asio::io_context ioContext;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    data::Collection collection;

    std::vector<std::shared_ptr<Session>> sessions;
    std::mutex sessionsMutex;
    std::uint64_t nextSessionId = 1;

    std::thread acceptorThread;
    std::atomic<bool> running{false};

    std::string persistencePath;
    std::atomic<bool> dirty{false};
    std::thread persistenceThread;
};

/**
 * Client - WebSocket client state. Owns the asio io_context, the socket
 * and a background reader thread that buffers incoming messages.
 */
struct Client {
    boost::asio::io_context ioContext;
    std::unique_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> stream;
    std::thread readerThread;
    std::mutex writeMutex;

    std::mutex inboxMutex;
    std::deque<std::string> inbox;

    std::atomic<bool> connected{false};
    std::atomic<bool> running{false};
    std::string lastError;
    std::mutex errorMutex;

    std::uint64_t sessionId = 0;
};

/**
 * startServer - binds to the given port and begins accepting WebSocket
 * clients in the background. Returns once the acceptor is listening.
 *
 * @param server           Target server state (mutated).
 * @param port             TCP port number to listen on.
 * @param persistencePath  Optional JSON file to autosave the collection
 *                         on every change. Pass an empty string to skip.
 * @return true on success, false if binding failed.
 */
bool startServer(Server& server, std::uint16_t port, const std::string& persistencePath);

/**
 * stopServer - gracefully shuts down the server and all sessions.
 *
 * @param server Target server.
 */
void stopServer(Server& server);

/**
 * broadcastMessage - pushes a single encoded frame to every live session.
 * Failed sends mark the session as dead; the janitor will reap it.
 *
 * @param server Source server.
 * @param text   Encoded JSON frame.
 */
void broadcastMessage(Server& server, const std::string& text);

/**
 * connectClient - performs the WebSocket handshake to the given host/port.
 *
 * @param client Target client state.
 * @param host   Hostname or IP of the server.
 * @param port   Port string (Asio resolver convention).
 * @return true on success; populate client.lastError otherwise.
 */
bool connectClient(Client& client, const std::string& host, const std::string& port);

/**
 * disconnectClient - closes the socket and joins the reader thread.
 *
 * @param client Target client.
 */
void disconnectClient(Client& client);

/**
 * sendClientMessage - transmits a single frame to the server.
 *
 * @param client Target client.
 * @param text   Encoded JSON frame.
 * @return true on success.
 */
bool sendClientMessage(Client& client, const std::string& text);

/**
 * drainInbox - atomically moves every buffered inbound frame out of the
 * client and returns them to the caller (typically the GUI thread).
 *
 * @param client Source client.
 * @return Zero or more JSON strings in arrival order.
 */
std::vector<std::string> drainInbox(Client& client);

/**
 * lastClientError - retrieves the latest error string captured by the
 * reader thread, if any.
 *
 * @param client Source client.
 * @return Error text (possibly empty).
 */
std::string lastClientError(Client& client);

} // namespace mcm::network

#endif // MCM_NETWORK_H
