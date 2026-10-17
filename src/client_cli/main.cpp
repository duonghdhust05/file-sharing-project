#include <iostream>
#include <string>
#include "common/FSPProtocol.h"
#include "common/Config.h"

int main(int argc, char* argv[]) {
    std::cout << "====================================================\n";
    std::cout << "   FSP CLIENT CLI (Test & Benchmark Tool) v1.0      \n";
    std::cout << "====================================================\n";
    std::cout << "[Usage] fsp_client_cli [server_ip] [port]\n";
    std::cout << "[Config] Default Client Storage: " << FSP::Config::DEFAULT_CLIENT_STORAGE << "\n";
    std::cout << "[Status] Ready to connect to FSP Server.\n";
    
    return 0;
}
