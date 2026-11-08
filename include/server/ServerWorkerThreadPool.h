#ifndef FSP_SERVER_WORKER_THREAD_POOL_H
#define FSP_SERVER_WORKER_THREAD_POOL_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

#include "server/ThreadSafeQueue.h"

namespace FSP {

/**
 * @brief Pool gồm N worker thread cố định lấy task từ ThreadSafeQueue (M1.4).
 *
 * NetworkServer gửi vào pool một task "phục vụ trọn vẹn 1 kết nối client"; worker chạy task đó
 * cho tới khi client ngắt kết nối rồi quay lại lấy task tiếp theo. Số thread không tăng theo
 * số client => tài nguyên server bị chặn trên, không bị tấn công cạn kiệt thread.
 */
class ServerWorkerThreadPool {
public:
    using Task = std::function<void()>;

    /// @param workerCount số worker (>= 1); @param maxPendingTasks giới hạn hàng đợi (0 = vô hạn)
    ServerWorkerThreadPool(size_t workerCount, size_t maxPendingTasks);
    ~ServerWorkerThreadPool();

    ServerWorkerThreadPool(const ServerWorkerThreadPool&) = delete;
    ServerWorkerThreadPool& operator=(const ServerWorkerThreadPool&) = delete;

    /// Đưa task vào hàng đợi. false nếu hàng đợi đầy hoặc pool đã shutdown (task không chạy).
    bool submit(Task task);

    /**
     * @brief Dừng pool: không nhận task mới, chờ các task ĐANG CHẠY kết thúc rồi join thread.
     * Các task còn nằm trong hàng đợi bị hủy (destructor của chúng giải phóng tài nguyên).
     * Idempotent.
     */
    void shutdown();

    size_t workerCount() const noexcept { return m_workers.size(); }
    size_t busyWorkers() const noexcept { return m_busy.load(); }
    size_t pendingTasks() const { return m_queue.size(); }
    bool isAcceptingTasks() const { return !m_queue.isClosed(); }

private:
    void workerLoop();

    ThreadSafeQueue<Task> m_queue;
    std::vector<std::thread> m_workers;
    std::atomic<size_t> m_busy{0};
    std::atomic<bool> m_stopped{false};
};

}  // namespace FSP

#endif  // FSP_SERVER_WORKER_THREAD_POOL_H
