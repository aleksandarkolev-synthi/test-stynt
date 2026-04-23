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
- **Network** (`include/network.h`, `src/network.cpp`) — Boost.Beast
  based WebSocket server *and* client. The server owns the canonical
  `Collection`, applies requests atomically, then broadcasts the
  resulting event to every connected client so every GUI updates
  immediately.

No global state, no classes defined by the application (stdlib types
don't count), no member functions — only structs and free functions.

## Building

Requires:
- CMake ≥ 3.20, a C++17 compiler
- Boost ≥ 1.75 with `Boost::system`
- OpenGL + a working windowing stack (for the client)

Other dependencies (`nlohmann/json`, Dear ImGui, GLFW if not present)
are fetched automatically via CMake's `FetchContent`.

```
cmake -S . -B build
cmake --build build -j
```

Server-only build (no GUI deps):

```
cmake -S . -B build -DMCM_BUILD_CLIENT=OFF
cmake --build build -j
```

## Running

Start the hub:

```
./build/mcm server 9275 movies.json
```

Then launch one GUI per user (on the same machine or another):

```
./build/mcm client 127.0.0.1 9275
```

Any add/update/delete performed by any client is persisted by the
server and broadcast in real time to every other connected GUI.
