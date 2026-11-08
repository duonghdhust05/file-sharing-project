#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "common/Config.h"
#include "common/FSPProtocol.h"
#include "common/PacketHandler.h"
#include "common/Socket.h"

#define TEST_ASSERT(cond, msg)                                       \
    do {                                                             \
        if (!(cond)) {                                               \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ")\n"; \
            return 1;                                                \
        }                                                            \
    } while (0)

// Test 1: Testing the encapsulation and decapsulation of a standard frame
int testSingleFramePackUnpack() {
    std::cout << "[TEST] Running testSingleFramePackUnpack...\n";
    std::string testJson = "{\"username\":\"nguyen\",\"action\":\"ping\"}";

    auto frame = FSP::PacketHandler::packFrame(FSP::MessageType::EVENT_REQ, FSP::Events::AUTH_LOGIN,
                                               testJson);

    TEST_ASSERT(frame.size() == FSP::HEADER_SIZE + testJson.size(), "Frame size mismatch");

    size_t offset = 0;
    FSP::FSPHeader hdr;
    std::vector<uint8_t> payload;
    bool ok = FSP::PacketHandler::unpackFromBuffer(frame, offset, hdr, payload);

    TEST_ASSERT(ok, "unpackFromBuffer must succeed on valid frame");
    TEST_ASSERT(hdr.magic == FSP::MAGIC_BYTE, "Magic byte must be 0xAA");
    TEST_ASSERT(std::string(hdr.event) == FSP::Events::AUTH_LOGIN, "Event name must match");
    TEST_ASSERT(payload.size() == testJson.size(), "Payload size must match");

    std::string receivedStr(payload.begin(), payload.end());
    TEST_ASSERT(receivedStr == testJson, "Payload content must be identical");

    std::cout << "  -> PASSED: Single frame packaging validated.\n";
    return 0;
}

// Test 2: PROBE 1 - Testing for the detection and disaggregation of "Sticky Packets"
int testProbeStickyPackets() {
    std::cout << "[TEST] Running PROBE 1: Sticky Packets Separation...\n";

    // The client emulator sends two consecutive packets that end up bundled together in a single
    // TCP buffer.
    std::string payload1 = "{\"id\":1,\"msg\":\"First Packet\"}";
    std::string payload2 = "{\"id\":2,\"msg\":\"Second Packet Following Immediately\"}";

    auto frame1 =
        FSP::PacketHandler::packFrame(FSP::MessageType::EVENT_REQ, "FIRST_EVENT", payload1);
    auto frame2 =
        FSP::PacketHandler::packFrame(FSP::MessageType::EVENT_RESP, "SECOND_EVENT", payload2);

    // Combine both frames into a single contiguous byte array
    std::vector<uint8_t> stickyBuffer;
    stickyBuffer.insert(stickyBuffer.end(), frame1.begin(), frame1.end());
    stickyBuffer.insert(stickyBuffer.end(), frame2.begin(), frame2.end());

    TEST_ASSERT(stickyBuffer.size() == frame1.size() + frame2.size(),
                "Combined buffer size mismatch");

    // Processing the extraction of frame 1
    size_t offset = 0;
    FSP::FSPHeader h1;
    std::vector<uint8_t> p1;
    bool ok1 = FSP::PacketHandler::unpackFromBuffer(stickyBuffer, offset, h1, p1);

    TEST_ASSERT(ok1, "Must extract Packet 1 successfully");
    TEST_ASSERT(std::string(h1.event) == "FIRST_EVENT", "Packet 1 event mismatch");
    TEST_ASSERT(std::string(p1.begin(), p1.end()) == payload1, "Packet 1 payload mismatch");
    TEST_ASSERT(offset == frame1.size(), "Offset must shift exactly past Packet 1");

    // Handle the extraction of frame 2 at the very next offset
    FSP::FSPHeader h2;
    std::vector<uint8_t> p2;
    bool ok2 = FSP::PacketHandler::unpackFromBuffer(stickyBuffer, offset, h2, p2);

    TEST_ASSERT(ok2, "Must extract Packet 2 successfully without losing bytes");
    TEST_ASSERT(std::string(h2.event) == "SECOND_EVENT", "Packet 2 event mismatch");
    TEST_ASSERT(std::string(p2.begin(), p2.end()) == payload2, "Packet 2 payload mismatch");
    TEST_ASSERT(offset == stickyBuffer.size(), "Offset must reach end of sticky buffer");

    std::cout << "  -> PASSED: Successfully decoupled 2 sticky packets with zero byte loss!\n";
    return 0;
}

