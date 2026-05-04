#pragma once

#include "instruments/Instrument.h"
#include "effects/EffectChain.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// Instrument Rack — multi-chain instrument container (like Ableton Instrument Rack).
// Each chain has: key range, velocity range, instrument, volume, pan.
// Routes MIDI to matching chains, renders each into a scratch buffer,
// then mixes with volume/pan into the final output.
class InstrumentRack : public Instrument {
public:
    static constexpr int kMaxChains = 8;

    struct Chain {
        std::unique_ptr<Instrument> instrument;
        uint8_t keyLow  = 0, keyHigh  = 127;
        uint8_t velLow  = 1, velHigh  = 127;
        float   volume  = 1.0f;
        float   pan     = 0.0f; // -1 = L, 0 = center, 1 = R
        bool    enabled = true;
        // Per-chain audio effect chain. Lazy-allocated — nullptr
        // until the user adds the first effect, so chains without
        // effects pay zero memory + zero CPU. Process order:
        // chain.instrument → fx (if any) → mix into rack output
        // with vol/pan. Mirrors DrumRack's per-pad fx pattern.
        std::unique_ptr<effects::EffectChain> fx;
    };

    // Auto-populates one default chain (SubtractiveSynth, full key /
    // velocity range, vol=1, pan=0) so a freshly-added rack makes
    // sound immediately. Project / preset deserialization calls
    // clearChains() first so the saved chain list isn't appended to
    // the default — no duplication on round-trip.
    InstrumentRack();

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    // --- Chain management ---

    bool addChain(std::unique_ptr<Instrument> instrument,
                  uint8_t keyLow = 0, uint8_t keyHigh = 127,
                  uint8_t velLow = 1, uint8_t velHigh = 127) {
        if (m_numChains >= kMaxChains) return false;
        auto& ch = m_chains[m_numChains];
        ch.instrument = std::move(instrument);
        ch.keyLow = keyLow; ch.keyHigh = keyHigh;
        ch.velLow = velLow; ch.velHigh = velHigh;
        if (m_sampleRate > 0 && ch.instrument)
            ch.instrument->init(m_sampleRate, m_maxBlockSize);
        ++m_numChains;
        return true;
    }

    std::unique_ptr<Instrument> removeChain(int index) {
        if (index < 0 || index >= m_numChains) return nullptr;
        auto removed = std::move(m_chains[index].instrument);
        for (int i = index; i < m_numChains - 1; ++i)
            m_chains[i] = std::move(m_chains[i + 1]);
        m_chains[--m_numChains] = Chain{};
        if (m_selectedChain >= m_numChains)
            m_selectedChain = std::max(0, m_numChains - 1);
        return removed;
    }

    // Drop every chain. Used by the project loader before
    // re-populating from JSON so the default chain set up by the
    // constructor isn't appended to the saved list. Cheap — just
    // resets the per-slot Chain to its default-constructed state
    // and zeroes the count.
    void clearChains() {
        for (int i = 0; i < m_numChains; ++i)
            m_chains[i] = Chain{};
        m_numChains = 0;
        m_selectedChain = 0;
    }

    Chain&       chain(int i)       { return m_chains[i]; }
    const Chain& chain(int i) const { return m_chains[i]; }
    int chainCount() const { return m_numChains; }

    // Public accessors for the rack's init parameters — used by App's
    // "Change Instrument" callback to init a freshly-created chain
    // instrument at the same SR / block size the rack runs at.
    double sampleRate()   const { return m_sampleRate; }
    int    maxBlockSize() const { return m_maxBlockSize; }

    // Per-chain effect chain access. Lazy-allocates on first call so
    // the user can directly start adding effects to the returned
    // chain. Returns nullptr only for an out-of-range chain index.
    effects::EffectChain* chainFxChain(int idx) {
        if (idx < 0 || idx >= m_numChains) return nullptr;
        if (!m_chains[idx].fx) {
            m_chains[idx].fx = std::make_unique<effects::EffectChain>();
            if (m_sampleRate > 0)
                m_chains[idx].fx->init(m_sampleRate, m_maxBlockSize);
        }
        return m_chains[idx].fx.get();
    }
    // Const view — returns nullptr if no chain has been allocated
    // (no effects added yet). Project / preset save uses this so
    // chains with no fx don't pollute JSON.
    const effects::EffectChain* chainFxChainOrNull(int idx) const {
        if (idx < 0 || idx >= m_numChains) return nullptr;
        return m_chains[idx].fx.get();
    }
    void clearChainFx(int idx) {
        if (idx >= 0 && idx < m_numChains) m_chains[idx].fx.reset();
    }

