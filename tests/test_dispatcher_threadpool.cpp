// M1.3 EventDispatcher & M1.4 ServerWorkerThreadPool tests.
// Mọi assertion kiểm tra trên dữ liệu thật: byte nhận về qua socket loopback, bộ đếm thực thi,
// thread id thực tế, use_count của tài nguyên.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "common/Connection.h"
#include "common/EventDispatcher.h"
#include "common/PacketHandler.h"
#include "server/ServerWorkerThreadPool.h"
#include "server/ThreadSafeQueue.h"
#include "test_support.h"

using namespace FSP;

namespace {
Packet makePacket(const std::string& event, const std::string& json) {
    Packet p;
    p.type = MessageType::EVENT_REQ;
    p.event = event;
    p.payload.assign(json.begin(), json.end());
    return p;
}
}  // namespace

// ---------------------------------------------------------------------------
// JSON envelope helpers
// ---------------------------------------------------------------------------
int testJsonEnvelope() {
    std::cout << "[TEST] Running testJsonEnvelope...\n";
    TEST_ASSERT(Json::escape("a\"b\\c\n") == "a\\\"b\\\\c\\n", "escape quotes/backslash/newline");
    TEST_ASSERT(Json::escape(std::string(1, '\x01')) == "\\u0001", "escape control chars");
    TEST_ASSERT(
        Json::envelope(StatusCode::OK, "{\"x\":1}") == "{\"status\":200,\"data\":{\"x\":1}}",
        "envelope format");
    TEST_ASSERT(Json::envelope(StatusCode::CREATED, "") == "{\"status\":201,\"data\":{}}",
                "empty data becomes {}");
    TEST_ASSERT(Json::errorEnvelope(StatusCode::FORBIDDEN, "no \"access\"") ==
                    "{\"status\":403,\"data\":{\"error\":\"no \\\"access\\\"\"}}",
                "error envelope escapes message");
    TEST_ASSERT(Json::parseStatus("{\"status\":404,\"data\":{}}") == 404, "parse status");
    TEST_ASSERT(Json::parseStatus("{\"data\":{}}") == -1, "missing status -> -1");
    TEST_ASSERT(Json::parseStatus("{\"status\":abc,") == -1, "non-numeric status -> -1");
    TEST_ASSERT(Json::parseStatus("") == -1, "empty -> -1");
    std::cout << "  -> PASSED\n";
    return 0;
}

// ---------------------------------------------------------------------------
// M1.3 EventDispatcher
// ---------------------------------------------------------------------------
int testDispatchRoutesToHandler() {
    std::cout << "[TEST] Running testDispatchRoutesToHandler...\n";
    Socket serverSide{INVALID_SOCKET_FD}, clientSide{INVALID_SOCKET_FD};
    TEST_ASSERT(TestSupport::makeLoopbackPair(serverSide, clientSide), "loopback pair");
    Connection conn(std::move(serverSide), 7, "127.0.0.1", 1234);

    EventDispatcher dispatcher;
    std::string seenPayload;
    int loginCalls = 0, listCalls = 0;
    TEST_ASSERT(dispatcher.registerHandler(Events::AUTH_LOGIN,
                                           [&](Connection& c, const Packet& pkt) {
                                               ++loginCalls;
                                               seenPayload = pkt.text();
                                               c.respond(pkt.event, StatusCode::OK,
                                                         "{\"token\":\"abc\"}");
                                           }),
                "register AUTH_LOGIN");
    TEST_ASSERT(dispatcher.registerHandler(Events::DIR_LIST,
                                           [&](Connection&, const Packet&) { ++listCalls; }),
                "register DIR_LIST");

    auto result =
        dispatcher.dispatch(conn, makePacket(Events::AUTH_LOGIN, "{\"username\":\"nguyen\"}"));
    TEST_ASSERT(result == EventDispatcher::Result::HANDLED, "result HANDLED");
    TEST_ASSERT(loginCalls == 1 && listCalls == 0, "only the matching handler runs");
    TEST_ASSERT(seenPayload == "{\"username\":\"nguyen\"}", "handler receives exact payload");

    Packet reply;
    TEST_ASSERT(PacketHandler::recvPacket(clientSide, reply), "client receives reply frame");
    TEST_ASSERT(reply.type == MessageType::EVENT_RESP, "reply type EVENT_RESP");
    TEST_ASSERT(reply.event == Events::AUTH_LOGIN, "reply keeps event name");
    TEST_ASSERT(reply.text() == "{\"status\":200,\"data\":{\"token\":\"abc\"}}", "reply envelope");
    std::cout << "  -> PASSED\n";
    return 0;
}

