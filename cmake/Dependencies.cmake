# Third-party dependencies.
#
# Decision (recorded in README.md as well): vcpkg is NOT used. Both dependencies
# are lightweight and can be pulled with FetchContent, which keeps a fresh clone
# buildable with nothing but CMake + MSVC + an internet connection:
#
#   nlohmann/json 3.11.3  - JSON serialization for state.json and the IPC protocol
#   spdlog 1.15.3         - logging (rotating file sinks)
#
# If the machine is offline, populate the cache once and reconfigure with
# -DFETCHCONTENT_FULLY_DISCONNECTED=ON.

include(FetchContent)

# json
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# spdlog
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(json spdlog)

if(NOT TARGET nlohmann_json::nlohmann_json)
    message(FATAL_ERROR "nlohmann/json was not provided by FetchContent.")
endif()

if(NOT TARGET spdlog::spdlog)
    message(FATAL_ERROR "spdlog was not provided by FetchContent.")
endif()
