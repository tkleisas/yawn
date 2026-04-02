#pragma once

#include "midi/MidiEffect.h"
#include <memory>

namespace yawn {
namespace midi {

class MidiEffectChain {
public:
    static constexpr int kMaxMidiEffects = 8;

    void init(double sampleRate) {
        m_sampleRate = sampleRate;
        for (int i = 0; i < m_count; ++i)
            if (m_effects[i]) m_effects[i]->init(sampleRate);
    }

    void reset() {
        for (int i = 0; i < m_count; ++i)
            if (m_effects[i]) m_effects[i]->reset();
    }

    void process(MidiBuffer& buffer, int numFrames, const TransportInfo& transport) {
        for (int i = 0; i < m_count; ++i) {
            if (m_effects[i] && !m_effects[i]->bypassed())
                m_effects[i]->process(buffer, numFrames, transport);
        }
    }

    bool addEffect(std::unique_ptr<MidiEffect> effect) {
        if (m_count >= kMaxMidiEffects) return false;
        effect->init(m_sampleRate);
        m_effects[m_count++] = std::move(effect);
        return true;
    }

    std::unique_ptr<MidiEffect> removeEffect(int index) {
        if (index < 0 || index >= m_count) return nullptr;
        auto removed = std::move(m_effects[index]);
        for (int i = index; i < m_count - 1; ++i)
            m_effects[i] = std::move(m_effects[i + 1]);
        m_effects[--m_count] = nullptr;
        return removed;
    }

    // Move effect from one position to another, shifting others
    void moveEffect(int fromIndex, int toIndex) {
        if (fromIndex == toIndex) return;
        if (fromIndex < 0 || fromIndex >= m_count) return;
        if (toIndex < 0 || toIndex >= m_count) return;
        auto fx = std::move(m_effects[fromIndex]);
        if (fromIndex < toIndex) {
            for (int i = fromIndex; i < toIndex; ++i)
                m_effects[i] = std::move(m_effects[i + 1]);
        } else {
            for (int i = fromIndex; i > toIndex; --i)
                m_effects[i] = std::move(m_effects[i - 1]);
        }
        m_effects[toIndex] = std::move(fx);
    }

    MidiEffect* effect(int index) {
        return (index >= 0 && index < m_count) ? m_effects[index].get() : nullptr;
    }
    const MidiEffect* effect(int index) const {
        return (index >= 0 && index < m_count) ? m_effects[index].get() : nullptr;
    }

    void clear() {
        for (int i = 0; i < m_count; ++i)
            m_effects[i].reset();
        m_count = 0;
    }

    int  count() const { return m_count; }
    bool empty() const { return m_count == 0; }

private:
    std::unique_ptr<MidiEffect> m_effects[kMaxMidiEffects];
    int    m_count      = 0;
    double m_sampleRate = 44100.0;
};

} // namespace midi
} // namespace yawn
