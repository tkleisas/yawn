#pragma once

#include <array>
#include <memory>
#include <utility>

namespace yawn {

template<typename T, int N>
class ChainBase {
public:
    int  count() const { return m_count; }
    bool empty() const { return m_count == 0; }

    void clear() {
        for (int i = 0; i < N; ++i)
            m_slots[i].reset();
        m_count = 0;
    }

    void moveEffect(int fromIndex, int toIndex) {
        if (fromIndex == toIndex) return;
        if (fromIndex < 0 || fromIndex >= m_count) return;
        if (toIndex < 0 || toIndex >= m_count) return;
        auto elem = std::move(m_slots[fromIndex]);
        if (fromIndex < toIndex) {
            for (int i = fromIndex; i < toIndex; ++i)
                m_slots[i] = std::move(m_slots[i + 1]);
        } else {
            for (int i = fromIndex; i > toIndex; --i)
                m_slots[i] = std::move(m_slots[i - 1]);
        }
        m_slots[toIndex] = std::move(elem);
    }

protected:
    std::array<std::unique_ptr<T>, N> m_slots;
    int m_count = 0;
};

} // namespace yawn
