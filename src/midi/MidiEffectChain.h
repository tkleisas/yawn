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
        // Iterate the published snapshot: reset() runs on the audio
        // thread (ResetMidiEffectChainMsg, recording finalize), where
        // m_slots is off-limits. See EffectChain::reset.
        const Snapshot* s = rtSnapshot();
        if (!s) return;
        for (int i = 0; i < s->count; ++i)
            if (s->ptrs[i]) s->ptrs[i]->reset();
    }

    void process(MidiBuffer& buffer, int numFrames, const TransportInfo& transport) {
        const Snapshot* s = rtSnapshot();
        if (!s) return;
        for (int i = 0; i < s->count; ++i) {
            if (s->ptrs[i] && !s->ptrs[i]->bypassed())
                s->ptrs[i]->process(buffer, numFrames, transport);
        }
    }

    // UI thread only; the audio thread picks the new chain up on its
    // next block (see ChainBase's thread contract).
    bool addEffect(std::unique_ptr<MidiEffect> effect) {
        if (m_count >= kMaxMidiEffects) return false;
        effect->init(m_sampleRate);
        m_slots[m_count++] = std::move(effect);
        publishSnapshot();
        return true;
    }

    // Unlink and hand ownership to the caller — callers that intend
    // to destroy the effect MUST park it in the engine's retire list
    // (AudioEngine::retireList()); see EffectChain::remove.
    std::unique_ptr<MidiEffect> removeEffect(int index) {
        if (index < 0 || index >= m_count) return nullptr;
        auto removed = std::move(m_slots[index]);
        for (int i = index; i < m_count - 1; ++i)
            m_slots[i] = std::move(m_slots[i + 1]);
        m_slots[--m_count] = nullptr;
        publishSnapshot();
        return removed;
    }

    // Remove + deferred destruction via the retire list (delete
    // flows; see EffectChain::removeRetired).
    void removeEffectRetired(int index) {
        retireEffect(removeEffect(index));
    }

    MidiEffect* effect(int index) {
        return (index >= 0 && index < m_count) ? m_slots[index].get() : nullptr;
    }
    const MidiEffect* effect(int index) const {
        return (index >= 0 && index < m_count) ? m_slots[index].get() : nullptr;
    }

    // Audio-thread accessor (snapshot-based; see ChainBase).
    MidiEffect* effectRt(int index) const { return effectAtRt(index); }

private:
    double m_sampleRate = kDefaultSampleRate;
};

} // namespace midi
} // namespace yawn
