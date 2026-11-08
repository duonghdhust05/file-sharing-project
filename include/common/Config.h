#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace FSP {
namespace Config {

// Network defaults
constexpr uint16_t SERVER_PORT = 8088;
constexpr int BACKLOG = 64;
// Mỗi worker phục vụ trọn vẹn 1 kết nối (thread-per-connection lấy từ pool) -> số worker chính
// là số client được phục vụ đồng thời. NFR-03 yêu cầu tối thiểu 50 clients.
constexpr int THREAD_POOL_SIZE = 64;
// Số kết nối đã accept nhưng đang chờ worker rảnh. Vượt quá -> từ chối (Fail-Closed).
constexpr int MAX_PENDING_CONNECTIONS = 128;
constexpr int BUFFER_SIZE = 128 * 1024;  // 128 KB buffer
constexpr int SOCKET_TIMEOUT_S = 30;

// Sandbox storage defaults
inline const std::string DEFAULT_SERVER_STORAGE = "./storage/server_root";
inline const std::string DEFAULT_CLIENT_STORAGE = "./storage/client_downloads";

}  // namespace Config

// Standard IEEE 802.3 CRC32 implementation
class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length) {
        if (!data || length == 0)
            return 0;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF;
        const uint32_t* table = getTable();
        for (size_t i = 0; i < length; ++i) {
            uint8_t byte = bytes[i];
            crc = (crc >> 8) ^ table[(crc ^ byte) & 0xFF];
        }
        return crc ^ 0xFFFFFFFF;
    }

private:
    struct Table {
        uint32_t entries[256];
        Table() {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (size_t j = 0; j < 8; ++j) {
                    if (c & 1) {
                        c = 0xEDB88320u ^ (c >> 1);
                    } else {
                        c = c >> 1;
                    }
                }
                entries[i] = c;
            }
        }
    };

    static const uint32_t* getTable() {
        // C++11 "magic static": khởi tạo đúng một lần và thread-safe, nhiều worker thread
        // có thể gọi CRC32::calculate() đồng thời mà không bị data race.
        static const Table table;
        return table.entries;
    }
};

}  // namespace FSP

#endif  // FSP_CONFIG_H
