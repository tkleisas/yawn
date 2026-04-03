#pragma once

#include "instruments/Instrument.h"
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
    };

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        int stride = maxBlockSize * 2; // stereo interleaved
        m_chainBufHeap.resize(kMaxChains * stride, 0.0f);
        for (int i = 0; i < kMaxChains; ++i)
            m_chainBufPtrs[i] = m_chainBufHeap.data() + i * stride;
        for (int i = 0; i < m_numChains; ++i)
            if (m_chains[i].instrument)
                m_chains[i].instrument->init(sampleRate, maxBlockSize);
    }

    void reset() override {
        for (int i = 0; i < m_numChains; ++i)
            if (m_chains[i].instrument)
                m_chains[i].instrument->reset();
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        for (int c = 0; c < m_numChains; ++c) {
            auto& chain = m_chains[c];
            if (!chain.enabled || !chain.instrument) continue;

            // Filter MIDI for this chain's key/velocity range
            m_chainMidi[c].clear();
            for (int i = 0; i < midi.count(); ++i) {
                const auto& msg = midi[i];
                if (msg.isNoteOn()) {
                    uint8_t vel7 = midi::Convert::vel16to7(msg.velocity);
                    if (msg.note >= chain.keyLow  && msg.note <= chain.keyHigh &&
                        vel7  >= chain.velLow  && vel7  <= chain.velHigh)
                        m_chainMidi[c].addMessage(msg);
                } else if (msg.isNoteOff()) {
                    if (msg.note >= chain.keyLow && msg.note <= chain.keyHigh)
                        m_chainMidi[c].addMessage(msg);
                } else {
                    m_chainMidi[c].addMessage(msg); // CC, PB pass through
                }
            }

            // Clear chain scratch buffer and render
            std::memset(m_chainBufPtrs[c], 0,
                        numFrames * numChannels * sizeof(float));
            chain.instrument->process(
                m_chainBufPtrs[c], numFrames, numChannels, m_chainMidi[c]);

            // Mix into output with volume/pan
            float angle = (chain.pan + 1.0f) * 0.25f * (float)M_PI;
            float gL = chain.volume * std::cos(angle);
            float gR = chain.volume * std::sin(angle);
            for (int i = 0; i < numFrames; ++i) {
                buffer[i * numChannels + 0] +=
                    m_chainBufPtrs[c][i * numChannels + 0] * gL;
                if (numChannels > 1)
                    buffer[i * numChannels + 1] +=
                        m_chainBufPtrs[c][i * numChannels + 1] * gR;
            }
        }
    }

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
        return removed;
    }

    Chain&       chain(int i)       { return m_chains[i]; }
    const Chain& chain(int i) const { return m_chains[i]; }
    int chainCount() const { return m_numChains; }

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
            {"Volume",       0,   1,    1.0f,  "",  false},
            {"Chain Vol",    0,   1,    1.0f,  "",  false},
            {"Chain Pan",   -1,   1,    0.0f,  "",  false},
            {"Key Low",      0, 127,    0.0f,  "",  false},
            {"Key High",     0, 127,  127.0f,  "",  false},
            {"Vel Low",      0, 127,    1.0f,  "",  false},
            {"Vel High",     0, 127,  127.0f,  "",  false},
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
