#include "server/ServerWorkerThreadPool.h"

#include <exception>
#include <iostream>
#include <utility>

namespace FSP {

ServerWorkerThreadPool::ServerWorkerThreadPool(size_t workerCount, size_t maxPendingTasks)
    : m_queue(maxPendingTasks) {
    if (workerCount == 0)
        workerCount = 1;
    m_workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        m_workers.emplace_back(&ServerWorkerThreadPool::workerLoop, this);
    }
}

ServerWorkerThreadPool::~ServerWorkerThreadPool() {
    shutdown();
}

bool ServerWorkerThreadPool::submit(Task task) {
    if (!task || m_stopped.load())
        return false;
    return m_queue.tryPush(std::move(task));
}

void ServerWorkerThreadPool::shutdown() {
    if (m_stopped.exchange(true))
        return;
    m_queue.close();
    for (auto& worker : m_workers) {
        if (worker.joinable())
            worker.join();
    }
    // Hủy các task chưa kịp chạy (vd: kết nối đang xếp hàng) -> RAII đóng socket của chúng.
    m_queue.drain();
}

void ServerWorkerThreadPool::workerLoop() {
    for (;;) {
        std::optional<Task> task = m_queue.pop();
        if (!task)
            return;  // Hàng đợi đã đóng
        ++m_busy;
        try {
            (*task)();
        } catch (const std::exception& ex) {
            std::cerr << "[ThreadPool] Task threw exception: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[ThreadPool] Task threw unknown exception\n";
        }
        --m_busy;
    }
}

}  // namespace FSP
