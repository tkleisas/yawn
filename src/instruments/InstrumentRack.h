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

    // --- Instrument interface ---

    const char* name() const override { return "Instrument Rack"; }
    const char* id()   const override { return "instrack"; }

    int parameterCount() const override { return 1; }

    const InstrumentParameterInfo& parameterInfo(int) const override {
        static const InstrumentParameterInfo p = {"Volume", 0, 1, 1, "", false};
        return p;
    }

    float getParameter(int) const override { return m_rackVolume; }

    void setParameter(int, float v) override {
        m_rackVolume = std::clamp(v, 0.0f, 1.0f);
    }

private:
    Chain m_chains[kMaxChains];
    int   m_numChains = 0;
    float m_rackVolume = 1.0f;

    midi::MidiBuffer   m_chainMidi[kMaxChains];
    std::vector<float> m_chainBufHeap;
    float*             m_chainBufPtrs[kMaxChains] = {};
};

} // namespace instruments
} // namespace yawn