int testDispatchUnknownAndErrors() {
    std::cout << "[TEST] Running testDispatchUnknownAndErrors...\n";
    Socket serverSide{INVALID_SOCKET_FD}, clientSide{INVALID_SOCKET_FD};
    TEST_ASSERT(TestSupport::makeLoopbackPair(serverSide, clientSide), "loopback pair");
    Connection conn(std::move(serverSide), 1);

    EventDispatcher dispatcher;
    dispatcher.registerHandler(
        "THROWS", [](Connection&, const Packet&) { throw std::runtime_error("boom"); });

    // Sự kiện chưa đăng ký -> ERROR_RESP 400
    auto r1 = dispatcher.dispatch(conn, makePacket("NOT_REGISTERED", "{}"));
    TEST_ASSERT(r1 == EventDispatcher::Result::UNKNOWN_EVENT, "UNKNOWN_EVENT result");
    Packet reply;
    TEST_ASSERT(PacketHandler::recvPacket(clientSide, reply), "receive 400 reply");
    TEST_ASSERT(reply.type == MessageType::ERROR_RESP, "unknown event -> ERROR_RESP");
    TEST_ASSERT(reply.event == "NOT_REGISTERED", "error echoes event name");
    TEST_ASSERT(Json::parseStatus(reply.text()) == 400, "unknown event -> status 400");

    // Handler ném exception -> ERROR_RESP 500, không lộ chi tiết nội bộ ("boom")
    auto r2 = dispatcher.dispatch(conn, makePacket("THROWS", "{}"));
    TEST_ASSERT(r2 == EventDispatcher::Result::HANDLER_FAILED, "HANDLER_FAILED result");
    TEST_ASSERT(PacketHandler::recvPacket(clientSide, reply), "receive 500 reply");
    TEST_ASSERT(Json::parseStatus(reply.text()) == 500, "exception -> status 500");
    TEST_ASSERT(reply.text().find("boom") == std::string::npos, "internal error text not leaked");
    TEST_ASSERT(!conn.isClosing(), "connection stays open after handler exception");

    // Fallback handler bắt mọi sự kiện chưa đăng ký
    int fallbackCalls = 0;
    dispatcher.setFallbackHandler([&](Connection&, const Packet&) { ++fallbackCalls; });
    auto r3 = dispatcher.dispatch(conn, makePacket("ANYTHING", "{}"));
    TEST_ASSERT(r3 == EventDispatcher::Result::FALLBACK && fallbackCalls == 1, "fallback used");
    std::cout << "  -> PASSED\n";
    return 0;
}

int testRegistrationRules() {
    std::cout << "[TEST] Running testRegistrationRules...\n";
    EventDispatcher dispatcher;
    auto noop = [](Connection&, const Packet&) {};
    TEST_ASSERT(dispatcher.registerHandler("A_EVENT", noop), "first registration ok");
    TEST_ASSERT(!dispatcher.registerHandler("A_EVENT", noop), "duplicate registration refused");
    TEST_ASSERT(!dispatcher.registerHandler("", noop), "empty name refused");
    TEST_ASSERT(!dispatcher.registerHandler(std::string(32, 'X'), noop), "32-char name refused");
    TEST_ASSERT(dispatcher.registerHandler(std::string(31, 'Y'), noop), "31-char name accepted");
    TEST_ASSERT(!dispatcher.registerHandler("NULL_HANDLER", nullptr), "null handler refused");
    TEST_ASSERT(dispatcher.replaceHandler("A_EVENT", noop), "replaceHandler overrides");
    TEST_ASSERT(dispatcher.registeredEvents().size() == 2, "exactly two events registered");
    TEST_ASSERT(dispatcher.unregisterHandler("A_EVENT"), "unregister existing");
    TEST_ASSERT(!dispatcher.hasHandler("A_EVENT"), "handler removed");
    TEST_ASSERT(!dispatcher.unregisterHandler("A_EVENT"), "unregister twice returns false");
    std::cout << "  -> PASSED\n";
    return 0;
}

