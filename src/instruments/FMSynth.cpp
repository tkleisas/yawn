#include "instruments/FMSynth.h"

namespace yawn {
namespace instruments {

constexpr FMSynth::Algorithm FMSynth::kAlgorithms[FMSynth::kNumAlgos];

void FMSynth::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices)
        for (auto& e : v.env) e.setSampleRate(sampleRate);
    applyDefaults();
}

void FMSynth::reset() {
    for (auto& v : m_voices) {
        v.active = false;
        for (int o = 0; o < kNumOps; ++o) {
            v.phase[o] = 0.0; v.prevOut[o] = 0.0f; v.env[o].reset();
        }
    }
    m_voiceCounter = 0;
}

void FMSynth::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn()) noteOn(msg.note, msg.velocity, msg.channel);
        else if (msg.isNoteOff()) noteOff(msg.note, msg.channel);
        else if (msg.isCC() && msg.ccNumber == 123) {
            for (auto& v : m_voices)
                if (v.active) { for (auto& e : v.env) e.gate(false); }
        }
        else if (msg.type == midi::MidiMessage::Type::PitchBend)
            m_pitchBend = midi::Convert::pb32toFloat(msg.value);
    }

    const auto& algo = kAlgorithms[m_algorithm];

    for (int v = 0; v < kMaxVoices; ++v) {
        auto& voice = m_voices[v];
        if (!voice.active) continue;

        float baseFreq = noteToFreq(voice.note) *
            std::pow(2.0f, m_pitchBend * 2.0f / 12.0f);

        for (int i = 0; i < numFrames; ++i) {
            float opOut[kNumOps] = {};

            // Process operators from highest to lowest (modulators first)
            for (int op = kNumOps - 1; op >= 0; --op) {
                float modIn = 0.0f;
                for (int src = 0; src < kNumOps; ++src)
                    if (algo.modMatrix[op][src])
                        modIn += opOut[src];

                // Self-feedback on operator 3 (highest)
                if (op == kNumOps - 1)
                    modIn += voice.prevOut[op] * m_feedback;

                float freq = baseFreq * m_opRatio[op];
                float envVal = voice.env[op].process();
                float totalPhase = (float)voice.phase[op] + modIn;
                opOut[op] = std::sin(totalPhase * 2.0f * (float)M_PI) *
                            m_opLevel[op] * envVal;

                voice.prevOut[op] = opOut[op];
                voice.phase[op] += freq / m_sampleRate;
                if (voice.phase[op] >= 1.0) voice.phase[op] -= 1.0;
            }

            // Sum carrier outputs
            float sample = 0.0f;
            for (int op = 0; op < kNumOps; ++op)
                if (algo.isCarrier[op])
                    sample += opOut[op];

            sample *= voice.velocity * m_volume;
            buffer[i * numChannels + 0] += sample;
            if (numChannels > 1)
                buffer[i * numChannels + 1] += sample;
        }

        // Check if all envelopes idle → deactivate voice
        bool allIdle = true;
        for (int o = 0; o < kNumOps; ++o)
            if (!voice.env[o].isIdle()) { allIdle = false; break; }
        if (allIdle) voice.active = false;
    }
}

void FMSynth::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active = true;
    v.note = note; v.channel = ch;
    v.velocity = velocityToGain(vel16);
    v.startOrder = m_voiceCounter++;
    for (int o = 0; o < kNumOps; ++o) {
        v.phase[o] = 0.0; v.prevOut[o] = 0.0f;
        v.env[o].setADSR(m_opAttack[o], 0.1f, 1.0f, m_opRelease[o]);
        v.env[o].gate(true);
    }
}

void FMSynth::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch)
            for (auto& e : v.env) e.gate(false);
}

int FMSynth::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

void FMSynth::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

} // namespace instruments
} // namespace yawn
