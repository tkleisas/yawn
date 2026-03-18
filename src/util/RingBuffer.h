#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <type_traits>

namespace yawn {
namespace util {

// Lock-free single-producer single-consumer ring buffer.
// Safe for one writer thread (UI) and one reader thread (audio) without locks.
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    RingBuffer() : m_head(0), m_tail(0) {}

    bool push(const T& item) {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;
        if (next == m_tail.load(std::memory_order_acquire)) {
            return false; // full
        }
        m_buffer[head] = item;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) {
            return false; // empty
        }
        item = m_buffer[tail];
        m_tail.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    size_t size() const {
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }

    bool empty() const {
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }

    void clear() {
        m_tail.store(m_head.load(std::memory_order_relaxed), std::memory_order_release);
    }

private:
    static constexpr size_t kMask = Capacity - 1;
    std::array<T, Capacity> m_buffer;
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
};

} // namespace util
} // namespace yawn