// Test 3: Testing for rejection when the CRC32 is modified or the Magic Byte is incorrect
int testCorruptedFrameProbe() {
    std::cout << "[TEST] Running Corrupted Frame Detection...\n";
    std::string testJson = "{\"secret\":\"confidential\"}";
    auto frame = FSP::PacketHandler::packFrame(FSP::MessageType::EVENT_REQ, "TEST_EVENT", testJson);

    // Case A: Flipping 1 bit in the payload -> CRC32 mismatch
    auto corruptedCrcFrame = frame;
    corruptedCrcFrame[FSP::HEADER_SIZE + 2] ^= 0xFF;  // Tamper payload

    size_t offset = 0;
    FSP::FSPHeader hdr;
    std::vector<uint8_t> p;
    bool okCrc = FSP::PacketHandler::unpackFromBuffer(corruptedCrcFrame, offset, hdr, p);
    TEST_ASSERT(!okCrc, "System must REJECT frame when CRC32 is tampered");

    // Case B: Magic Byte is incorrect
    auto corruptedMagicFrame = frame;
    corruptedMagicFrame[0] = 0x99;  // Incorrect magic byte (expected 0xAA)
    offset = 0;
    bool okMagic = FSP::PacketHandler::unpackFromBuffer(corruptedMagicFrame, offset, hdr, p);
    TEST_ASSERT(!okMagic, "System must REJECT frame when Magic Byte != 0xAA");

    std::cout << "  -> PASSED: Fail-closed verified on corrupted frames.\n";
    return 0;
}

// Test 4: Header bị làm giả -> phải từ chối trước khi cấp phát hay đọc tràn bộ nhớ
int testMalformedHeaderProbe() {
    std::cout << "[TEST] Running Malformed Header Detection...\n";
    std::string testJson = "{\"k\":\"v\"}";
    auto frame = FSP::PacketHandler::packFrame(FSP::MessageType::EVENT_REQ, "TEST_EVENT", testJson);
    FSP::FSPHeader hdr;
    std::vector<uint8_t> p;

    // Case A: event name lấp đầy 32 bytes, không có '\0' -> std::string(hdr.event) sẽ đọc tràn
    auto noTerminator = frame;
    std::memset(noTerminator.data() + 6, 'A', FSP::EVENT_NAME_MAX_LEN);
    size_t offset = 0;
    TEST_ASSERT(!FSP::PacketHandler::unpackFromBuffer(noTerminator, offset, hdr, p),
                "Must REJECT event name without null terminator");
    TEST_ASSERT(offset == 0, "Offset must not move on rejected frame");

    // Case B: type không thuộc MessageType
    auto badType = frame;
    badType[1] = 0x7F;
    offset = 0;
    TEST_ASSERT(!FSP::PacketHandler::unpackFromBuffer(badType, offset, hdr, p),
                "Must REJECT unknown message type");

    // Case C: payload_len khổng lồ (2 GB) -> từ chối, không resize vector 2 GB
    auto huge = frame;
    uint32_t hugeLen = htonl(0x7FFFFFFFu);
    std::memcpy(huge.data() + 2, &hugeLen, sizeof(hugeLen));
    offset = 0;
    TEST_ASSERT(!FSP::PacketHandler::unpackFromBuffer(huge, offset, hdr, p),
                "Must REJECT payload_len above maxPayloadSize()");

    // Case D: payload rỗng nhưng checksum khác 0
    auto empty = FSP::PacketHandler::packFrame(FSP::MessageType::PING, "PING", nullptr, 0);
    TEST_ASSERT(empty.size() == FSP::HEADER_SIZE, "Empty frame must be exactly 42 bytes");
    empty[FSP::HEADER_SIZE - 1] = 0x01;
    offset = 0;
    TEST_ASSERT(!FSP::PacketHandler::unpackFromBuffer(empty, offset, hdr, p),
                "Must REJECT empty payload with non-zero checksum");

    std::cout << "  -> PASSED: Malformed headers rejected (fail-closed).\n";
    return 0;
}

