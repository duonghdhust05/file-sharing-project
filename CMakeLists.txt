cmake_minimum_required(VERSION 3.16)
project(FileSharingProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Platform specific libraries (Winsock for Windows, pthread for Linux)
if(WIN32)
    set(PLATFORM_NETWORK_LIBS ws2_32)
else()
    set(PLATFORM_NETWORK_LIBS pthread)
endif()

# Global Include Directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Common Static Library
find_package(Threads REQUIRED)

add_library(fsp_common STATIC
    src/common/Socket.cpp
    src/common/PacketHandler.cpp
    src/common/Connection.cpp
    src/common/EventDispatcher.cpp
)
target_include_directories(fsp_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fsp_common PUBLIC ${PLATFORM_NETWORK_LIBS} Threads::Threads)

# Server Core Library (M1: ThreadPool + NetworkServer). M2/M3 thêm source của họ vào đây.
add_library(fsp_server_core STATIC
    src/server/ServerWorkerThreadPool.cpp
    src/server/NetworkServer.cpp
)
target_link_libraries(fsp_server_core PUBLIC fsp_common)

# Client Core Library (M1: NetworkClient SDK dùng cho CLI và Qt GUI)
add_library(fsp_client_core STATIC
    src/client/NetworkClient.cpp
)
target_link_libraries(fsp_client_core PUBLIC fsp_common)

# 1. Server Target
add_executable(fsp_server
    src/server/main.cpp
)
target_link_libraries(fsp_server PRIVATE fsp_server_core)

# 2. Client CLI Target
add_executable(fsp_client_cli
    src/client_cli/main.cpp
)
target_link_libraries(fsp_client_cli PRIVATE fsp_client_core)

# 3. Optional: Qt Client GUI
option(BUILD_QT_GUI "Build Qt-based Graphical Client" OFF)
if(BUILD_QT_GUI)
    find_package(Qt6 COMPONENTS Widgets Network QUIET)
    if(NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Widgets Network REQUIRED)
    endif()

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    # add_executable(fsp_client_gui src/client_gui/main.cpp)
    # target_link_libraries(fsp_client_gui PRIVATE fsp_common Qt::Widgets Qt::Network)
endif()

# 4. Testing Target (CTest Automated Suite)
enable_testing()

# Test 1: Kiểm thử Giao thức chuẩn & Bitmask POSIX
add_executable(fsp_tests
    tests/test_protocol.cpp
)
target_link_libraries(fsp_tests PRIVATE fsp_common)
add_test(NAME FSP_Protocol_UnitTests COMMAND fsp_tests)

# Test 2: Kiểm thử M1 Tầng Mạng & Probe 1 Sticky Packets
add_executable(fsp_network_tests
    tests/test_network_core.cpp
)
target_link_libraries(fsp_network_tests PRIVATE fsp_common)
add_test(NAME FSP_Network_Core_Tests COMMAND fsp_network_tests)

# Test 3: Kiểm thử M1.3 EventDispatcher & M1.4 ThreadPool (không cần mạng)
add_executable(fsp_dispatcher_tests
    tests/test_dispatcher_threadpool.cpp
)
target_link_libraries(fsp_dispatcher_tests PRIVATE fsp_server_core)
add_test(NAME FSP_Dispatcher_ThreadPool_Tests COMMAND fsp_dispatcher_tests)

# Test 4: Kiểm thử tích hợp M1 qua TCP loopback thật (NetworkServer + NetworkClient)
add_executable(fsp_server_integration_tests
    tests/test_server_integration.cpp
)
target_link_libraries(fsp_server_integration_tests PRIVATE fsp_server_core fsp_client_core)
add_test(NAME FSP_Server_Integration_Tests COMMAND fsp_server_integration_tests)
set_tests_properties(FSP_Server_Integration_Tests PROPERTIES TIMEOUT 120)

message(STATUS "FSP Build System Configured Successfully.")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Platform Network Libraries: ${PLATFORM_NETWORK_LIBS}")
message(STATUS "Automated Testing: ENABLED")