int testConcurrentSendsDoNotInterleave() {
    std::cout << "[TEST] Running testConcurrentSendsDoNotInterleave...\n";
    Socket serverSide{INVALID_SOCKET_FD}, clientSide{INVALID_SOCKET_FD};
    TEST_ASSERT(TestSupport::makeLoopbackPair(serverSide, clientSide), "loopback pair");
    Connection conn(std::move(serverSide), 2);

    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 100;
    // Payload lớn (8 KB) để buộc send() phải chia nhiều lần -> lộ ra lỗi trộn byte nếu thiếu mutex
    std::vector<std::thread> senders;
    for (int t = 0; t < THREADS; ++t) {
        senders.emplace_back([&conn, t] {
            std::string body(8 * 1024, static_cast<char>('a' + t));
            for (int i = 0; i < PER_THREAD; ++i)
                conn.send(MessageType::EVENT_RESP, "T" + std::to_string(t), body);
        });
    }

    int perThreadCount[THREADS] = {0};
    for (int i = 0; i < THREADS * PER_THREAD; ++i) {
        Packet pkt;
        TEST_ASSERT(PacketHandler::recvPacket(clientSide, pkt), "every frame must be valid");
        int t = std::stoi(pkt.event.substr(1));
        TEST_ASSERT(t >= 0 && t < THREADS, "event id in range");
        TEST_ASSERT(pkt.payload.size() == 8 * 1024, "payload size intact");
        for (uint8_t b : pkt.payload) {
            if (b != static_cast<uint8_t>('a' + t)) {
                std::cerr << "[FAIL] frame from thread " << t << " contains foreign bytes\n";
                for (auto& s : senders)
                    s.join();
                return 1;
            }
        }
        ++perThreadCount[t];
    }
    for (auto& s : senders)
        s.join();
    for (int t = 0; t < THREADS; ++t)
        TEST_ASSERT(perThreadCount[t] == PER_THREAD, "each thread delivered all its frames");
    std::cout << "  -> PASSED: " << THREADS * PER_THREAD << " frames from " << THREADS
              << " threads, zero interleaving.\n";
    return 0;
}

