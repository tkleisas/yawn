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

    // Fan the per-track sidechain pointer out to every effect in the
    // chain. Same routing as the instrument — effects on a track read
    // the same source the instrument does. Called by AudioEngine each
    // block from the per-track sidechain dispatch (alongside the
    // existing m_instruments[t]->setSidechainInput call). Effects
    // that don't override supportsSidechain() simply ignore the
    // pointer.
    void setSidechainInput(const float* buffer) {
        for (auto& slot : m_slots)
            if (slot) slot->setSidechainInput(buffer);
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

    // Sum of latency samples reported by every non-bypassed effect
    // in this chain. Bypassed effects don't add latency because
    // process() is a no-op for them — the audio passes through with
    // zero delay. Used by Mixer to expose per-track / per-bus /
    // master latency totals for the UI and (Latency P2) for auto-
    // delay-compensation.
    int latencySamples() const {
        int total = 0;
        for (auto& slot : m_slots) {
            if (slot && !slot->bypassed())
                total += slot->latencySamples();
        }
        return total;
    }

private:
    void recountSlots() {
        m_count = 0;
        for (int i = 0; i < kMaxEffectsPerChain; ++i) {
            if (m_slots[i]) m_count = i + 1;
        }
    }

    double m_sampleRate = kDefaultSampleRate;
    int    m_maxBlockSize = 4096;
};

} // namespace effects
} // namespace yawn
