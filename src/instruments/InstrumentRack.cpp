#include "instruments/InstrumentRack.h"

namespace yawn {
namespace instruments {

void InstrumentRack::init(double sampleRate, int maxBlockSize) {
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

void InstrumentRack::reset() {
    for (int i = 0; i < m_numChains; ++i)
        if (m_chains[i].instrument)
            m_chains[i].instrument->reset();
}

void InstrumentRack::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
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

} // namespace instruments
} // namespace yawn
