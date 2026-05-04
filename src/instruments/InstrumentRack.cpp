#include "instruments/InstrumentRack.h"
#include "instruments/SubtractiveSynth.h"

namespace yawn {
namespace instruments {

InstrumentRack::InstrumentRack() {
    // Auto-populate with a single full-range SubtractiveSynth chain
    // so the rack makes sound on first add. Without this the user
    // sees an empty rack panel and has to click "Add Chain" before
    // the keys do anything — confusing first impression. Project /
    // preset load wipes this default via clearChains() before
    // re-populating from JSON.
    addChain(std::make_unique<SubtractiveSynth>());
}

void InstrumentRack::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    int stride = maxBlockSize * 2; // stereo interleaved
    m_chainBufHeap.resize(kMaxChains * stride, 0.0f);
    for (int i = 0; i < kMaxChains; ++i)
        m_chainBufPtrs[i] = m_chainBufHeap.data() + i * stride;
    for (int i = 0; i < m_numChains; ++i) {
        if (m_chains[i].instrument)
            m_chains[i].instrument->init(sampleRate, maxBlockSize);
        // Per-chain fx — only init if the chain was allocated by a
        // prior chainFxChain() call (or by the deserializer).
        // Newly-created chains start without an fx chain at all.
        if (m_chains[i].fx)
            m_chains[i].fx->init(sampleRate, maxBlockSize);
    }
}

void InstrumentRack::reset() {
    for (int i = 0; i < m_numChains; ++i) {
        if (m_chains[i].instrument)
            m_chains[i].instrument->reset();
        if (m_chains[i].fx)
            m_chains[i].fx->reset();
    }
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

        // Per-chain fx — process the scratch buffer in place after
        // the instrument fills it but BEFORE we apply chain
        // volume/pan, so the effects see the instrument's natural
        // signal level (compressors / saturation feel right). Same
        // pre-mix routing as DrumRack PadFx.
        if (chain.fx)
            chain.fx->process(m_chainBufPtrs[c], numFrames, numChannels);

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
