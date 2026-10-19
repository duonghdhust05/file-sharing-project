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

    std::cout << "====================================================\n";
    std::cout << "  ALL M1 NETWORK TESTS PASSED SUCCESSFULLY! (3/3)   \n";
    std::cout << "====================================================\n";
    return 0;
}