int testConnectionAttributesAndClose() {
    std::cout << "[TEST] Running testConnectionAttributesAndClose...\n";
    Socket serverSide{INVALID_SOCKET_FD}, clientSide{INVALID_SOCKET_FD};
    TEST_ASSERT(TestSupport::makeLoopbackPair(serverSide, clientSide), "loopback pair");
    Connection conn(std::move(serverSide), 3, "10.0.0.5", 5555);
    TEST_ASSERT(conn.peerAddress() == "10.0.0.5:5555", "peer address formatting");
    TEST_ASSERT(!conn.attribute("session_token").has_value(), "attribute absent initially");
    conn.setAttribute("session_token", "uuid-123");
    TEST_ASSERT(conn.attribute("session_token").value_or("") == "uuid-123", "attribute stored");
    conn.eraseAttribute("session_token");
    TEST_ASSERT(!conn.attribute("session_token").has_value(), "attribute erased");

    // requestClose() từ luồng khác phải đánh thức receive() đang bị chặn
    std::atomic<bool> receiveReturned{false};
    bool receiveResult = true;
    std::thread reader([&] {
        Packet p;
        receiveResult = conn.receive(p);
        receiveReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(!receiveReturned.load(), "receive() is blocking before close");
    conn.requestClose();
    bool woke = TestSupport::waitUntil([&] { return receiveReturned.load(); }, 2000);
    reader.join();
    TEST_ASSERT(woke, "requestClose() must unblock receive()");
    TEST_ASSERT(!receiveResult, "receive() returns false after close");
    TEST_ASSERT(!conn.send(MessageType::EVENT_RESP, "X", "{}"), "send refused after close");

    // Phía client thấy EOF
    char byte;
    TEST_ASSERT(clientSide.waitReadable(2000) == 1, "client notified");
    TEST_ASSERT(clientSide.recv(&byte, 1) == 0, "client sees orderly shutdown (EOF)");
    std::cout << "  -> PASSED\n";
    return 0;
}

// ---------------------------------------------------------------------------
// M1.4 ThreadSafeQueue & ServerWorkerThreadPool
// ---------------------------------------------------------------------------
int testQueueBasics() {
    std::cout << "[TEST] Running testQueueBasics...\n";
    ThreadSafeQueue<int> q(2);
    TEST_ASSERT(q.tryPush(1) && q.tryPush(2), "push within capacity");
    TEST_ASSERT(!q.tryPush(3), "push beyond capacity refused");
    TEST_ASSERT(q.pop().value_or(-1) == 1 && q.pop().value_or(-1) == 2, "FIFO order");

    std::atomic<bool> popReturned{false};
    std::optional<int> popped = 42;
    std::thread waiter([&] {
        popped = q.pop();
        popReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TEST_ASSERT(!popReturned.load(), "pop blocks on empty queue");
    q.close();
    bool woke = TestSupport::waitUntil([&] { return popReturned.load(); }, 2000);
    waiter.join();
    TEST_ASSERT(woke, "close() wakes blocked pop");
    TEST_ASSERT(!popped.has_value(), "pop returns nullopt after close");
    TEST_ASSERT(!q.tryPush(9), "push refused after close");
    std::cout << "  -> PASSED\n";
    return 0;
}

int testPoolRunsTasksInParallel() {
    std::cout << "[TEST] Running testPoolRunsTasksInParallel...\n";
    constexpr int WORKERS = 4;
    ServerWorkerThreadPool pool(WORKERS, 0);
    TEST_ASSERT(pool.workerCount() == WORKERS, "worker count");

    // Mỗi task chờ cho tới khi đủ WORKERS task cùng đang chạy -> chỉ pass nếu thật sự song song
    std::atomic<int> concurrentlyRunning{0};
    std::atomic<int> reachedBarrier{0};
    std::mutex idsMutex;
    std::set<std::thread::id> ids;
    for (int i = 0; i < WORKERS; ++i) {
        TEST_ASSERT(pool.submit([&] {
            ++concurrentlyRunning;
            {
                std::lock_guard<std::mutex> lock(idsMutex);
                ids.insert(std::this_thread::get_id());
            }
            if (TestSupport::waitUntil([&] { return concurrentlyRunning.load() == WORKERS; }, 3000))
                ++reachedBarrier;
        }),
                    "submit");
    }
    TEST_ASSERT(TestSupport::waitUntil([&] { return reachedBarrier.load() == WORKERS; }, 5000),
                "all tasks must run at the same time");
    TEST_ASSERT(ids.size() == static_cast<size_t>(WORKERS), "tasks ran on distinct threads");

    // Khối lượng lớn: 2000 task, đếm chính xác
    std::atomic<int> counter{0};
    for (int i = 0; i < 2000; ++i)
        TEST_ASSERT(pool.submit([&] { ++counter; }), "submit bulk");
    TEST_ASSERT(TestSupport::waitUntil([&] { return counter.load() == 2000; }), "2000 tasks ran");

    // Task ném exception không được giết worker
    pool.submit([] { throw std::runtime_error("task failure"); });
    std::atomic<bool> after{false};
    pool.submit([&] { after = true; });
    TEST_ASSERT(TestSupport::waitUntil([&] { return after.load(); }), "pool survives exception");
    std::cout << "  -> PASSED\n";
    return 0;
}

int testPoolBoundedQueueAndShutdown() {
    std::cout << "[TEST] Running testPoolBoundedQueueAndShutdown...\n";
    auto pool = std::make_unique<ServerWorkerThreadPool>(1, 2);

    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    TEST_ASSERT(pool->submit([&] {
        started = true;
        TestSupport::waitUntil([&] { return release.load(); }, 10000);
    }),
                "submit blocking task");
    TEST_ASSERT(TestSupport::waitUntil([&] { return started.load(); }), "worker busy");
    TEST_ASSERT(pool->busyWorkers() == 1, "busyWorkers reports 1");

    // Tài nguyên giả lập "kết nối đang xếp hàng": phải được giải phóng khi shutdown hủy task
    auto resource = std::make_shared<int>(1);
    std::atomic<int> pendingRan{0};
    TEST_ASSERT(pool->submit([resource, &pendingRan] { ++pendingRan; }), "queue slot 1");
    TEST_ASSERT(pool->submit([resource, &pendingRan] { ++pendingRan; }), "queue slot 2");
    TEST_ASSERT(!pool->submit([] {}), "queue full -> submit refused (back-pressure)");
    TEST_ASSERT(pool->pendingTasks() == 2, "two tasks pending");
    TEST_ASSERT(resource.use_count() == 3, "pending tasks hold the resource");

    std::thread stopper([&] { pool->shutdown(); });
    TEST_ASSERT(TestSupport::waitUntil([&] { return !pool->isAcceptingTasks(); }),
                "shutdown closes the queue first");
    release = true;  // cho task đang chạy kết thúc để shutdown join được
    stopper.join();

    TEST_ASSERT(pendingRan.load() == 0, "pending tasks are dropped, not executed, on shutdown");
    TEST_ASSERT(resource.use_count() == 1, "dropped tasks released their resources");
    TEST_ASSERT(!pool->submit([] {}), "submit refused after shutdown");
    pool->shutdown();  // idempotent
    std::cout << "  -> PASSED\n";
    return 0;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "   M1.3 DISPATCHER & M1.4 THREAD POOL TESTS         \n";
    std::cout << "====================================================\n";
    int (*tests[])() = {
        testJsonEnvelope,      testDispatchRoutesToHandler,        testDispatchUnknownAndErrors,
        testRegistrationRules, testConcurrentSendsDoNotInterleave, testConnectionAttributesAndClose,
        testQueueBasics,       testPoolRunsTasksInParallel,        testPoolBoundedQueueAndShutdown};
    int count = 0;
    for (auto test : tests) {
        if (test() != 0)
            return 1;
        ++count;
    }
    std::cout << "====================================================\n";
    std::cout << "  ALL DISPATCHER/THREADPOOL TESTS PASSED (" << count << "/" << count << ")\n";
    std::cout << "====================================================\n";
    return 0;
}
