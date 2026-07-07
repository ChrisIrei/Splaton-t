cmake_minimum_required(VERSION 3.24)
project(splatont C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(MSVC)
  add_compile_options(/utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(FetchContent)

# ---- raylib (window / render / input / audio) ----
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# ---- ENet (reliable UDP). Its own CMakeLists is ancient, so we build the
# sources into our own static lib. SOURCE_SUBDIR points at a folder with no
# CMakeLists so MakeAvailable only downloads. ----
FetchContent_Declare(enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG v1.3.18
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR include)
FetchContent_MakeAvailable(enet)

add_library(enet_static STATIC
  ${enet_SOURCE_DIR}/callbacks.c
  ${enet_SOURCE_DIR}/compress.c
  ${enet_SOURCE_DIR}/host.c
  ${enet_SOURCE_DIR}/list.c
  ${enet_SOURCE_DIR}/packet.c
  ${enet_SOURCE_DIR}/peer.c
  ${enet_SOURCE_DIR}/protocol.c
  ${enet_SOURCE_DIR}/unix.c
  ${enet_SOURCE_DIR}/win32.c)
target_include_directories(enet_static PUBLIC ${enet_SOURCE_DIR}/include)
if(WIN32)
  target_link_libraries(enet_static PUBLIC ws2_32 winmm)
endif()

# ---- SQLite amalgamation (local account database) ----
FetchContent_Declare(sqlite3
  URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip)
FetchContent_MakeAvailable(sqlite3)

add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})

# ---- shared simulation + protocol (used by client and server) ----
add_library(splat_shared STATIC
  src/shared/sim.cpp)
target_include_directories(splat_shared PUBLIC src)

# ---- engine pieces that don't touch raylib (safe to link anywhere) ----
add_library(splat_engine STATIC
  src/engine/net.cpp
  src/engine/sha256.cpp)
target_include_directories(splat_engine PUBLIC src)
target_link_libraries(splat_engine PUBLIC enet_static)

# ---- dedicated server (headless console app) ----
add_executable(splatont_server
  src/server/main.cpp
  src/server/db.cpp
  src/server/game.cpp)
target_link_libraries(splatont_server PRIVATE splat_shared splat_engine sqlite3)

# ---- game client ----
add_executable(splatont
  src/client/main.cpp
  src/client/assets.cpp
  src/client/ui.cpp
  src/client/launch_server.cpp)
target_link_libraries(splatont PRIVATE splat_shared splat_engine raylib)
