#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "common/Config.h"
#include "common/EventDispatcher.h"
#include "common/FSPProtocol.h"
#include "server/NetworkServer.h"

namespace {
FSP::NetworkServer* g_server = nullptr;

extern "C" void handleSignal(int) {
    // requestStop() chỉ ghi một atomic<bool> lock-free -> an toàn trong signal handler.
    if (g_server)
        g_server->requestStop();
}

void printUsage() {
    std::cout << "Usage: fsp_server [port] [worker_threads]\n"
              << "  port            TCP port to listen on (default " << FSP::Config::SERVER_PORT
              << ")\n"
              << "  worker_threads  Size of the worker thread pool (default "
              << FSP::Config::THREAD_POOL_SIZE << ")\n";
}

bool parseNumber(const char* text, long minValue, long maxValue, long& out) {
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < minValue || value > maxValue)
        return false;
    out = value;
    return true;
}
}  // namespace

int main(int argc, char* argv[]) {
    std::cout << "====================================================\n";
    std::cout << "   FSP SERVER (File Sharing Protocol Daemon) v1.0   \n";
    std::cout << "====================================================\n";

    FSP::ServerOptions options;
    if (argc > 1) {
        std::string first = argv[1];
        if (first == "-h" || first == "--help") {
            printUsage();
            return 0;
        }
        long port = 0;
        if (!parseNumber(argv[1], 1, 65535, port)) {
            std::cerr << "Invalid port: " << argv[1] << "\n";
            printUsage();
            return 1;
        }
        options.port = static_cast<uint16_t>(port);
    }
    if (argc > 2) {
        long workers = 0;
        if (!parseNumber(argv[2], 1, 1024, workers)) {
            std::cerr << "Invalid worker_threads: " << argv[2] << "\n";
            printUsage();
            return 1;
        }
        options.workerCount = static_cast<size_t>(workers);
    }

    std::cout << "[Protocol] Magic 0x" << std::hex << static_cast<int>(FSP::MAGIC_BYTE) << std::dec
              << ", header " << FSP::HEADER_SIZE << " bytes\n";
    std::cout << "[Config] Sandbox: " << FSP::Config::DEFAULT_SERVER_STORAGE << "\n";

    // Các module nghiệp vụ đăng ký handler vào dispatcher tại đây khi được tích hợp (Tuần 3):
    //   M2: AUTH_REGISTER / AUTH_LOGIN / AUTH_LOGOUT
    //   M3: DIR_* / FS_* / FILE_* / FOLDER_* / SEARCH_QUERY
    // Sự kiện chưa có handler sẽ tự nhận ERROR_RESP 400 "unknown event".
    FSP::EventDispatcher dispatcher;

    FSP::NetworkServer server(dispatcher, options);
    server.setOnConnect([](FSP::Connection& conn) {
        std::cout << "[Server] Client #" << conn.id() << " connected from " << conn.peerAddress()
                  << "\n";
    });
    server.setOnDisconnect([](FSP::Connection& conn) {
        std::cout << "[Server] Client #" << conn.id() << " disconnected\n";
    });

    g_server = &server;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::cout << "[Status] Press Ctrl+C to stop.\n";
    if (!server.run()) {
        std::cerr << "[Fatal] Could not start server on port " << options.port << "\n";
        g_server = nullptr;
        return 1;
    }
    g_server = nullptr;
    std::cout << "[Status] Server shut down cleanly.\n";
    return 0;
}
