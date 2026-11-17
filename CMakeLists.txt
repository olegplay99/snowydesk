cmake_minimum_required(VERSION 3.25)

project(SnowyDesk
    VERSION 1.0.0
    DESCRIPTION "SnowyDesk - snow on your desktop, drawn by a transparent overlay"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Toolchain gate: the project is Windows/MSVC-only by design.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR "SnowyDesk is a Windows-only project.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "SnowyDesk requires MSVC (Visual Studio 2022, v143 toolset).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SnowyDesk requires an x64 build. Configure with: cmake -B build -G \"Visual Studio 17 2022\" -A x64")
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (nlohmann/json + spdlog) are pulled with FetchContent.
# No vcpkg checkout is required; see cmake/Dependencies.cmake.
# ---------------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Shared compile settings
# ---------------------------------------------------------------------------
add_library(snowydesk_options INTERFACE)
target_compile_features(snowydesk_options INTERFACE cxx_std_20)
target_compile_options(snowydesk_options INTERFACE
    /W4
    /permissive-
    /utf-8
    /EHsc
    /Zc:__cplusplus
    /MP)
target_compile_definitions(snowydesk_options INTERFACE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00)

# ---------------------------------------------------------------------------
# common/ : shared types, JSON layer, state store, logging, paths
# ---------------------------------------------------------------------------
add_library(snowydesk_common STATIC
    common/src/Paths.cpp
    common/src/Log.cpp
    common/src/Json.cpp
    common/src/StateStore.cpp)
target_include_directories(snowydesk_common PUBLIC common/include)
target_link_libraries(snowydesk_common
    PUBLIC snowydesk_options
           nlohmann_json::nlohmann_json
           spdlog::spdlog
           shell32
           ole32)

# ---------------------------------------------------------------------------
# ipc/ : versioned JSON protocol over the \\.\pipe\SnowyDesk named pipe
# ---------------------------------------------------------------------------
add_library(snowydesk_ipc STATIC
    ipc/src/Protocol.cpp
    ipc/src/PipeServer.cpp
    ipc/src/PipeClient.cpp)
target_include_directories(snowydesk_ipc PUBLIC ipc/include)
target_link_libraries(snowydesk_ipc PUBLIC snowydesk_common advapi32)

# ---------------------------------------------------------------------------
# snow/ : particle simulation and window anchoring
# ---------------------------------------------------------------------------
add_library(snowydesk_snow STATIC
    snow/src/SnowWorld.cpp)
target_include_directories(snowydesk_snow PUBLIC snow/include)
target_link_libraries(snowydesk_snow PUBLIC snowydesk_common dwmapi)

# ---------------------------------------------------------------------------
# garland/ : flickering light chain across the top of every monitor
# ---------------------------------------------------------------------------
add_library(snowydesk_garland STATIC
    garland/src/Garland.cpp)
target_include_directories(snowydesk_garland PUBLIC garland/include)
target_link_libraries(snowydesk_garland PUBLIC snowydesk_common)

# ---------------------------------------------------------------------------
# i18n/ : every user visible string lives here (ru/en)
# ---------------------------------------------------------------------------
add_library(snowydesk_i18n STATIC
    i18n/src/I18n.cpp)
target_include_directories(snowydesk_i18n PUBLIC i18n/include)
target_link_libraries(snowydesk_i18n PUBLIC snowydesk_common)

# ---------------------------------------------------------------------------
# overlay/ + settings/ : the UI process implementation, also reused by tests
# ---------------------------------------------------------------------------
add_library(snowydesk_ui STATIC
    overlay/src/OverlayHost.cpp
    overlay/src/MainWindow.cpp
    overlay/src/TrayIcon.cpp
    overlay/src/App.cpp
    settings/src/SettingsWindow.cpp)
target_include_directories(snowydesk_ui PUBLIC overlay/include settings/include)
target_link_libraries(snowydesk_ui
    PUBLIC snowydesk_options
           snowydesk_common
           snowydesk_ipc
           snowydesk_snow
           snowydesk_garland
           snowydesk_i18n
           d2d1
           dwmapi
           shell32
           shcore
           user32
           gdi32
           comctl32
           ole32)

# ---------------------------------------------------------------------------
# SnowyDeskService : Windows service, owns state.json, never draws
# ---------------------------------------------------------------------------
add_executable(SnowyDeskService
    service/src/main.cpp
    service/src/ServiceHost.cpp
    service/src/SessionLaunch.cpp
    resources/service.rc
    resources/service.manifest)
target_include_directories(SnowyDeskService PRIVATE service/include)
target_link_libraries(SnowyDeskService PRIVATE
    snowydesk_options
    snowydesk_common
    snowydesk_ipc
    advapi32
    userenv
    wtsapi32
    user32)

# ---------------------------------------------------------------------------
# SnowyDeskUI : tray icon, main window, settings window, snow overlay
# ---------------------------------------------------------------------------
add_executable(SnowyDeskUI WIN32
    overlay/src/main.cpp
    resources/ui.rc
    resources/ui.manifest)
target_link_libraries(SnowyDeskUI PRIVATE snowydesk_ui)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(snowydesk_tests tests/main.cpp)
target_link_libraries(snowydesk_tests PRIVATE
    snowydesk_options
    snowydesk_common
    snowydesk_ipc
    snowydesk_snow
    snowydesk_i18n)
add_test(NAME serialization COMMAND snowydesk_tests)

# Keep the Release binaries together so a human can copy them around.
set_target_properties(SnowyDeskService SnowyDeskUI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

message(STATUS "SnowyDesk ${PROJECT_VERSION} configured for MSVC x64 (C++20)")
