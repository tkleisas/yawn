#pragma once

#include "effects/AudioEffect.h"
#include "core/ChainBase.h"
#include <array>
#include <memory>

namespace yawn {
namespace effects {

static constexpr int kMaxEffectsPerChain = 8;

class EffectChain : public ChainBase<AudioEffect, kMaxEffectsPerChain> {
    using Base = ChainBase<AudioEffect, kMaxEffectsPerChain>;
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

    AudioEffect* insert(int slot, std::unique_ptr<AudioEffect> effect) {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        effect->init(m_sampleRate, m_maxBlockSize);
        m_slots[slot] = std::move(effect);
        recountSlots();
        return m_slots[slot].get();
    }

    AudioEffect* append(std::unique_ptr<AudioEffect> effect) {
        for (int i = 0; i < kMaxEffectsPerChain; ++i) {
            if (!m_slots[i]) return insert(i, std::move(effect));
        }
        return nullptr;
    }

    std::unique_ptr<AudioEffect> remove(int slot) {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        auto fx = std::move(m_slots[slot]);
        recountSlots();
        return fx;
    }

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

    double m_sampleRate = 44100.0;
    int    m_maxBlockSize = 4096;
};

} // namespace effects
} // namespace yawn
