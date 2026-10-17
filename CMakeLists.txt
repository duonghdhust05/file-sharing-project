cmake_minimum_required(VERSION 3.16)
project(FileSharingProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
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

# Common Interface Library
add_library(fsp_common INTERFACE)
target_include_directories(fsp_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 1. Server Target
add_executable(fsp_server
    src/server/main.cpp
)
target_link_libraries(fsp_server PRIVATE fsp_common ${PLATFORM_NETWORK_LIBS})

# 2. Client CLI Target (Dùng cho kiểm thử chức năng không cần GUI)
add_executable(fsp_client_cli
    src/client_cli/main.cpp
)
target_link_libraries(fsp_client_cli PRIVATE fsp_common ${PLATFORM_NETWORK_LIBS})

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

    # Thêm target GUI khi đã có mã nguồn Qt
    # add_executable(fsp_client_gui src/client_gui/main.cpp)
    # target_link_libraries(fsp_client_gui PRIVATE fsp_common Qt::Widgets Qt::Network)
endif()

message(STATUS "FSP Build System Configured Successfully.")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Platform Network Libraries: ${PLATFORM_NETWORK_LIBS}")
