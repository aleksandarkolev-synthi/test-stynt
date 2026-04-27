/**
 * Network Layer implementation - IXWebSocket based WebSocket hub and client.
 *
 * IXWebSocket drives its own I/O thread(s); we register lambda callbacks
 * for Open / Close / Message events.  No manual thread management here
 * beyond the optional persistence autosave thread.
 */
#include "network.h"

#include <chrono>
#include <iostream>

#include "logic.h"

namespace mcm::network {

namespace {

/**
 * encodeSnapshot - builds a HELLO or FULL_STATE frame from the server
 * collection snapshot. Internal.
 */
std::string encodeSnapshot(Server& server,
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
 * sendErrorTo - sends an ERROR_REPLY frame to a single WebSocket. Internal.
 */
void sendErrorTo(ix::WebSocket& ws, const std::string& text) {
    protocol::Message error;
    error.kind = protocol::MessageKind::ERROR_REPLY;
    error.errorText = text;
    ws.send(protocol::encodeMessage(error));
}

/**
 * handleClientRequest - validates and applies a single request from a client,
 * then broadcasts the resulting event or sends an error back. Internal.
 */
void handleClientRequest(Server& server,
                         ix::WebSocket& ws,
                         const std::string& raw) {
    protocol::Message request;
    if (!protocol::decodeMessage(raw, request)) {
        sendErrorTo(ws, "Malformed JSON frame.");
        return;
    }

    switch (request.kind) {
        case protocol::MessageKind::REQUEST_SYNC: {
            // Caller only needs a full snapshot.
            ws.send(encodeSnapshot(server, 0, protocol::MessageKind::FULL_STATE));
            break;
        }
        case protocol::MessageKind::REQUEST_ADD: {
            std::string validationError;
            if (!logic::validateMovie(request.payload, validationError)) {
                sendErrorTo(ws, validationError);
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
                sendErrorTo(ws, validationError);
                return;
            }
            if (!data::updateMovie(server.collection, request.payload)) {
                sendErrorTo(ws, "Movie id not found.");
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
                sendErrorTo(ws, "Movie id not found.");
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
            sendErrorTo(ws, "Unsupported message type.");
            break;
    }
}

/**
 * persistenceLoop - autosaves the collection every second when dirty. Internal.
 */
void persistenceLoop(Server* server) {
    while (server->running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (server->dirty.exchange(false)) {
            data::saveToFile(server->collection, server->persistencePath);
        }
    }
    // Final flush on shutdown.
    if (!server->persistencePath.empty()) {
        data::saveToFile(server->collection, server->persistencePath);
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

    server.wsServer = std::make_unique<ix::WebSocketServer>(
        static_cast<int>(port), "0.0.0.0");

    // The callback captures &server by reference.  It is called on IXWebSocket's
    // internal thread, so all server state accesses must be mutex-protected or
    // atomic.  server is guaranteed to outlive wsServer because stopServer()
    // calls wsServer->stop() before it returns.
    server.wsServer->setOnClientMessageCallback(
        [&server](std::shared_ptr<ix::ConnectionState> state,
                  ix::WebSocket& ws,
                  const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Open) {
                std::uint64_t sessionId;
                {
                    std::lock_guard<std::mutex> lock(server.clientsMutex);
                    sessionId = server.nextSessionId++;
                    server.clients[state->getId()] = &ws;
                }
                std::clog << "[server] client " << sessionId
                          << " connected (" << state->getRemoteIp() << ")\n";
                ws.send(encodeSnapshot(server, sessionId,
                                       protocol::MessageKind::HELLO));

            } else if (msg->type == ix::WebSocketMessageType::Close) {
                std::lock_guard<std::mutex> lock(server.clientsMutex);
                server.clients.erase(state->getId());
                std::clog << "[server] client disconnected ("
                          << state->getRemoteIp() << ")\n";

            } else if (msg->type == ix::WebSocketMessageType::Message) {
                handleClientRequest(server, ws, msg->str);

            } else if (msg->type == ix::WebSocketMessageType::Error) {
                std::clog << "[server] error: " << msg->errorInfo.reason << "\n";
                std::lock_guard<std::mutex> lock(server.clientsMutex);
                server.clients.erase(state->getId());
            }
        });

    auto [success, errorMessage] = server.wsServer->listen();
    if (!success) {
        std::cerr << "[server] listen failed: " << errorMessage << "\n";
        return false;
    }
    server.running.store(true);
    server.wsServer->start();

    if (!persistencePath.empty()) {
        server.persistenceThread = std::thread(persistenceLoop, &server);
    }
    return true;
}

void stopServer(Server& server) {
    if (!server.running.exchange(false)) {
        return;
    }
    if (server.wsServer) {
        server.wsServer->stop();
    }
    if (server.persistenceThread.joinable()) {
        server.persistenceThread.join();
    }
}

void broadcastMessage(Server& server, const std::string& text) {
    // Copy the client pointers under the lock; send without holding it so
    // we don't block new connections during a slow send.
    std::vector<ix::WebSocket*> snapshot;
    {
        std::lock_guard<std::mutex> lock(server.clientsMutex);
        snapshot.reserve(server.clients.size());
        for (auto& [id, ws] : server.clients) {
            snapshot.push_back(ws);
        }
    }
    for (ix::WebSocket* ws : snapshot) {
        ws->send(text);
    }
}

bool connectClient(Client& client, const std::string& host, const std::string& port) {
    if (client.connected.load()) {
        return true;
    }
    client.ws = std::make_unique<ix::WebSocket>();
    const std::string url = "ws://" + host + ":" + port;
    client.ws->setUrl(url);

    // Disable per-message deflate compression to keep it simple.
    ix::WebSocketPerMessageDeflateOptions deflate(false);
    client.ws->setPerMessageDeflateOptions(deflate);

    // The callback is called on IXWebSocket's I/O thread.
    client.ws->setOnMessageCallback(
        [&client](const ix::WebSocketMessagePtr& msg)
        {
            if (msg->type == ix::WebSocketMessageType::Open) {
                {
                    std::lock_guard<std::mutex> lock(client.connectMutex);
                    client.connected.store(true);
                    client.running.store(true);
                }
                client.connectCv.notify_one();

            } else if (msg->type == ix::WebSocketMessageType::Message) {
                std::lock_guard<std::mutex> lock(client.inboxMutex);
                client.inbox.push_back(msg->str);

            } else if (msg->type == ix::WebSocketMessageType::Close) {
                client.connected.store(false);

            } else if (msg->type == ix::WebSocketMessageType::Error) {
                {
                    std::lock_guard<std::mutex> lock(client.errorMutex);
                    client.lastError = msg->errorInfo.reason;
                }
                client.connected.store(false);
                // Wake connect waiter so it can time out cleanly.
                client.connectCv.notify_one();
            }
        });

    client.ws->start();

    // Block until connected or 5-second timeout.
    std::unique_lock<std::mutex> lock(client.connectMutex);
    const bool success = client.connectCv.wait_for(
        lock,
        std::chrono::seconds(5),
        [&client] { return client.connected.load(); });

    if (!success) {
        std::lock_guard<std::mutex> errorLock(client.errorMutex);
        if (client.lastError.empty()) {
            client.lastError = "Connection timed out.";
        }
    }
    return success;
}

void disconnectClient(Client& client) {
    client.running.store(false);
    if (client.ws) {
        client.ws->stop();
        client.ws.reset();
    }
    client.connected.store(false);
}

bool sendClientMessage(Client& client, const std::string& text) {
    if (!client.connected.load() || !client.ws) {
        return false;
    }
    const auto result = client.ws->send(text);
    return result.success;
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
