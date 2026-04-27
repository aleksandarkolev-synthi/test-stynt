/**
 * Presentation Layer - Movie Collection Manager
 *
 * Renders the GUI with Dear ImGui (GLFW + OpenGL3 backend) and drives
 * user interaction. Strictly calls into the logic layer and the network
 * client; NEVER talks to the data layer directly.
 */
#ifndef MCM_PRESENTATION_H
#define MCM_PRESENTATION_H

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "data.h"
#include "logic.h"
#include "network.h"

namespace mcm::presentation {

/**
 * UiState - all mutable UI-side data. Passed by reference to render.
 * No statics, no globals.
 */
struct UiState {
    // Local replica of the server-side collection.
    data::Collection localCollection;

    // Sorting / filtering controls.
    logic::SortKey sortKey = logic::SortKey::TITLE;
    logic::SortOrder sortOrder = logic::SortOrder::ASCENDING;
    char searchBuffer[256] = "";

    // New-movie form fields.
    char formTitle[256] = "";
    int formYear = 2024;
    float formRating = 7.5f;
    int formDuration = 120;

    // Edit target (0 = creating a new entry).
    std::uint64_t editingId = 0;

    // Selection set for the recursive duration calculation.
    std::unordered_set<std::uint64_t> selectedIds;

    // Connection form.
    char hostBuffer[128] = "127.0.0.1";
    char portBuffer[16] = "9275";

    // Last error / info line shown in the status bar.
    std::string statusMessage;

    // Cached total duration for the current selection.
    long long totalSelectedMinutes = 0;

    // Monotonic revision tracked so we know when the local mirror changed.
    std::uint64_t lastKnownRevision = 0;
};

/**
 * initialisePresentation - creates the GLFW window and Dear ImGui context.
 *
 * @param windowTitle Title shown in the OS title bar.
 * @return true on success; inspect stderr for diagnostics on failure.
 */
bool initialisePresentation(const std::string& windowTitle);

/**
 * shutdownPresentation - tears down ImGui and GLFW in reverse order.
 */
void shutdownPresentation();

/**
 * runMainLoop - pumps the render loop until the user closes the window
 * or disconnects. Returns when the window has been requested to close.
 *
 * @param state  UI state (mutated across frames).
 * @param client Network client used to communicate with the server.
 */
void runMainLoop(UiState& state, network::Client& client);

/**
 * applyServerEvent - integrates a single decoded protocol message into
 * the local mirror. Exposed for testing / for the main loop.
 *
 * @param state   UI state to update.
 * @param message Decoded message from the server.
 */
void applyServerEvent(UiState& state, const protocol::Message& message);

} // namespace mcm::presentation

#endif // MCM_PRESENTATION_H
