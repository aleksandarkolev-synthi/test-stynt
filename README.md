# Movie Collection Manager

A multi-user, real-time movie collection manager written in modern C++17.
Architected as a strict three-tier structural-programming project:

- **Data layer** (`include/data.h`, `src/data.cpp`) — thread-safe
  `Movie` / `Collection` structs, CRUD helpers, JSON persistence.
- **Logic layer** (`include/logic.h`, `src/logic.cpp`) — recursive
  quicksort, linear + binary search, recursive duration aggregation,
  validation, averages.
- **Presentation layer** (`include/presentation.h`,
  `src/presentation.cpp`) — Dear ImGui desktop GUI (GLFW + OpenGL3)
  that visualises the collection in a sortable, searchable, selectable
  table. It never touches the data layer directly.

Real-time multi-user collaboration is provided by a WebSocket hub:

- **Protocol** (`include/protocol.h`, `src/protocol.cpp`) — JSON wire
  format with strongly-typed `Message` structs.
- **Network** (`include/network.h`, `src/network.cpp`) — IXWebSocket
  based server and client. The server owns the canonical `Collection`,
  applies requests atomically, then broadcasts the resulting event to
  every connected client so every GUI updates immediately.

No global state, no classes defined by the application (stdlib types
don't count), no member functions — only structs and free functions.

---

## Building

**Requirements:** CMake ≥ 3.20, a C++17 compiler, OpenGL drivers.
All other dependencies (nlohmann/json, IXWebSocket, GLFW, Dear ImGui)
are downloaded automatically by CMake's FetchContent on first configure.

### Windows (MSVC / Visual Studio)

```bat
cmake -S . -B build
cmake --build build --config Debug -j
```

Server-only (no GUI, no OpenGL required):

```bat
cmake -S . -B build -DMCM_BUILD_CLIENT=OFF
cmake --build build --config Debug -j
```

### Linux / macOS

```sh
cmake -S . -B build
cmake --build build -j
```

---

## Running

### Windows

Open **two separate Command Prompt / PowerShell windows**.

**Window 1 — server hub** (keeps running, persists the collection to `movies.json`):

```bat
.\build\Debug\mcm.exe server 9275 movies.json
```

**Window 2+ — client GUI** (one per user; repeat on other machines with the server's IP):

```bat
.\build\Debug\mcm.exe client 127.0.0.1 9275
```

> For a Release build replace `Debug` with `Release` in the paths above.

### Linux / macOS

```sh
# Terminal 1
./build/mcm server 9275 movies.json

# Terminal 2+
./build/mcm client 127.0.0.1 9275
```

---

## Multi-user workflow

1. Start **one server** on any machine on the network.
2. Launch **any number of clients**, pointing at the server's IP and port.
3. Every add / update / delete performed by any client is:
   - validated and applied by the server atomically,
   - persisted to `movies.json` within one second,
   - broadcast in real time to every other connected GUI.

The client GUI auto-connects on startup and shows **ONLINE** in the top bar.
If the server is not reachable yet, click **Connect** after starting the server.