// Test 5: Frame bị phân mảnh -> chưa đủ byte thì chưa bóc, đủ byte thì bóc chính xác
int testFragmentedBuffer() {
    std::cout << "[TEST] Running Fragmented Frame Reassembly...\n";
    std::string payload(1000, 'x');
    payload[0] = '{';
    auto frame = FSP::PacketHandler::packFrame(FSP::MessageType::EVENT_REQ, "BIG", payload);
    FSP::FSPHeader hdr;
    std::vector<uint8_t> p;

    std::vector<uint8_t> partial;
    // Nạp từng mảnh 7 byte, chỉ được bóc thành công khi mảnh cuối cùng tới
    for (size_t i = 0; i < frame.size(); i += 7) {
        size_t end = std::min(frame.size(), i + 7);
        partial.insert(partial.end(), frame.begin() + i, frame.begin() + end);
        size_t offset = 0;
        bool ok = FSP::PacketHandler::unpackFromBuffer(partial, offset, hdr, p);
        if (partial.size() < frame.size()) {
            TEST_ASSERT(!ok, "Must not extract an incomplete frame");
            TEST_ASSERT(offset == 0, "Offset must stay at 0 for incomplete frame");
        } else {
            TEST_ASSERT(ok, "Must extract the frame once all bytes arrived");
            TEST_ASSERT(offset == frame.size(), "Offset must land exactly at end of frame");
            TEST_ASSERT(std::string(p.begin(), p.end()) == payload, "Payload must be intact");
        }
    }
    std::cout << "  -> PASSED: Fragmented frame reassembled only when complete.\n";
    return 0;
}

// Test 6: sendPacket từ chối dữ liệu vi phạm hợp đồng trước khi đẩy lên mạng
int testSendPacketGuards() {
    std::cout << "[TEST] Running sendPacket contract guards...\n";
    FSP::Socket unconnected;
    std::string longName(FSP::EVENT_NAME_MAX_LEN, 'E');  // 32 ký tự -> không còn chỗ cho '\0'
    TEST_ASSERT(
        !FSP::PacketHandler::sendPacket(unconnected, FSP::MessageType::EVENT_REQ, longName, "{}"),
        "Must refuse event names of 32+ chars");
    std::vector<uint8_t> tooBig(FSP::PacketHandler::maxPayloadSize() + 1, 0);
    TEST_ASSERT(!FSP::PacketHandler::sendPacket(unconnected, FSP::MessageType::FILE_CHUNK, "CHUNK",
                                                tooBig.data(), tooBig.size()),
                "Must refuse payload above maxPayloadSize()");
    // Một FILE_CHUNK đầy đủ (88B header + 64KB data) phải nằm trong giới hạn
    TEST_ASSERT(sizeof(FSP::FileChunkHeader) + FSP::DEFAULT_CHUNK_SIZE <=
                    FSP::PacketHandler::maxPayloadSize(),
                "A full 64KB FILE_CHUNK must fit in maxPayloadSize()");
    std::cout << "  -> PASSED: sendPacket guards enforced.\n";
    return 0;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "   M1 CORE NETWORK ENGINE TESTS (Adversarial Suite) \n";
    std::cout << "====================================================\n";

    if (testSingleFramePackUnpack() != 0)
        return 1;
    if (testProbeStickyPackets() != 0)
        return 1;
    if (testCorruptedFrameProbe() != 0)
        return 1;
    if (testMalformedHeaderProbe() != 0)
        return 1;
    if (testFragmentedBuffer() != 0)
        return 1;
    if (testSendPacketGuards() != 0)
        return 1;

    std::cout << "====================================================\n";
    std::cout << "  ALL M1 NETWORK TESTS PASSED SUCCESSFULLY! (6/6)   \n";
    std::cout << "====================================================\n";
    return 0;
}
