// M1 integration tests: NetworkServer + ServerWorkerThreadPool + EventDispatcher + NetworkClient
// chạy qua TCP loopback thật (127.0.0.1, cổng do OS cấp). Không mock tầng mạng.

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "client/NetworkClient.h"
#include "common/Connection.h"
#include "common/EventDispatcher.h"
#include "common/PacketHandler.h"
#include "server/NetworkServer.h"
#include "test_support.h"

using namespace FSP;

namespace {
ServerOptions testOptions(size_t workers = 8, size_t pending = 64) {
    ServerOptions o;
    o.host = "127.0.0.1";
    o.port = 0;  // OS chọn cổng trống -> test chạy song song không đụng cổng 8088
    o.workerCount = workers;
    o.maxPendingConnections = pending;
    o.acceptPollIntervalMs = 50;
    return o;
}

/// Handler "ECHO": trả lại nguyên payload trong trường data.
void registerEcho(EventDispatcher& dispatcher) {
    dispatcher.registerHandler("ECHO", [](Connection& conn, const Packet& pkt) {
        conn.respond(pkt.event, StatusCode::OK, pkt.payload.empty() ? "{}" : pkt.text());
    });
}

std::string expectedEcho(const std::string& json) {
    return "{\"status\":200,\"data\":" + json + "}";
}
}  // namespace

int testRequestResponseRoundTrip() {
    std::cout << "[TEST] Running testRequestResponseRoundTrip...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    NetworkServer server(dispatcher, testOptions());
    TEST_ASSERT(server.start(), "server starts");
    TEST_ASSERT(server.port() != 0, "server reports bound port");
    TEST_ASSERT(!server.start(), "second start() refused while running");

    NetworkClient client;
    TEST_ASSERT(client.connect("localhost", server.port()), "client connects via hostname");
    auto r = client.request("ECHO", "{\"msg\":\"xin chao\"}");
    TEST_ASSERT(r.has_value(), "got a response");
    TEST_ASSERT(r->ok() && r->status == 200, "status 200");
    TEST_ASSERT(r->body == expectedEcho("{\"msg\":\"xin chao\"}"), "echo body exact");

    auto unknown = client.request("DIR_LIST", "{}");
    TEST_ASSERT(unknown.has_value(), "unknown event still answered");
    TEST_ASSERT(unknown->type == MessageType::ERROR_RESP && unknown->status == 400,
                "unknown event -> ERROR_RESP 400");
    TEST_ASSERT(client.isConnected(), "connection survives an unknown event");

    client.disconnect();
    server.stop();
    TEST_ASSERT(!server.isRunning(), "server stopped");
    std::cout << "  -> PASSED\n";
    return 0;
}

// PROBE 1 qua TCP thật: 500 frame dồn vào MỘT lần send() -> server phải tách đủ 500 sự kiện
int testStickyPacketsOverRealTcp() {
    std::cout << "[TEST] Running PROBE 1 over TCP: 500 frames in one send()...\n";
    EventDispatcher dispatcher;
    std::atomic<int> received{0};
    std::atomic<int> outOfOrder{0};
    dispatcher.registerHandler("SEQ", [&](Connection& conn, const Packet& pkt) {
        int expected = received.load();
        if (pkt.text() != "{\"i\":" + std::to_string(expected) + "}")
            ++outOfOrder;
        ++received;
        conn.respond(pkt.event, StatusCode::OK, pkt.text());
    });
    NetworkServer server(dispatcher, testOptions());
    TEST_ASSERT(server.start(), "server starts");

    Socket raw;
    TEST_ASSERT(raw.connect("127.0.0.1", server.port()), "raw connect");
    constexpr int N = 500;
    std::vector<uint8_t> sticky;
    for (int i = 0; i < N; ++i) {
        auto f = PacketHandler::packFrame(MessageType::EVENT_REQ, "SEQ",
                                          "{\"i\":" + std::to_string(i) + "}");
        sticky.insert(sticky.end(), f.begin(), f.end());
    }
    TEST_ASSERT(PacketHandler::sendExact(raw, sticky.data(), sticky.size()), "blast all frames");

    for (int i = 0; i < N; ++i) {
        Packet reply;
        TEST_ASSERT(PacketHandler::recvPacket(raw, reply), "receive reply");
        TEST_ASSERT(reply.text() == expectedEcho("{\"i\":" + std::to_string(i) + "}"),
                    "replies arrive in order with exact payload");
    }
    TEST_ASSERT(received.load() == N, "server dispatched exactly 500 events");
    TEST_ASSERT(outOfOrder.load() == 0, "server saw events in order");
    server.stop();
    std::cout << "  -> PASSED: 500 sticky frames -> 500 independent events.\n";
    return 0;
}

