#include <cstdlib>
#include <iostream>
#include <string>

#include "client/NetworkClient.h"
#include "common/Config.h"
#include "common/FSPProtocol.h"

namespace {
void printUsage() {
    std::cout << "Usage:\n"
              << "  fsp_client_cli [host] [port]                    interactive mode\n"
              << "  fsp_client_cli <host> <port> <EVENT> [json]     send one request and exit\n\n"
              << "Interactive commands:\n"
              << "  <EVENT> [json]   send EVENT_REQ, e.g.  AUTH_LOGIN {\"username\":\"a\"}\n"
              << "  quit             close the connection\n";
}

int sendAndPrint(FSP::NetworkClient& client, const std::string& event, const std::string& json) {
    if (event.size() >= FSP::EVENT_NAME_MAX_LEN) {
        std::cout << "!! Event name must be at most " << (FSP::EVENT_NAME_MAX_LEN - 1)
                  << " characters\n";
        return 1;
    }
    auto response = client.request(event, json.empty() ? "{}" : json);
    if (!response) {
        std::cout << "<< (no response: connection closed or timed out)\n";
        return 2;
    }
    const char* kind = response->type == FSP::MessageType::ERROR_RESP ? "ERROR_RESP" : "EVENT_RESP";
    std::cout << "<< " << kind << " " << response->event << " status=" << response->status << " "
              << response->body << "\n";
    return response->ok() ? 0 : 3;
}
}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        printUsage();
        return 0;
    }

    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    long port = FSP::Config::SERVER_PORT;
    if (argc > 2) {
        char* end = nullptr;
        port = std::strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || port < 1 || port > 65535) {
            std::cerr << "Invalid port: " << argv[2] << "\n";
            return 1;
        }
    }

    FSP::NetworkClient client;
    if (!client.connect(host, static_cast<uint16_t>(port))) {
        std::cerr << "Cannot connect to " << host << ":" << port << "\n";
        return 1;
    }

    if (argc > 3) {
        std::string json = argc > 4 ? argv[4] : "{}";
        return sendAndPrint(client, argv[3], json);
    }

    std::cout << "Connected to " << host << ":" << port << ". Type 'help' for commands.\n";
    std::string line;
    while (client.isConnected() && std::cout << "fsp> " && std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        if (line == "quit" || line == "exit")
            break;
        if (line == "help") {
            printUsage();
            continue;
        }
        size_t space = line.find(' ');
        std::string event = line.substr(0, space);
        std::string json = space == std::string::npos ? "" : line.substr(space + 1);
        sendAndPrint(client, event, json);
    }
    client.disconnect();
    return 0;
}
