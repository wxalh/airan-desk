cmake_minimum_required(VERSION 3.21)
project(airan_device_presence_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network WebSockets Test
    HINTS "${Qt6_DIR}" "${Qt5_DIR}")
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network WebSockets Test)
get_filename_component(AIRAN_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# WsCli does not use RTC or application configuration. Its umbrella include
# pulls those in, so isolate those unrelated dependencies for this socket test.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shims/common")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shims/common/constant.h"
    "#pragma once\n#include \"common/protocol_constants.h\"\n#include \"common/logger_manager.h\"\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shims/common/logger_manager.h"
    "#pragma once\n#define LOG_TRACE(...) do {} while(false)\n#define LOG_DEBUG(...) do {} while(false)\n#define LOG_INFO(...) do {} while(false)\n#define LOG_WARN(...) do {} while(false)\n#define LOG_ERROR(...) do {} while(false)\n")
add_executable(device_presence_tests
    test_device_presence.cpp
    ../ws_cli.h
    ../ws_cli.cpp
    ../ws_cli_events.cpp
    ../ws_cli_messages.cpp
    ../ws_cli_reconnect.cpp
    ../../util/json/json_util_parse.cpp
    ../../util/json/json_util_access.cpp)
target_include_directories(device_presence_tests PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/shims" "${AIRAN_SOURCE_ROOT}/src")
target_link_libraries(device_presence_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets Qt${QT_VERSION_MAJOR}::Test)
enable_testing()
add_test(NAME device_presence COMMAND device_presence_tests)
