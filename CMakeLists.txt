cmake_minimum_required(VERSION 3.20)
project(movie_collection_manager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# nlohmann_json (JSON serialisation for the data / protocol layers)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Boost (Asio + Beast for the WebSocket server/client)
# ---------------------------------------------------------------------------
find_package(Boost 1.75 REQUIRED COMPONENTS system)

# ---------------------------------------------------------------------------
# Dear ImGui + GLFW + OpenGL (presentation layer)
# The MCM_BUILD_CLIENT switch lets server-only builds skip these deps.
# ---------------------------------------------------------------------------
option(MCM_BUILD_CLIENT "Build the ImGui-based client UI" ON)

set(IMGUI_AVAILABLE FALSE)
if(MCM_BUILD_CLIENT)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG        3.4
        )
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(glfw)
    endif()

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
    set(IMGUI_AVAILABLE TRUE)
endif()

# ---------------------------------------------------------------------------
# Movie Collection Manager executable
# ---------------------------------------------------------------------------
set(MCM_SOURCES
    src/data.cpp
    src/logic.cpp
    src/protocol.cpp
    src/network.cpp
    src/main.cpp
)

if(IMGUI_AVAILABLE)
    list(APPEND MCM_SOURCES src/presentation.cpp)
endif()

add_executable(mcm ${MCM_SOURCES})
target_include_directories(mcm PRIVATE include)
target_link_libraries(mcm PRIVATE
    nlohmann_json::nlohmann_json
    Boost::system
)

if(IMGUI_AVAILABLE)
    target_link_libraries(mcm PRIVATE imgui)
    target_compile_definitions(mcm PRIVATE MCM_HAS_IMGUI=1)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(mcm PRIVATE pthread)
endif()
