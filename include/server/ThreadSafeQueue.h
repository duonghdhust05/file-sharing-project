#ifndef FSP_THREAD_SAFE_QUEUE_H
#define FSP_THREAD_SAFE_QUEUE_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace FSP {

/**
 * @brief Hàng đợi FIFO có giới hạn, an toàn đa luồng (mutex + condition_variable).
 *
 * - tryPush(): không bao giờ chặn; trả về false khi hàng đợi đầy hoặc đã close().
 * - pop(): chặn cho tới khi có phần tử; trả về std::nullopt ngay khi hàng đợi bị close().
 * - close(): đánh thức mọi luồng đang chờ trong pop(); các phần tử còn lại được lấy ra bằng
 *   drain() để giải phóng tài nguyên.
 */
template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t capacity = 0) : m_capacity(capacity) {}

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /// capacity == 0 nghĩa là không giới hạn.
    bool tryPush(T item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed || (m_capacity != 0 && m_items.size() >= m_capacity))
                return false;
            m_items.push_back(std::move(item));
        }
        m_cv.notify_one();
        return true;
    }

    /// Chặn cho tới khi lấy được phần tử. std::nullopt nghĩa là hàng đợi đã đóng.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_closed || !m_items.empty(); });
        if (m_closed)
            return std::nullopt;
        T item = std::move(m_items.front());
        m_items.pop_front();
        return item;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    /// Lấy toàn bộ phần tử còn lại (dùng khi shutdown).
    std::deque<T> drain() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::deque<T> out;
        out.swap(m_items);
        return out;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<T> m_items;
    const size_t m_capacity;
    bool m_closed{false};
};

}  // namespace FSP

#endif  // FSP_THREAD_SAFE_QUEUE_H
