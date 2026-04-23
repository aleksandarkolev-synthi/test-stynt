/**
 * Network Layer implementation - Boost.Beast based WebSocket
 * server and client. Structural style: all free functions operating
 * on the Session / Server / Client structs.
 */
#include "network.h"

#include <chrono>
#include <iostream>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/system/error_code.hpp>

#include "logic.h"

namespace mcm::network {

namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using boost::asio::ip::tcp;

namespace {

/**
 * encodeSnapshotForSession - builds a HELLO or FULL_STATE frame from
 * the current canonical collection. Internal.
 */
std::string encodeSnapshotForSession(Server& server,
                                     std::uint64_t sessionId,
                                     protocol::MessageKind kind) {
    protocol::Message message;
    message.kind = kind;
    message.sessionId = sessionId;
    message.snapshot = data::snapshotMovies(server.collection);
    message.revision = data::currentRevision(server.collection);
    return protocol::encodeMessage(message);
}

/**
 * writeFrame - sends a text frame over a websocket stream under the given
 * mutex. Returns false on socket error. Internal.
 */
bool writeFrame(websocket::stream<beast::tcp_stream>& stream,
                std::mutex& writeMutex,
                const std::string& text) {
    std::lock_guard<std::mutex> lock(writeMutex);
    boost::system::error_code errorCode;
    stream.text(true);
    stream.write(boost::asio::buffer(text), errorCode);
    return !errorCode;
}

/**
 * sendToSession - convenience wrapper used across the server code path. Internal.
 */
bool sendToSession(Session& session, const std::string& text) {
    if (!session.alive.load() || !session.stream) {
        return false;
    }
    if (!writeFrame(*session.stream, session.writeMutex, text)) {
        session.alive.store(false);
        return false;
    }
    return true;
}

/**
 * sendErrorReply - emits an ERROR_REPLY frame to a single session. Internal.
 */
void sendErrorReply(Session& session, const std::string& text) {
    protocol::Message error;
    error.kind = protocol::MessageKind::ERROR_REPLY;
    error.sessionId = session.id;
    error.errorText = text;
    sendToSession(session, protocol::encodeMessage(error));
}

/**
 * reapDeadSessions - removes sessions flagged dead and joins their reader
 * threads. Internal.
 */
void reapDeadSessions(Server& server) {
    std::vector<std::shared_ptr<Session>> reaped;
    {
        std::lock_guard<std::mutex> lock(server.sessionsMutex);
        auto iterator = server.sessions.begin();
        while (iterator != server.sessions.end()) {
            if (!(*iterator)->alive.load()) {
                reaped.push_back(*iterator);
                iterator = server.sessions.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }
    for (auto& session : reaped) {
        if (session->stream) {
            boost::system::error_code ignored;
            session->stream->close(websocket::close_code::normal, ignored);
        }
        if (session->readerThread.joinable()
            && session->readerThread.get_id() != std::this_thread::get_id()) {
            session->readerThread.join();
        }
    }
}

/**
 * handleRequest - applies a client request to the canonical collection
 * and broadcasts the resulting event. Internal.
 */
void handleRequest(Server& server,
                   Session& session,
                   const protocol::Message& request) {
    switch (request.kind) {
        case protocol::MessageKind::REQUEST_SYNC: {
            sendToSession(session,
                encodeSnapshotForSession(server, session.id,
                                         protocol::MessageKind::FULL_STATE));
            break;
        }
        case protocol::MessageKind::REQUEST_ADD: {
            std::string validationError;
            if (!logic::validateMovie(request.payload, validationError)) {
                sendErrorReply(session, validationError);
                return;
            }
            const std::uint64_t newId = data::addMovie(server.collection, request.payload);
            data::Movie stored = request.payload;
            stored.id = newId;

            protocol::Message event;
            event.kind = protocol::MessageKind::EVENT_ADDED;
            event.payload = stored;
            event.revision = data::currentRevision(server.collection);
            broadcastMessage(server, protocol::encodeMessage(event));
            server.dirty.store(true);
            break;
        }
        case protocol::MessageKind::REQUEST_UPDATE: {
            std::string validationError;
            if (!logic::validateMovie(request.payload, validationError)) {
                sendErrorReply(session, validationError);
                return;
            }
            if (!data::updateMovie(server.collection, request.payload)) {
                sendErrorReply(session, "Movie id not found.");
                return;
            }
            protocol::Message event;
            event.kind = protocol::MessageKind::EVENT_UPDATED;
            event.payload = request.payload;
            event.revision = data::currentRevision(server.collection);
            broadcastMessage(server, protocol::encodeMessage(event));
            server.dirty.store(true);
            break;
        }
        case protocol::MessageKind::REQUEST_REMOVE: {
            if (!data::removeMovie(server.collection, request.targetId)) {
                sendErrorReply(session, "Movie id not found.");
                return;
            }
            protocol::Message event;
            event.kind = protocol::MessageKind::EVENT_REMOVED;
            event.targetId = request.targetId;
            event.revision = data::currentRevision(server.collection);
            broadcastMessage(server, protocol::encodeMessage(event));
            server.dirty.store(true);
            break;
        }
        default:
            sendErrorReply(session, "Unsupported message type.");
            break;
    }
}

/**
 * runSessionReader - main loop for a single connected client on the server.
 * Runs on its own thread. Internal.
 */
void runSessionReader(Server* server, std::shared_ptr<Session> sessionPtr) {
    Session& session = *sessionPtr;

    // Send the greeting snapshot up front.
    sendToSession(session,
        encodeSnapshotForSession(*server, session.id,
                                 protocol::MessageKind::HELLO));

    while (session.alive.load() && server->running.load()) {
        beast::flat_buffer buffer;
        boost::system::error_code errorCode;
        session.stream->read(buffer, errorCode);
        if (errorCode == websocket::error::closed || errorCode) {
            session.alive.store(false);
            break;
        }
        const std::string text = beast::buffers_to_string(buffer.data());
        protocol::Message request;
        if (!protocol::decodeMessage(text, request)) {
            sendErrorReply(session, "Malformed JSON frame.");
            continue;
        }
        handleRequest(*server, session, request);
    }
}

/**
 * acceptorLoop - accepts new connections and spawns a reader thread
 * per session. Internal.
 */
void acceptorLoop(Server* server) {
    while (server->running.load()) {
        boost::system::error_code errorCode;
        tcp::socket socket(server->ioContext);
        server->acceptor->accept(socket, errorCode);
        if (errorCode) {
            if (server->running.load()) {
                std::cerr << "[server] accept error: " << errorCode.message() << "\n";
            }
            continue;
        }

        auto session = std::make_shared<Session>();
        session->stream = std::make_unique<websocket::stream<beast::tcp_stream>>(std::move(socket));
        session->stream->set_option(websocket::stream_base::decorator(
            [](websocket::response_type& response) {
                response.set(boost::beast::http::field::server, "mcm-server/1.0");
            }));
        session->stream->accept(errorCode);
        if (errorCode) {
            std::cerr << "[server] ws accept error: " << errorCode.message() << "\n";
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(server->sessionsMutex);
            session->id = server->nextSessionId++;
            server->sessions.push_back(session);
        }
        std::cerr << "[server] client " << session->id << " connected\n";

        session->readerThread = std::thread(runSessionReader, server, session);
        reapDeadSessions(*server);
    }
}

/**
 * persistenceLoop - autosaves the collection once per second whenever
 * the dirty flag is set. Internal.
 */
void persistenceLoop(Server* server) {
    while (server->running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (server->dirty.exchange(false)) {
            data::saveToFile(server->collection, server->persistencePath);
        }
    }
    if (!server->persistencePath.empty()) {
        data::saveToFile(server->collection, server->persistencePath);
    }
}

/**
 * runClientReader - continually reads frames from the server and buffers
 * them for the UI thread. Internal.
 */
void runClientReader(Client* client) {
    while (client->running.load()) {
        beast::flat_buffer buffer;
        boost::system::error_code errorCode;
        client->stream->read(buffer, errorCode);
        if (errorCode == websocket::error::closed || errorCode) {
            {
                std::lock_guard<std::mutex> lock(client->errorMutex);
                client->lastError = errorCode.message();
            }
            client->connected.store(false);
            break;
        }
        const std::string text = beast::buffers_to_string(buffer.data());
        std::lock_guard<std::mutex> lock(client->inboxMutex);
        client->inbox.push_back(text);
    }
}

} // namespace

bool startServer(Server& server, std::uint16_t port, const std::string& persistencePath) {
    if (server.running.load()) {
        return false;
    }
    server.persistencePath = persistencePath;
    if (!persistencePath.empty()) {
        data::loadFromFile(server.collection, persistencePath);
    }

    boost::system::error_code errorCode;
    server.acceptor = std::make_unique<tcp::acceptor>(server.ioContext);
    const tcp::endpoint endpoint(tcp::v4(), port);

    server.acceptor->open(endpoint.protocol(), errorCode);
    if (errorCode) {
        std::cerr << "[server] open failed: " << errorCode.message() << "\n";
        return false;
    }
    server.acceptor->set_option(boost::asio::socket_base::reuse_address(true), errorCode);
    server.acceptor->bind(endpoint, errorCode);
    if (errorCode) {
        std::cerr << "[server] bind failed: " << errorCode.message() << "\n";
        return false;
    }
    server.acceptor->listen(boost::asio::socket_base::max_listen_connections, errorCode);
    if (errorCode) {
        std::cerr << "[server] listen failed: " << errorCode.message() << "\n";
        return false;
    }

    server.running.store(true);
    server.acceptorThread = std::thread(acceptorLoop, &server);
    if (!persistencePath.empty()) {
        server.persistenceThread = std::thread(persistenceLoop, &server);
    }
    return true;
}

void stopServer(Server& server) {
    if (!server.running.exchange(false)) {
        return;
    }
    boost::system::error_code ignored;
    if (server.acceptor) {
        server.acceptor->close(ignored);
    }
    if (server.acceptorThread.joinable()) {
        server.acceptorThread.join();
    }

    std::vector<std::shared_ptr<Session>> toClose;
    {
        std::lock_guard<std::mutex> lock(server.sessionsMutex);
        toClose = std::move(server.sessions);
        server.sessions.clear();
    }
    for (auto& session : toClose) {
        session->alive.store(false);
        if (session->stream) {
            session->stream->close(websocket::close_code::normal, ignored);
        }
    }
    for (auto& session : toClose) {
        if (session->readerThread.joinable()) {
            session->readerThread.join();
        }
    }
    if (server.persistenceThread.joinable()) {
        server.persistenceThread.join();
    }
}

void broadcastMessage(Server& server, const std::string& text) {
    std::vector<std::shared_ptr<Session>> snapshot;
    {
        std::lock_guard<std::mutex> lock(server.sessionsMutex);
        snapshot = server.sessions;
    }
    for (auto& session : snapshot) {
        sendToSession(*session, text);
    }
    reapDeadSessions(server);
}

bool connectClient(Client& client, const std::string& host, const std::string& port) {
    if (client.connected.load()) {
        return true;
    }
    try {
        tcp::resolver resolver(client.ioContext);
        const auto results = resolver.resolve(host, port);

        client.stream = std::make_unique<websocket::stream<beast::tcp_stream>>(client.ioContext);
        boost::beast::get_lowest_layer(*client.stream).connect(results);
        client.stream->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& request) {
                request.set(boost::beast::http::field::user_agent, "mcm-client/1.0");
            }));
        client.stream->handshake(host + ":" + port, "/");
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lock(client.errorMutex);
        client.lastError = ex.what();
        return false;
    }

    client.connected.store(true);
    client.running.store(true);
    client.readerThread = std::thread(runClientReader, &client);
    return true;
}

void disconnectClient(Client& client) {
    if (!client.running.exchange(false)) {
        return;
    }
    client.connected.store(false);
    if (client.stream) {
        boost::system::error_code ignored;
        client.stream->close(websocket::close_code::normal, ignored);
    }
    if (client.readerThread.joinable()) {
        client.readerThread.join();
    }
}

bool sendClientMessage(Client& client, const std::string& text) {
    if (!client.connected.load() || !client.stream) {
        return false;
    }
    if (!writeFrame(*client.stream, client.writeMutex, text)) {
        client.connected.store(false);
        return false;
    }
    return true;
}

std::vector<std::string> drainInbox(Client& client) {
    std::vector<std::string> drained;
    std::lock_guard<std::mutex> lock(client.inboxMutex);
    while (!client.inbox.empty()) {
        drained.push_back(std::move(client.inbox.front()));
        client.inbox.pop_front();
    }
    return drained;
}

std::string lastClientError(Client& client) {
    std::lock_guard<std::mutex> lock(client.errorMutex);
    return client.lastError;
}

} // namespace mcm::network
