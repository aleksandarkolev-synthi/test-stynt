/**
 * Movie Collection Manager - unified entry point.
 *
 * Usage:
 *   mcm server [port] [persistence_path]
 *   mcm client [host] [port]
 *
 * `server` mode starts the WebSocket hub that brokers real-time updates
 * between every connected client. `client` mode launches the Dear ImGui
 * GUI that talks to such a hub.
 *
 * Structural programming only: everything below is free functions over
 * plain data, no classes defined locally.
 */
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "network.h"

#if defined(MCM_HAS_IMGUI)
#include "presentation.h"
#endif

namespace {

/**
 * SHUTDOWN_REQUESTED - simple flag toggled by the SIGINT handler in
 * server mode. Declared inside an anonymous namespace so it stays a
 * file-local translation-unit static rather than a global.
 */
std::atomic<bool> SHUTDOWN_REQUESTED{false};

/**
 * handleSigint - installs as the SIGINT handler while the server runs.
 *
 * @param signalNumber The POSIX signal number (ignored).
 */
void handleSigint(int /*signalNumber*/) {
    SHUTDOWN_REQUESTED.store(true);
}

/**
 * printUsage - writes the command-line help text.
 *
 * @param programName argv[0].
 */
void printUsage(const char* programName) {
    std::cerr << "Usage:\n"
              << "  " << programName << " server [port] [persistence_path]\n"
              << "  " << programName << " client [host] [port]\n";
}

/**
 * runServerMode - starts the WebSocket hub and blocks until SIGINT.
 *
 * @param port        TCP port to bind.
 * @param persistence File path for autosave (optional, may be empty).
 * @return Process exit code.
 */
int runServerMode(std::uint16_t port, const std::string& persistence) {
    mcm::network::Server server;
    if (!mcm::network::startServer(server, port, persistence)) {
        std::cerr << "Failed to start server on port " << port << "\n";
        return EXIT_FAILURE;
    }
    std::signal(SIGINT, handleSigint);
    std::cout << "Movie Collection Manager server listening on port "
              << port << ". Press Ctrl+C to stop." << std::endl;

    while (!SHUTDOWN_REQUESTED.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "Shutting down..." << std::endl;
    mcm::network::stopServer(server);
    return EXIT_SUCCESS;
}

/**
 * runClientMode - launches the ImGui GUI, auto-connects to the server
 * on startup and enters the render loop.
 *
 * @param host Hostname or IP of the server.
 * @param port Port string.
 * @return Process exit code.
 */
int runClientMode(const std::string& host, const std::string& port) {
#if defined(MCM_HAS_IMGUI)
    mcm::presentation::UiState state;
    std::snprintf(state.hostBuffer, sizeof(state.hostBuffer), "%s", host.c_str());
    std::snprintf(state.portBuffer, sizeof(state.portBuffer), "%s", port.c_str());

    mcm::network::Client client;
    if (mcm::network::connectClient(client, host, port)) {
        state.statusMessage = "Connected to " + host + ":" + port;
        mcm::protocol::Message sync;
        sync.kind = mcm::protocol::MessageKind::REQUEST_SYNC;
        mcm::network::sendClientMessage(client, mcm::protocol::encodeMessage(sync));
    } else {
        state.statusMessage = "Auto-connect failed: "
            + mcm::network::lastClientError(client);
    }

    if (!mcm::presentation::initialisePresentation("Movie Collection Manager")) {
        std::cerr << "Failed to initialise presentation layer." << std::endl;
        mcm::network::disconnectClient(client);
        return EXIT_FAILURE;
    }
    mcm::presentation::runMainLoop(state, client);
    mcm::presentation::shutdownPresentation();
    mcm::network::disconnectClient(client);
    return EXIT_SUCCESS;
#else
    (void)host;
    (void)port;
    std::cerr << "Client UI not compiled. Rebuild with -DMCM_BUILD_CLIENT=ON." << std::endl;
    return EXIT_FAILURE;
#endif
}

} // namespace

/**
 * main - parses argv and dispatches to the selected mode.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit code.
 */
int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
    const std::string mode = argv[1];
    if (mode == "server") {
        const std::uint16_t port = (argc >= 3)
            ? static_cast<std::uint16_t>(std::stoi(argv[2]))
            : static_cast<std::uint16_t>(9275);
        const std::string persistence = (argc >= 4) ? argv[3] : std::string{};
        return runServerMode(port, persistence);
    }
    if (mode == "client") {
        const std::string host = (argc >= 3) ? argv[2] : "127.0.0.1";
        const std::string port = (argc >= 4) ? argv[3] : "9275";
        return runClientMode(host, port);
    }
    printUsage(argv[0]);
    return EXIT_FAILURE;
}
