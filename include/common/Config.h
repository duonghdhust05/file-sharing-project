#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

#include <cstdint>
#include <string>

namespace FSP {
namespace Config {

    // Network defaults
    constexpr uint16_t SERVER_PORT      = 8088;
    constexpr int      BACKLOG          = 64;
    constexpr int      THREAD_POOL_SIZE = 16;
    constexpr int      BUFFER_SIZE      = 128 * 1024; // 128 KB buffer
    constexpr int      SOCKET_TIMEOUT_S = 30;

    // Sandbox storage defaults
    inline const std::string DEFAULT_SERVER_STORAGE = "./storage/server_root";
    inline const std::string DEFAULT_CLIENT_STORAGE = "./storage/client_downloads";

} // namespace Config

// Fast lookup CRC32 helper
class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; ++i) {
            uint8_t byte = bytes[i];
            crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF];
        }
        return crc ^ 0xFFFFFFFF;
    }

private:
    static inline const uint32_t table[16] = {
        0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
        0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
        0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
        0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C
    };
};

} // namespace FSP

#endif // FSP_CONFIG_H
