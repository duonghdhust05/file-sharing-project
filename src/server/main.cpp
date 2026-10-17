#include <iostream>
#include <string>
#include "common/FSPProtocol.h"
#include "common/Config.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================\n";
    std::cout << "   FSP SERVER (File Sharing Protocol Daemon) v1.0   \n";
    std::cout << "====================================================\n";
    std::cout << "[Protocol Info] Magic Byte: 0x" << std::hex << (int)FSP::MAGIC_BYTE << std::dec << "\n";
    std::cout << "[Protocol Info] Header Size: " << FSP::HEADER_SIZE << " bytes\n";
    std::cout << "[Config] Default Port: " << FSP::Config::SERVER_PORT << "\n";
    std::cout << "[Config] Server Sandbox: " << FSP::Config::DEFAULT_SERVER_STORAGE << "\n";
    std::cout << "[Status] Initializing Core Network Engine (Member 1)...\n";
    std::cout << "----------------------------------------------------\n";
    std::cout << "Ready to bind and listen on TCP port " << FSP::Config::SERVER_PORT << ".\n";
    
    return 0;
}
