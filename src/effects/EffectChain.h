#pragma once

// EffectChain — ordered list of AudioEffect instances for a channel.
// Processes effects in series. Owns the effect instances.

#include "effects/AudioEffect.h"
#include <array>
#include <memory>

namespace yawn {
namespace effects {

static constexpr int kMaxEffectsPerChain = 8;

class EffectChain {
public:
    void init(double sampleRate, int maxBlockSize) {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& slot : m_slots) {
            if (slot) slot->init(sampleRate, maxBlockSize);
        }
    }

    void reset() {
        for (auto& slot : m_slots) {
            if (slot) slot->reset();
        }
    }

    void process(float* buffer, int numFrames, int numChannels) {
        for (int i = 0; i < m_count; ++i) {
            auto& fx = m_slots[i];
            if (fx && !fx->bypassed())
                fx->process(buffer, numFrames, numChannels);
        }
    }

    // Insert an effect at a slot (takes ownership). Returns raw pointer.
    AudioEffect* insert(int slot, std::unique_ptr<AudioEffect> effect) {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        effect->init(m_sampleRate, m_maxBlockSize);
        m_slots[slot] = std::move(effect);
        recountSlots();
        return m_slots[slot].get();
    }

    // Append to the next empty slot
    AudioEffect* append(std::unique_ptr<AudioEffect> effect) {
        for (int i = 0; i < kMaxEffectsPerChain; ++i) {
            if (!m_slots[i]) return insert(i, std::move(effect));
        }
        return nullptr; // Full
    }

    // Remove effect at slot (returns ownership)
    std::unique_ptr<AudioEffect> remove(int slot) {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        auto fx = std::move(m_slots[slot]);
        recountSlots();
        return fx;
    }

    // Move effect from one slot to another, shifting others to fill the gap
    void moveEffect(int fromSlot, int toSlot) {
        if (fromSlot == toSlot) return;
        if (fromSlot < 0 || fromSlot >= m_count) return;
        if (toSlot < 0 || toSlot >= m_count) return;
        auto fx = std::move(m_slots[fromSlot]);
        if (fromSlot < toSlot) {
            for (int i = fromSlot; i < toSlot; ++i)
                m_slots[i] = std::move(m_slots[i + 1]);
        } else {
            for (int i = fromSlot; i > toSlot; --i)
                m_slots[i] = std::move(m_slots[i - 1]);
        }
        m_slots[toSlot] = std::move(fx);
    }

    void clear() {
        for (auto& slot : m_slots) slot.reset();
        m_count = 0;
    }

    int count() const { return m_count; }
    bool empty() const { return m_count == 0; }

    AudioEffect* effectAt(int slot) const {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        return m_slots[slot].get();
    }

private:
    void recountSlots() {
        m_count = 0;
        for (int i = 0; i < kMaxEffectsPerChain; ++i) {
            if (m_slots[i]) m_count = i + 1;
        }
    }

    std::array<std::unique_ptr<AudioEffect>, kMaxEffectsPerChain> m_slots;
    int    m_count = 0;
    double m_sampleRate = 44100.0;
    int    m_maxBlockSize = 4096;
};

} // namespace effects
} // namespace yawn
