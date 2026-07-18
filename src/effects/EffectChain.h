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
        // Iterate the published snapshot: reset() is also called from
        // the audio thread (engine command handlers), where m_slots is
        // off-limits. Effects are alive in both views — the retire list
        // keeps removed ones until the grace period elapses.
        const Snapshot* s = rtSnapshot();
        if (!s) return;
        for (int i = 0; i < s->count; ++i) {
            if (s->ptrs[i]) s->ptrs[i]->reset();
        }
    }

    void process(float* buffer, int numFrames, int numChannels) {
        const Snapshot* s = rtSnapshot();
        if (!s) return;
        for (int i = 0; i < s->count; ++i) {
            auto* fx = s->ptrs[i];
            if (fx && !fx->bypassed())
                fx->process(buffer, numFrames, numChannels);
        }
    }

    // Fan the per-track sidechain pointer out to every effect in the
    // chain. Same routing as the instrument — effects on a track read
    // the same source the instrument does. Called by AudioEngine each
    // block from the per-track sidechain dispatch (alongside the
    // existing instrument setSidechainInput call). Effects
    // that don't override supportsSidechain() simply ignore the
    // pointer.
    void setSidechainInput(const float* buffer) {
        const Snapshot* s = rtSnapshot();
        if (!s) return;
        for (int i = 0; i < s->count; ++i)
            if (s->ptrs[i]) s->ptrs[i]->setSidechainInput(buffer);
    }

    // Fan the host tempo out to every effect (see AudioEffect::setTempo).
    void setTempo(double bpm, double beatPosition, bool playing) {
        const Snapshot* s = rtSnapshot();
        if (!s) return;
        for (int i = 0; i < s->count; ++i)
            if (s->ptrs[i]) s->ptrs[i]->setTempo(bpm, beatPosition, playing);
    }

    // UI thread only. The published snapshot is rebuilt before return,
    // so the audio thread sees the new effect on its next block.
    AudioEffect* insert(int slot, std::unique_ptr<AudioEffect> effect) {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        effect->init(m_sampleRate, m_maxBlockSize);
        effect->setRetireList(retireList());
        retireEffect(std::move(m_slots[slot]));  // replaced occupant, if any
        m_slots[slot] = std::move(effect);
        recountSlots();
        publishSnapshot();
        return m_slots[slot].get();
    }

    AudioEffect* append(std::unique_ptr<AudioEffect> effect) {
        for (int i = 0; i < kMaxEffectsPerChain; ++i) {
            if (!m_slots[i]) return insert(i, std::move(effect));
        }
        return nullptr;
    }

    // Unlink the effect at `slot` and hand ownership to the caller.
    // The audio thread may still be processing it — callers that
    // intend to destroy it MUST park it in the engine's retire list
    // (AudioEngine::retireList()) instead of letting the unique_ptr
    // go out of scope. The snapshot is republished either way.
    std::unique_ptr<AudioEffect> remove(int slot) {
        if (slot < 0 || slot >= kMaxEffectsPerChain) return nullptr;
        auto fx = std::move(m_slots[slot]);
        recountSlots();
        publishSnapshot();
        return fx;
    }

    // Remove + deferred destruction via the retire list. Use this for
    // delete flows; remove() only when the effect stays alive
    // somewhere else (e.g. an undo command holding the unique_ptr).
    void removeRetired(int slot) {
        retireEffect(remove(slot));
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
    // delay-compensation. Called from the audio thread (PDC) and the
    // UI — both read the published snapshot.
    int latencySamples() const {
        const Snapshot* s = rtSnapshot();
        if (!s) return 0;
        int total = 0;
        for (int i = 0; i < s->count; ++i) {
            if (s->ptrs[i] && !s->ptrs[i]->bypassed())
                total += s->ptrs[i]->latencySamples();
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
