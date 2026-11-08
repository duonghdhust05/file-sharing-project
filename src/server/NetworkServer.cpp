#include "server/NetworkServer.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <utility>
#include <vector>

namespace FSP {

namespace {
std::mutex g_logMutex;

void logLine(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << "[NetworkServer] " << message << std::endl;
}

void runHook(const NetworkServer::ConnectionHook& hook, Connection& connection, const char* name) {
    if (!hook)
        return;
    try {
        hook(connection);
    } catch (const std::exception& ex) {
        logLine(std::string(name) + " hook threw: " + ex.what());
    } catch (...) {
        logLine(std::string(name) + " hook threw unknown exception");
    }
}
}  // namespace

NetworkServer::NetworkServer(EventDispatcher& dispatcher, ServerOptions options)
    : m_dispatcher(dispatcher), m_options(std::move(options)) {}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::openListener() {
    Socket listener;
    if (!listener.isValid()) {
        logLine("socket() failed, error " + std::to_string(Socket::lastError()));
        return false;
    }
    if (!listener.bind(m_options.port, m_options.host)) {
        logLine("bind(" + m_options.host + ":" + std::to_string(m_options.port) +
                ") failed, error " + std::to_string(Socket::lastError()));
        return false;
    }
    if (!listener.listen(m_options.backlog)) {
        logLine("listen() failed, error " + std::to_string(Socket::lastError()));
        return false;
    }
    m_boundPort = listener.localPort();
    m_listener = std::move(listener);
    m_pool = std::make_unique<ServerWorkerThreadPool>(m_options.workerCount,
                                                      m_options.maxPendingConnections);
    return true;
}

bool NetworkServer::start() {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (m_running.load())
        return false;
    m_stopRequested = false;
    if (!openListener())
        return false;
    m_running = true;
    m_acceptThread = std::thread(&NetworkServer::acceptLoop, this);
    logLine("Listening on " + m_options.host + ":" + std::to_string(port()) + " with " +
            std::to_string(m_pool->workerCount()) + " worker threads");
    return true;
}

bool NetworkServer::run() {
    if (!start())
        return false;
    // Luồng gọi chỉ chờ tín hiệu dừng (có thể đến từ signal handler qua requestStop()).
    while (!m_stopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_options.acceptPollIntervalMs));
    }
    stop();
    return true;
}

void NetworkServer::requestStop() noexcept {
    m_stopRequested.store(true);
}

void NetworkServer::stop() {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    m_stopRequested = true;
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    if (!m_running.load())
        return;
    cleanup();
    m_running = false;
    logLine("Stopped. accepted=" + std::to_string(totalAccepted()) +
            " rejected=" + std::to_string(totalRejected()));
}

size_t NetworkServer::activeConnections() const {
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    return m_connections.size();
}

void NetworkServer::acceptLoop() {
    while (!m_stopRequested.load()) {
        int ready = m_listener.waitReadable(m_options.acceptPollIntervalMs);
        if (ready == 0)
            continue;
        if (ready < 0) {
            if (m_stopRequested.load())
                break;
            logLine("poll() on listener failed, error " + std::to_string(Socket::lastError()));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::string clientIp;
        uint16_t clientPort = 0;
        Socket client = m_listener.accept(&clientIp, &clientPort);
        if (!client.isValid())
            continue;  // vd: client RST trước khi accept xong -> bỏ qua, không làm sập server
        ++m_totalAccepted;

        client.setNoDelay(true);
        if (m_options.idleTimeoutSeconds > 0)
            client.setTimeout(m_options.idleTimeoutSeconds);  // giới hạn cả thời gian send()

        const uint64_t id = m_nextConnectionId++;
        auto connection = std::make_shared<Connection>(std::move(client), id, clientIp, clientPort);
        if (m_options.idleTimeoutSeconds > 0)
            connection->setIdleTimeoutMs(m_options.idleTimeoutSeconds * 1000);
        {
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            m_connections.emplace(id, connection);
        }

        bool queued = m_pool->submit([this, connection] { serveConnection(connection); });
        if (!queued) {
            ++m_totalRejected;
            logLine("Rejecting " + connection->peerAddress() + ": worker queue is full");
            connection->sendError(SERVER_BUSY_EVENT, StatusCode::INTERNAL_SERVER_ERROR,
                                  "server busy, try again later");
            connection->requestClose();
            std::lock_guard<std::mutex> lock(m_connectionsMutex);
            m_connections.erase(id);
        }
    }
}

void NetworkServer::serveConnection(const std::shared_ptr<Connection>& connection) {
    if (!m_stopRequested.load()) {
        runHook(m_onConnect, *connection, "onConnect");

        Packet packet;
        while (!m_stopRequested.load() && !connection->isClosing() && connection->receive(packet)) {
            m_dispatcher.dispatch(*connection, packet);
        }

        runHook(m_onDisconnect, *connection, "onDisconnect");
    }
    connection->requestClose();

    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    m_connections.erase(connection->id());
    // Socket thật sự được close() khi shared_ptr cuối cùng (giữ bởi task) bị hủy.
}

void NetworkServer::cleanup() {
    m_listener.close();

    // Đánh thức mọi worker đang bị chặn trong recv() để chúng thoát vòng lặp.
    std::vector<std::shared_ptr<Connection>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        snapshot.reserve(m_connections.size());
        for (auto& entry : m_connections)
            snapshot.push_back(entry.second);
    }
    for (auto& connection : snapshot)
        connection->requestClose();
    snapshot.clear();

    if (m_pool) {
        m_pool->shutdown();
        m_pool.reset();
    }

    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    m_connections.clear();
}

}  // namespace FSP