// Phân mảnh: gửi 1 frame từng byte một -> server vẫn ghép lại đúng
int testFragmentationOverRealTcp() {
    std::cout << "[TEST] Running fragmentation: 1 frame sent byte-by-byte...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    NetworkServer server(dispatcher, testOptions());
    TEST_ASSERT(server.start(), "server starts");

    Socket raw;
    TEST_ASSERT(raw.connect("127.0.0.1", server.port()), "raw connect");
    raw.setNoDelay(true);
    std::string json = "{\"fragmented\":\"" + std::string(200, 'z') + "\"}";
    auto frame = PacketHandler::packFrame(MessageType::EVENT_REQ, "ECHO", json);
    for (size_t i = 0; i < frame.size(); ++i) {
        TEST_ASSERT(raw.send(&frame[i], 1) == 1, "send 1 byte");
        if (i % 16 == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Packet reply;
    TEST_ASSERT(PacketHandler::recvPacket(raw, reply), "receive reply");
    TEST_ASSERT(reply.text() == expectedEcho(json), "reassembled payload exact");
    server.stop();
    std::cout << "  -> PASSED: " << frame.size() << " single-byte segments reassembled.\n";
    return 0;
}

// NFR-03: 50 client đồng thời, mỗi client 20 request, không deadlock, không lẫn dữ liệu
int testFiftyConcurrentClients() {
    std::cout << "[TEST] Running 50 concurrent clients x 20 requests...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    std::atomic<int> connects{0}, disconnects{0};
    NetworkServer server(dispatcher, testOptions(64, 128));
    server.setOnConnect([&](Connection&) { ++connects; });
    server.setOnDisconnect([&](Connection&) { ++disconnects; });
    TEST_ASSERT(server.start(), "server starts");

    constexpr int CLIENTS = 50;
    constexpr int REQUESTS = 20;
    std::atomic<int> okCount{0}, failures{0};
    std::vector<std::thread> threads;
    for (int c = 0; c < CLIENTS; ++c) {
        threads.emplace_back([&, c] {
            NetworkClient client;
            if (!client.connect("127.0.0.1", server.port())) {
                ++failures;
                return;
            }
            for (int i = 0; i < REQUESTS; ++i) {
                std::string json =
                    "{\"client\":" + std::to_string(c) + ",\"req\":" + std::to_string(i) + "}";
                auto r = client.request("ECHO", json, 10000);
                if (r && r->ok() && r->body == expectedEcho(json))
                    ++okCount;
                else
                    ++failures;
            }
        });
    }
    for (auto& t : threads)
        t.join();

    TEST_ASSERT(failures.load() == 0, "no failed or mismatched request");
    TEST_ASSERT(okCount.load() == CLIENTS * REQUESTS, "all 1000 requests answered correctly");
    TEST_ASSERT(TestSupport::waitUntil([&] { return server.activeConnections() == 0; }),
                "server released every connection after clients left");
    TEST_ASSERT(connects.load() == CLIENTS && disconnects.load() == CLIENTS,
                "onConnect/onDisconnect called once per client");
    TEST_ASSERT(server.totalAccepted() == CLIENTS, "accepted counter");
    server.stop();
    std::cout << "  -> PASSED: " << okCount.load() << " responses, 0 errors.\n";
    return 0;
}

// Fail-Closed: frame dị dạng -> server đóng đúng kết nối đó, vẫn phục vụ client khác
int testMalformedFramesCloseConnection() {
    std::cout << "[TEST] Running malformed frame fail-closed...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    std::atomic<int> disconnects{0};
    NetworkServer server(dispatcher, testOptions());
    server.setOnDisconnect([&](Connection&) { ++disconnects; });
    TEST_ASSERT(server.start(), "server starts");

    auto expectClosedAfter = [&](std::vector<uint8_t> bytes, const char* what) -> int {
        Socket raw;
        TEST_ASSERT(raw.connect("127.0.0.1", server.port()), "raw connect");
        TEST_ASSERT(PacketHandler::sendExact(raw, bytes.data(), bytes.size()), "send garbage");
        TEST_ASSERT(raw.waitReadable(3000) == 1, what);
        char b;
        TEST_ASSERT(raw.recv(&b, 1) <= 0, what);
        return 0;
    };

    auto good = PacketHandler::packFrame(MessageType::EVENT_REQ, "ECHO", "{\"a\":1}");

    auto badMagic = good;
    badMagic[0] = 0x55;
    if (expectClosedAfter(badMagic, "bad magic -> server closes connection") != 0)
        return 1;

    auto badCrc = good;
    badCrc.back() ^= 0x01;
    if (expectClosedAfter(badCrc, "bad CRC32 -> server closes connection") != 0)
        return 1;

    auto hugeLen = good;
    uint32_t len = htonl(0xFFFFFFF0u);
    std::memcpy(hugeLen.data() + 2, &len, 4);
    if (expectClosedAfter(hugeLen, "4GB payload_len -> server closes connection") != 0)
        return 1;

    TEST_ASSERT(TestSupport::waitUntil([&] { return disconnects.load() == 3; }),
                "three bad connections released");

    NetworkClient client;
    TEST_ASSERT(client.connect("127.0.0.1", server.port()), "server still accepts new clients");
    auto r = client.request("ECHO", "{\"alive\":true}");
    TEST_ASSERT(r && r->ok(), "server still serves after attacks");
    server.stop();
    std::cout << "  -> PASSED\n";
    return 0;
}

// Back-pressure: 1 worker, hàng đợi 1 -> client thứ 3 bị từ chối bằng SERVER_BUSY
int testServerBusyRejection() {
    std::cout << "[TEST] Running server-busy rejection...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    NetworkServer server(dispatcher, testOptions(1, 1));
    TEST_ASSERT(server.start(), "server starts");

    NetworkClient a;
    TEST_ASSERT(a.connect("127.0.0.1", server.port()), "client A connects");
    TEST_ASSERT(a.request("ECHO", "{}").has_value(), "A is being served by the only worker");

    NetworkClient b;
    TEST_ASSERT(b.connect("127.0.0.1", server.port()), "client B connects (queued)");
    TEST_ASSERT(TestSupport::waitUntil([&] { return server.activeConnections() == 2; }),
                "B is queued");

    NetworkClient c;
    TEST_ASSERT(c.connect("127.0.0.1", server.port()), "client C TCP connect");
    Packet busy;
    TEST_ASSERT(c.receive(busy, 3000) == NetworkClient::RecvStatus::OK, "C receives a frame");
    TEST_ASSERT(busy.type == MessageType::ERROR_RESP, "C gets ERROR_RESP");
    TEST_ASSERT(busy.event == NetworkServer::SERVER_BUSY_EVENT, "event SERVER_BUSY");
    TEST_ASSERT(Json::parseStatus(busy.text()) == 500, "status 500");
    Packet none;
    TEST_ASSERT(c.receive(none, 3000) == NetworkClient::RecvStatus::DISCONNECTED,
                "C is disconnected after rejection");
    TEST_ASSERT(server.totalRejected() == 1, "rejected counter");

    // Khi A rời đi, B được worker phục vụ
    a.disconnect();
    auto rb = b.request("ECHO", "{\"b\":1}", 5000);
    TEST_ASSERT(rb && rb->ok(), "queued client B served once worker frees up");
    server.stop();
    std::cout << "  -> PASSED\n";
    return 0;
}

// Handler đẩy nhiều frame từ nhiều luồng lên cùng 1 kết nối -> client nhận đủ, không hỏng frame
int testServerPushFromManyThreads() {
    std::cout << "[TEST] Running server push from many threads...\n";
    EventDispatcher dispatcher;
    dispatcher.registerHandler("BURST", [](Connection& conn, const Packet& pkt) {
        std::vector<std::thread> pushers;
        for (int t = 0; t < 4; ++t) {
            pushers.emplace_back([&conn, t] {
                std::string body(4096, static_cast<char>('A' + t));
                for (int i = 0; i < 50; ++i)
                    conn.send(MessageType::EVENT_RESP, "PUSH", body);
            });
        }
        for (auto& p : pushers)
            p.join();
        conn.respond(pkt.event, StatusCode::OK, "{\"pushed\":200}");
    });
    NetworkServer server(dispatcher, testOptions());
    TEST_ASSERT(server.start(), "server starts");

    NetworkClient client;
    TEST_ASSERT(client.connect("127.0.0.1", server.port()), "connect");
    auto r = client.request("BURST", "{}", 10000);
    TEST_ASSERT(r && r->ok(), "BURST acknowledged");
    int pushes = 0;
    Packet p;
    while (client.receive(p, 500) == NetworkClient::RecvStatus::OK) {
        TEST_ASSERT(p.event == "PUSH" && p.payload.size() == 4096, "push frame intact");
        char c = static_cast<char>(p.payload[0]);
        for (uint8_t b : p.payload)
            TEST_ASSERT(static_cast<char>(b) == c, "push payload not interleaved");
        ++pushes;
    }
    TEST_ASSERT(pushes == 200, "all 200 pushed frames kept by client backlog/socket");
    server.stop();
    std::cout << "  -> PASSED\n";
    return 0;
}

// stop() với nhiều client đang treo kết nối phải kết thúc nhanh và ngắt tất cả
int testStopWithIdleClients() {
    std::cout << "[TEST] Running stop() with idle clients...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    auto server = std::make_unique<NetworkServer>(dispatcher, testOptions(4, 16));
    TEST_ASSERT(server->start(), "server starts");

    std::vector<std::unique_ptr<NetworkClient>> clients;
    for (int i = 0; i < 10; ++i) {  // 4 đang được phục vụ + 6 đang xếp hàng
        clients.push_back(std::make_unique<NetworkClient>());
        TEST_ASSERT(clients.back()->connect("127.0.0.1", server->port()), "connect");
    }
    TEST_ASSERT(TestSupport::waitUntil([&] { return server->activeConnections() == 10; }),
                "10 connections tracked");

    auto t0 = std::chrono::steady_clock::now();
    server->stop();
    auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    TEST_ASSERT(elapsedMs < 3000, "stop() completes quickly even with blocked workers");
    TEST_ASSERT(server->activeConnections() == 0, "all connections released");

    for (auto& c : clients) {
        Packet p;
        TEST_ASSERT(c->receive(p, 3000) == NetworkClient::RecvStatus::DISCONNECTED,
                    "every client observes the disconnect");
    }
    server.reset();  // destructor sau stop() không được treo/crash
    std::cout << "  -> PASSED: stopped in " << elapsedMs << " ms.\n";
    return 0;
}

// run() chặn luồng gọi; requestStop() (an toàn cho signal handler) làm run() trả về
int testRunAndRequestStop() {
    std::cout << "[TEST] Running run() + requestStop()...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    NetworkServer server(dispatcher, testOptions());
    std::atomic<bool> runReturned{false};
    bool runResult = false;
    std::thread mainLike([&] {
        runResult = server.run();
        runReturned = true;
    });
    TEST_ASSERT(TestSupport::waitUntil([&] { return server.isRunning() && server.port() != 0; }),
                "run() started the server");
    NetworkClient client;
    TEST_ASSERT(client.connect("127.0.0.1", server.port()), "connect");
    TEST_ASSERT(client.request("ECHO", "{}").has_value(), "served while run() blocks");
    server.requestStop();
    bool returned = TestSupport::waitUntil([&] { return runReturned.load(); }, 3000);
    mainLike.join();
    TEST_ASSERT(returned && runResult, "run() returns true after requestStop()");
    TEST_ASSERT(!server.isRunning(), "server not running after run() returns");

    // Bind thất bại -> start() trả false, không treo
    ServerOptions conflict = testOptions();
    NetworkServer first(dispatcher, conflict);
    TEST_ASSERT(first.start(), "first server on random port");
    conflict.port = first.port();
    NetworkServer second(dispatcher, conflict);
    TEST_ASSERT(!second.start(), "second server on same port must fail to bind");
    first.stop();
    std::cout << "  -> PASSED\n";
    return 0;
}

// Idle timeout: client kết nối rồi im lặng -> server tự ngắt, giải phóng worker
int testIdleTimeout() {
    std::cout << "[TEST] Running idle timeout...\n";
    EventDispatcher dispatcher;
    registerEcho(dispatcher);
    ServerOptions options = testOptions(2, 4);
    options.idleTimeoutSeconds = 1;
    NetworkServer server(dispatcher, options);
    TEST_ASSERT(server.start(), "server starts");

    NetworkClient active;
    TEST_ASSERT(active.connect("127.0.0.1", server.port()), "connect");
    TEST_ASSERT(active.request("ECHO", "{}").has_value(), "served before going idle");

    auto t0 = std::chrono::steady_clock::now();
    Packet p;
    auto status = active.receive(p, 5000);
    auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    TEST_ASSERT(status == NetworkClient::RecvStatus::DISCONNECTED, "idle client disconnected");
    TEST_ASSERT(elapsedMs >= 800 && elapsedMs < 3000, "disconnect happens after ~1s of silence");
    TEST_ASSERT(TestSupport::waitUntil([&] { return server.activeConnections() == 0; }),
                "worker released");
    server.stop();
    std::cout << "  -> PASSED: idle client dropped after " << elapsedMs << " ms.\n";
    return 0;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "   M1 SERVER INTEGRATION TESTS (Real TCP Loopback)  \n";
    std::cout << "====================================================\n";
    int (*tests[])() = {testRequestResponseRoundTrip,
                        testStickyPacketsOverRealTcp,
                        testFragmentationOverRealTcp,
                        testFiftyConcurrentClients,
                        testMalformedFramesCloseConnection,
                        testServerBusyRejection,
                        testServerPushFromManyThreads,
                        testStopWithIdleClients,
                        testRunAndRequestStop,
                        testIdleTimeout};
    int count = 0;
    for (auto test : tests) {
        if (test() != 0)
            return 1;
        ++count;
    }
    std::cout << "====================================================\n";
    std::cout << "  ALL M1 INTEGRATION TESTS PASSED (" << count << "/" << count << ")\n";
    std::cout << "====================================================\n";
    return 0;
}
