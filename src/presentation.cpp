/**
 * Presentation Layer implementation - Dear ImGui front-end.
 *
 * Uses GLFW + OpenGL3 backend. All ImGui interaction is expressed with
 * free functions operating on the UiState struct; no classes defined.
 */
// Must come before any header that pulls in windows.h
#if defined(_WIN32)
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#endif

#include "presentation.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace mcm::presentation {

namespace {

/**
 * WINDOW_WIDTH / WINDOW_HEIGHT - initial window size constants.
 */
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 800;

/**
 * windowHandle - GLFW window kept at file scope (inside an anonymous
 * namespace) solely because GLFW exposes C-style callbacks. It is NOT
 * mutable state the application logic relies on.
 */
GLFWwindow* windowHandle = nullptr;

/**
 * copyString - safe strncpy to fixed-size UI buffers. Internal.
 */
void copyString(char* destination, std::size_t capacity, const std::string& source) {
    const std::size_t length = std::min(source.size(), capacity - 1);
    std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

/**
 * makeMovieFromForm - reads the form fields into a Movie struct. Internal.
 */
data::Movie makeMovieFromForm(const UiState& state) {
    data::Movie movie;
    movie.id = state.editingId;
    movie.title = state.formTitle;
    movie.year = state.formYear;
    movie.rating = state.formRating;
    movie.durationMinutes = state.formDuration;
    return movie;
}

/**
 * resetForm - clears the form inputs. Internal.
 */
void resetForm(UiState& state) {
    state.formTitle[0] = '\0';
    state.formYear = 2024;
    state.formRating = 7.5f;
    state.formDuration = 120;
    state.editingId = 0;
}

/**
 * sendRequest - encodes and pushes a Message to the server. Internal.
 */
bool sendRequest(network::Client& client, const protocol::Message& message) {
    return network::sendClientMessage(client, protocol::encodeMessage(message));
}

/**
 * refreshTotalDuration - recomputes the recursive selection total. Internal.
 */
void refreshTotalDuration(UiState& state) {
    const std::vector<data::Movie> snapshot = logic::snapshotCollection(state.localCollection);
    std::vector<std::uint64_t> selected(state.selectedIds.begin(), state.selectedIds.end());
    state.totalSelectedMinutes = logic::totalDurationRecursive(snapshot, selected, 0);
}

/**
 * renderConnectionBar - draws the host/port controls. Internal.
 */
void renderConnectionBar(UiState& state, network::Client& client) {
    ImGui::Text("Server:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputText("##host", state.hostBuffer, sizeof(state.hostBuffer));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputText("##port", state.portBuffer, sizeof(state.portBuffer));
    ImGui::SameLine();
    if (client.connected.load()) {
        if (ImGui::Button("Disconnect")) {
            network::disconnectClient(client);
            state.statusMessage = "Disconnected.";
        }
    } else {
        if (ImGui::Button("Connect")) {
            if (network::connectClient(client, state.hostBuffer, state.portBuffer)) {
                state.statusMessage = "Connected.";
                protocol::Message sync;
                sync.kind = protocol::MessageKind::REQUEST_SYNC;
                sendRequest(client, sync);
            } else {
                state.statusMessage = "Connect failed: " + network::lastClientError(client);
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", client.connected.load() ? "ONLINE" : "OFFLINE");
}

/**
 * renderToolbar - sort/search controls plus the submit buttons. Internal.
 */
void renderToolbar(UiState& state, network::Client& client) {
    const char* KEYS[] = {"Title", "Year", "Rating", "Duration"};
    int keyIndex = static_cast<int>(state.sortKey);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("Sort By", &keyIndex, KEYS, IM_ARRAYSIZE(KEYS))) {
        state.sortKey = static_cast<logic::SortKey>(keyIndex);
    }
    ImGui::SameLine();
    bool ascending = state.sortOrder == logic::SortOrder::ASCENDING;
    if (ImGui::Checkbox("Ascending", &ascending)) {
        state.sortOrder = ascending ? logic::SortOrder::ASCENDING
                                    : logic::SortOrder::DESCENDING;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##search", "Search title (substring)",
                             state.searchBuffer, sizeof(state.searchBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Request Full Sync") && client.connected.load()) {
        protocol::Message sync;
        sync.kind = protocol::MessageKind::REQUEST_SYNC;
        sendRequest(client, sync);
    }
}

/**
 * renderMovieTable - displays the sorted / filtered movies. Internal.
 */
void renderMovieTable(UiState& state) {
    std::vector<data::Movie> movies = logic::snapshotCollection(state.localCollection);
    logic::sortMovies(movies, state.sortKey, state.sortOrder);

    std::vector<std::size_t> visibleIndices;
    const std::string needle = state.searchBuffer;
    if (needle.empty()) {
        visibleIndices.reserve(movies.size());
        for (std::size_t index = 0; index < movies.size(); ++index) {
            visibleIndices.push_back(index);
        }
    } else {
        visibleIndices = logic::linearSearchByTitle(movies, needle);
    }

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders
                                     | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_Resizable
                                     | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("movies", 7, tableFlags, ImVec2(0.0f, 360.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Sel");
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Title");
        ImGui::TableSetupColumn("Year");
        ImGui::TableSetupColumn("Rating");
        ImGui::TableSetupColumn("Duration");
        ImGui::TableSetupColumn("Actions");
        ImGui::TableHeadersRow();

        for (std::size_t visibleCursor = 0; visibleCursor < visibleIndices.size(); ++visibleCursor) {
            const data::Movie& movie = movies[visibleIndices[visibleCursor]];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            bool selected = state.selectedIds.count(movie.id) > 0;
            ImGui::PushID(static_cast<int>(movie.id));
            if (ImGui::Checkbox("##sel", &selected)) {
                if (selected) {
                    state.selectedIds.insert(movie.id);
                } else {
                    state.selectedIds.erase(movie.id);
                }
                refreshTotalDuration(state);
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(movie.id));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(movie.title.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", movie.year);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.1f", movie.rating);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%d min", movie.durationMinutes);

            ImGui::TableSetColumnIndex(6);
            if (ImGui::SmallButton("Edit")) {
                state.editingId = movie.id;
                copyString(state.formTitle, sizeof(state.formTitle), movie.title);
                state.formYear = movie.year;
                state.formRating = movie.rating;
                state.formDuration = movie.durationMinutes;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                state.editingId = movie.id; // marker for delete button below
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

/**
 * renderForm - create/update form. Internal.
 */
void renderForm(UiState& state, network::Client& client) {
    ImGui::SeparatorText(state.editingId == 0 ? "Add Movie" : "Edit Movie");

    ImGui::InputText("Title", state.formTitle, sizeof(state.formTitle));
    ImGui::InputInt("Year", &state.formYear);
    ImGui::SliderFloat("Rating", &state.formRating, 0.0f, 10.0f, "%.1f");
    ImGui::InputInt("Duration (min)", &state.formDuration);

    const bool online = client.connected.load();
    if (!online) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button(state.editingId == 0 ? "Create" : "Save Changes")) {
        data::Movie candidate = makeMovieFromForm(state);
        std::string validationError;
        if (!logic::validateMovie(candidate, validationError)) {
            state.statusMessage = "Invalid input: " + validationError;
        } else {
            protocol::Message request;
            request.payload = candidate;
            request.kind = state.editingId == 0
                ? protocol::MessageKind::REQUEST_ADD
                : protocol::MessageKind::REQUEST_UPDATE;
            if (sendRequest(client, request)) {
                state.statusMessage = state.editingId == 0
                    ? "Add request sent."
                    : "Update request sent.";
                resetForm(state);
            } else {
                state.statusMessage = "Send failed.";
            }
        }
    }

    if (state.editingId != 0) {
        ImGui::SameLine();
        if (ImGui::Button("Delete This")) {
            protocol::Message request;
            request.kind = protocol::MessageKind::REQUEST_REMOVE;
            request.targetId = state.editingId;
            if (sendRequest(client, request)) {
                state.statusMessage = "Delete request sent.";
                resetForm(state);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            resetForm(state);
        }
    }

    if (!online) {
        ImGui::EndDisabled();
    }
}

/**
 * renderStatusBar - summary metrics and last status message. Internal.
 */
void renderStatusBar(UiState& state) {
    const std::vector<data::Movie> snapshot = logic::snapshotCollection(state.localCollection);
    const float mean = logic::averageRating(snapshot);

    ImGui::SeparatorText("Summary");
    ImGui::Text("Movies: %zu | Average rating: %.2f | Selected total: %lld min (%.1f h)",
                snapshot.size(),
                static_cast<double>(mean),
                state.totalSelectedMinutes,
                static_cast<double>(state.totalSelectedMinutes) / 60.0);
    if (!state.statusMessage.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "%s", state.statusMessage.c_str());
    }
}

/**
 * processServerTraffic - drains the inbox and applies each decoded frame. Internal.
 */
void processServerTraffic(UiState& state, network::Client& client) {
    std::vector<std::string> frames = network::drainInbox(client);
    if (frames.empty()) {
        return;
    }
    for (const std::string& raw : frames) {
        protocol::Message message;
        if (!protocol::decodeMessage(raw, message)) {
            continue;
        }
        applyServerEvent(state, message);
    }
    refreshTotalDuration(state);
}

} // namespace

bool initialisePresentation(const std::string& windowTitle) {
    glfwSetErrorCallback([](int errorCode, const char* description) {
        std::cerr << "[glfw " << errorCode << "] " << description << "\n";
    });
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    windowHandle = glfwCreateWindow(
        WINDOW_WIDTH, WINDOW_HEIGHT,
        windowTitle.c_str(), nullptr, nullptr);
    if (!windowHandle) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(windowHandle);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(windowHandle, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void shutdownPresentation() {
    if (!windowHandle) {
        return;
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(windowHandle);
    glfwTerminate();
    windowHandle = nullptr;
}

void applyServerEvent(UiState& state, const protocol::Message& message) {
    switch (message.kind) {
        case protocol::MessageKind::HELLO:
        case protocol::MessageKind::FULL_STATE: {
            logic::replaceMirror(state.localCollection, message.snapshot, message.revision);
            state.statusMessage = (message.kind == protocol::MessageKind::HELLO)
                ? "Session started (id=" + std::to_string(message.sessionId) + ")"
                : std::string("Full sync applied.");
            state.lastKnownRevision = message.revision;
            break;
        }
        case protocol::MessageKind::EVENT_ADDED:
        case protocol::MessageKind::EVENT_UPDATED: {
            logic::applyEventToMirror(state.localCollection, message);
            state.lastKnownRevision = message.revision;
            break;
        }
        case protocol::MessageKind::EVENT_REMOVED: {
            logic::applyEventToMirror(state.localCollection, message);
            state.selectedIds.erase(message.targetId);
            if (state.editingId == message.targetId) {
                state.editingId = 0;
            }
            state.lastKnownRevision = message.revision;
            break;
        }
        case protocol::MessageKind::ERROR_REPLY: {
            state.statusMessage = "Server error: " + message.errorText;
            break;
        }
        default:
            break;
    }
}

void runMainLoop(UiState& state, network::Client& client) {
    if (!windowHandle) {
        return;
    }
    while (!glfwWindowShouldClose(windowHandle)) {
        glfwPollEvents();
        processServerTraffic(state, client);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration
                                           | ImGuiWindowFlags_NoMove
                                           | ImGuiWindowFlags_NoSavedSettings
                                           | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Movie Collection Manager", nullptr, windowFlags);

        renderConnectionBar(state, client);
        ImGui::Separator();
        renderToolbar(state, client);
        ImGui::Separator();
        renderMovieTable(state);
        ImGui::Separator();
        renderForm(state, client);
        ImGui::Separator();
        renderStatusBar(state);

        ImGui::End();

        ImGui::Render();
        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(windowHandle, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(windowHandle);
    }
}

} // namespace mcm::presentation
