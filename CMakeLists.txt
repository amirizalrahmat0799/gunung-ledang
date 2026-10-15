cmake_minimum_required(VERSION 3.24)
project(GunungLedang VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(LEDANG_BUILD_CLIENT "Build the SDL3 game client (downloads SDL3 if it isn't installed)" ON)
option(LEDANG_BUILD_TESTS "Build the unit and soak tests" ON)
# A standalone build: SDL3 and (on Windows) the C++ runtime are linked into the executables, so the game is a
# single .exe that runs on any PC. On by default on Windows, and used for release builds everywhere.
if(WIN32)
  option(LEDANG_STATIC "Link SDL3 and the C++ runtime statically" ON)
else()
  option(LEDANG_STATIC "Link SDL3 and the C++ runtime statically" OFF)
endif()
if(LEDANG_STATIC AND MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The version shown in the game and in the exe's properties. Release builds pass the git tag (v0.1.0).
set(LEDANG_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string compiled into the game")

function(ledang_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
  endif()
endfunction()

# ---- Engine: networking, simulation and protocol (no graphics) ----------------------------------------------
add_library(ledang_core STATIC
  src/core/bitstream.cpp
  src/core/clock.cpp
  src/net/socket.cpp
  src/net/link_conditioner.cpp
  src/net/connection.cpp
  src/sim/map.cpp
  src/sim/player.cpp
  src/sim/enemy.cpp
  src/sim/world.cpp
  src/proto/protocol.cpp
  src/server/server.cpp
  src/client/client_core.cpp
)
target_include_directories(ledang_core PUBLIC src)
ledang_warnings(ledang_core)
if(WIN32)
  target_link_libraries(ledang_core PUBLIC ws2_32 winmm)
  target_compile_definitions(ledang_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
find_package(Threads REQUIRED)
target_link_libraries(ledang_core PUBLIC Threads::Threads)
if(LEDANG_STATIC AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(ledang_core PUBLIC -static-libstdc++ -static-libgcc)  # runs on older Linux too
endif()

add_executable(ledang_server src/server/main.cpp)
target_link_libraries(ledang_server PRIVATE ledang_core)
ledang_warnings(ledang_server)

add_executable(ledang_bot src/bot/main.cpp)
target_link_libraries(ledang_bot PRIVATE ledang_core)
ledang_warnings(ledang_bot)

# ---- Game client ---------------------------------------------------------------------------------------------
if(LEDANG_BUILD_CLIENT)
  if(NOT LEDANG_STATIC)
    find_package(SDL3 CONFIG QUIET)
  endif()
  if(NOT SDL3_FOUND)
    message(STATUS "Downloading SDL 3.2.24")
    include(FetchContent)
    if(LEDANG_STATIC)
      set(SDL_SHARED OFF CACHE BOOL "" FORCE)
      set(SDL_STATIC ON CACHE BOOL "" FORCE)
    else()
      set(SDL_SHARED ON CACHE BOOL "" FORCE)
      set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.2.24
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(SDL3)
  endif()

  add_executable(gunung_ledang
    src/client/main.cpp
    src/client/game.cpp
    src/client/render.cpp
    src/client/sprites.cpp
  )
  if(LEDANG_STATIC)
    target_link_libraries(gunung_ledang PRIVATE ledang_core SDL3::SDL3-static)
  else()
    target_link_libraries(gunung_ledang PRIVATE ledang_core SDL3::SDL3)
  endif()
  target_compile_definitions(gunung_ledang PRIVATE LEDANG_VERSION="${LEDANG_VERSION}")
  ledang_warnings(gunung_ledang)
  if(WIN32)
    set_target_properties(gunung_ledang PROPERTIES WIN32_EXECUTABLE TRUE)
    configure_file(packaging/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)
    target_sources(gunung_ledang PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    if(NOT LEDANG_STATIC)
      # Put SDL3.dll next to the game so it runs straight from Visual Studio or the build folder.
      add_custom_command(TARGET gunung_ledang POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:gunung_ledang> $<TARGET_FILE_DIR:gunung_ledang>
        COMMAND_EXPAND_LISTS)
    endif()
  endif()
  set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT gunung_ledang)
endif()

# ---- Tests ---------------------------------------------------------------------------------------------------
if(LEDANG_BUILD_TESTS)
  enable_testing()
  add_executable(ledang_tests
    tests/main.cpp
    tests/test_bitstream.cpp
    tests/test_connection.cpp
    tests/test_sim.cpp
    tests/test_protocol.cpp
    tests/test_netgame.cpp
  )
  target_link_libraries(ledang_tests PRIVATE ledang_core)
  ledang_warnings(ledang_tests)
  add_test(NAME unit COMMAND ledang_tests --exclude soak)
  add_test(NAME soak COMMAND ledang_tests soak)
  set_tests_properties(soak PROPERTIES TIMEOUT 300)
endif()
