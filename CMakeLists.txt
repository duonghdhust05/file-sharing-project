cmake_minimum_required(VERSION 3.16)
project(FileSharingProject VERSION 2.0.0 LANGUAGES C)

# Ngôn ngữ thống nhất: C11 thuần + POSIX (docs/C-MIGRATION-PLAN.md, SDD v2.0)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT UNIX)
    message(FATAL_ERROR "FSP v2.0 dùng POSIX socket + pthread: hãy build trên Linux (hoặc WSL).")
endif()

# -std=c11 nghiêm ngặt ẩn các API POSIX (getaddrinfo, sigaction, nanosleep...) -> bật lại
add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# libfsp_common (M1 & M2): dùng chung cho server, CLI và GUI
add_library(fsp_common STATIC
    src/common/fsp_socket.c
    src/common/fsp_packet.c
    src/common/fsp_crc32.c
    src/common/fsp_json.c
    src/common/fsp_sha256.c
)
target_include_directories(fsp_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fsp_common PUBLIC Threads::Threads)

# Server core (M1 & M2): dispatcher + server + auth/user/perm. M3 thêm file .c vào đây.
add_library(fsp_server_core STATIC
    src/server/fsp_dispatcher.c
    src/server/fsp_server.c
    src/server/fsp_user.c
    src/server/fsp_session.c
    src/server/fsp_perm.c
    src/server/fsp_auth_handler.c
)
target_link_libraries(fsp_server_core PUBLIC fsp_common)

add_executable(fsp_server src/server/server_main.c)
target_link_libraries(fsp_server PRIVATE fsp_server_core)

# Client SDK (fsp_client_api): Tầng API mạng dùng chung cho CLI và GTK GUI (M4)
add_library(fsp_client_api STATIC
    src/client_api/fsp_client_api.c
)
target_include_directories(fsp_client_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fsp_client_api PUBLIC fsp_common)

# M4 thêm ứng dụng đầu cuối ở đây: fsp_client_cli (src/client_cli/cli_main.c) và fsp_client_gui (GTK 3)

# ---------------------------------------------------------------------------
# Kiểm thử CTest
# ---------------------------------------------------------------------------
enable_testing()

add_executable(fsp_test_protocol tests/test_protocol.c)
target_link_libraries(fsp_test_protocol PRIVATE fsp_common)
add_test(NAME FSP_Protocol_Contract COMMAND fsp_test_protocol)

add_executable(fsp_test_packet tests/test_packet.c)
target_link_libraries(fsp_test_packet PRIVATE fsp_common)
add_test(NAME FSP_Probe1_Packet COMMAND fsp_test_packet)

add_executable(fsp_test_json tests/test_json.c)
target_link_libraries(fsp_test_json PRIVATE fsp_common)
add_test(NAME FSP_Json COMMAND fsp_test_json)

add_executable(fsp_test_auth_permissions tests/test_auth_permissions.c)
target_link_libraries(fsp_test_auth_permissions PRIVATE fsp_server_core)
add_test(NAME FSP_M2_Auth_Permissions COMMAND fsp_test_auth_permissions)

add_executable(fsp_test_network_core tests/test_network_core.c)
target_link_libraries(fsp_test_network_core PRIVATE fsp_server_core)
add_test(NAME FSP_Network_Core_Integration COMMAND fsp_test_network_core)
set_tests_properties(FSP_Network_Core_Integration PROPERTIES TIMEOUT 120)