    // Selected chain for UI editing
    int  selectedChain() const { return m_selectedChain; }
    void setSelectedChain(int idx) { m_selectedChain = std::clamp(idx, 0, kMaxChains - 1); }

    // --- Instrument interface ---

    const char* name() const override { return "Instrument Rack"; }
    const char* id()   const override { return "instrack"; }

    // Parameter indices
    static constexpr int kVolume      = 0;
    static constexpr int kChainVol    = 1;
    static constexpr int kChainPan    = 2;
    static constexpr int kChainKeyLow = 3;
    static constexpr int kChainKeyHi  = 4;
    static constexpr int kChainVelLow = 5;
    static constexpr int kChainVelHi  = 6;

    int parameterCount() const override { return 7; }

    const InstrumentParameterInfo& parameterInfo(int idx) const override {
        static const InstrumentParameterInfo infos[] = {
            {"Volume",       0,   1,    1.0f,  "",  false, false, WidgetHint::DentedKnob},
            {"Chain Vol",    0,   1,    1.0f,  "",  false, false, WidgetHint::DentedKnob},
            {"Chain Pan",   -1,   1,    0.0f,  "",  false, false, WidgetHint::DentedKnob},
            {"Key Low",      0, 127,    0.0f,  "",  false, false, WidgetHint::StepSelector},
            {"Key High",     0, 127,  127.0f,  "",  false, false, WidgetHint::StepSelector},
            {"Vel Low",      0, 127,    1.0f,  "",  false, false, WidgetHint::StepSelector},
            {"Vel High",     0, 127,  127.0f,  "",  false, false, WidgetHint::StepSelector},
        };
        return infos[std::clamp(idx, 0, 6)];
    }

    float getParameter(int idx) const override {
        if (idx == kVolume) return m_rackVolume;
        int ci = m_selectedChain;
        if (ci < 0 || ci >= m_numChains) return 0.0f;
        const auto& ch = m_chains[ci];
        switch (idx) {
        case kChainVol:    return ch.volume;
        case kChainPan:    return ch.pan;
        case kChainKeyLow: return static_cast<float>(ch.keyLow);
        case kChainKeyHi:  return static_cast<float>(ch.keyHigh);
        case kChainVelLow: return static_cast<float>(ch.velLow);
        case kChainVelHi:  return static_cast<float>(ch.velHigh);
        default:           return 0.0f;
        }
    }

    void setParameter(int idx, float v) override {
        if (idx == kVolume) { m_rackVolume = std::clamp(v, 0.0f, 1.0f); return; }
        int ci = m_selectedChain;
        if (ci < 0 || ci >= m_numChains) return;
        auto& ch = m_chains[ci];
        switch (idx) {
        case kChainVol:    ch.volume  = std::clamp(v, 0.0f, 1.0f); break;
        case kChainPan:    ch.pan     = std::clamp(v, -1.0f, 1.0f); break;
        case kChainKeyLow: ch.keyLow  = static_cast<uint8_t>(std::clamp(v, 0.0f, 127.0f)); break;
        case kChainKeyHi:  ch.keyHigh = static_cast<uint8_t>(std::clamp(v, 0.0f, 127.0f)); break;
        case kChainVelLow: ch.velLow  = static_cast<uint8_t>(std::clamp(v, 0.0f, 127.0f)); break;
        case kChainVelHi:  ch.velHigh = static_cast<uint8_t>(std::clamp(v, 0.0f, 127.0f)); break;
        }
    }

private:
    Chain m_chains[kMaxChains];
    int   m_numChains = 0;
    int   m_selectedChain = 0;
    float m_rackVolume = 1.0f;

    midi::MidiBuffer   m_chainMidi[kMaxChains];
    std::vector<float> m_chainBufHeap;
    float*             m_chainBufPtrs[kMaxChains] = {};
};

} // namespace instruments
} // namespace yawn
