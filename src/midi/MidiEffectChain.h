#pragma once

#include "midi/MidiEffect.h"
#include "core/ChainBase.h"
#include <memory>

namespace yawn {
namespace midi {

static constexpr int kMaxMidiEffects = 8;

class MidiEffectChain : public ChainBase<MidiEffect, kMaxMidiEffects> {
    using Base = ChainBase<MidiEffect, kMaxMidiEffects>;
public:

    void init(double sampleRate) {
        m_sampleRate = sampleRate;
        for (int i = 0; i < m_count; ++i)
            if (m_slots[i]) m_slots[i]->init(sampleRate);
    }

    void reset() {
        for (int i = 0; i < m_count; ++i)
            if (m_slots[i]) m_slots[i]->reset();
    }

    void process(MidiBuffer& buffer, int numFrames, const TransportInfo& transport) {
        for (int i = 0; i < m_count; ++i) {
            if (m_slots[i] && !m_slots[i]->bypassed())
                m_slots[i]->process(buffer, numFrames, transport);
        }
    }

    bool addEffect(std::unique_ptr<MidiEffect> effect) {
        if (m_count >= kMaxMidiEffects) return false;
        effect->init(m_sampleRate);
        m_slots[m_count++] = std::move(effect);
        return true;
    }

    std::unique_ptr<MidiEffect> removeEffect(int index) {
        if (index < 0 || index >= m_count) return nullptr;
        auto removed = std::move(m_slots[index]);
        for (int i = index; i < m_count - 1; ++i)
            m_slots[i] = std::move(m_slots[i + 1]);
        m_slots[--m_count] = nullptr;
        return removed;
    }

    MidiEffect* effect(int index) {
        return (index >= 0 && index < m_count) ? m_slots[index].get() : nullptr;
    }
    const MidiEffect* effect(int index) const {
        return (index >= 0 && index < m_count) ? m_slots[index].get() : nullptr;
    }

private:
    double m_sampleRate = 44100.0;
};

} // namespace midi
} // namespace yawn
