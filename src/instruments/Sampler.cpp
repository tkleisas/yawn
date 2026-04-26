#include "instruments/Sampler.h"

namespace yawn {
namespace instruments {

void Sampler::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) v.env.setSampleRate(sampleRate);
    applyDefaults();
}

void Sampler::reset() {
    for (auto& v : m_voices) {
        v.active = false; v.env.reset();
        v.filterLow = v.filterBand = 0.0f;
    }
    m_voiceCounter = 0;
}

void Sampler::loadSample(const float* data, int numFrames, int numChannels,
                int rootNote) {
    m_sampleData.assign(data, data + numFrames * numChannels);
    m_sampleFrames = numFrames;
    m_sampleChannels = numChannels;
    m_rootNote = rootNote;
    m_loopStart = 0.0f;
    m_loopEnd = 1.0f;
}

void Sampler::clearSample() {
    m_sampleData.clear();
    m_sampleFrames = 0;
    m_sampleChannels = 1;
}

void Sampler::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    if (m_sampleFrames == 0) return;

    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn()) noteOn(msg.note, msg.velocity, msg.channel);
        else if (msg.isNoteOff()) noteOff(msg.note, msg.channel);
        else if (msg.isCC() && msg.ccNumber == 123) {
            for (auto& v : m_voices)
                if (v.active) { v.env.gate(false); }
        }
    }

    // Compute loop boundaries in frames
    int loopStartFrame = static_cast<int>(m_loopStart * m_sampleFrames);
    int loopEndFrame   = static_cast<int>(m_loopEnd * m_sampleFrames);
    if (loopEndFrame <= loopStartFrame) loopEndFrame = m_sampleFrames;
    bool looping = (m_loopStart > 0.001f || m_loopEnd < 0.999f);

    for (int v = 0; v < kMaxVoices; ++v) {
        auto& voice = m_voices[v];
        if (!voice.active) continue;

        for (int i = 0; i < numFrames; ++i) {
            int pos0 = (int)voice.playPos;

            // Handle loop/end boundaries
            if (looping) {
                if (!m_reverse && pos0 >= loopEndFrame) {
                    voice.playPos = loopStartFrame;
                    pos0 = loopStartFrame;
                } else if (m_reverse && pos0 < loopStartFrame) {
                    voice.playPos = loopEndFrame - 1;
                    pos0 = loopEndFrame - 1;
                }
            } else {
                if (!m_reverse && pos0 >= m_sampleFrames) {
                    voice.active = false; break;
                } else if (m_reverse && pos0 < 0) {
                    voice.active = false; break;
                }
            }

            pos0 = std::clamp(pos0, 0, m_sampleFrames - 1);
            int pos1 = std::clamp(pos0 + (m_reverse ? -1 : 1), 0, m_sampleFrames - 1);
            float frac = (float)(voice.playPos - pos0);
            if (frac < 0) frac = -frac;

            // Linear interpolation
            float sL = m_sampleData[pos0 * m_sampleChannels] * (1.0f - frac)
                     + m_sampleData[pos1 * m_sampleChannels] * frac;
            float sR = sL;
            if (m_sampleChannels > 1) {
                sR = m_sampleData[pos0 * m_sampleChannels + 1] * (1.0f - frac)
                   + m_sampleData[pos1 * m_sampleChannels + 1] * frac;
            }

            // Apply sample gain
            sL *= m_sampleGain;
            sR *= m_sampleGain;

            // SVF filter
            const float cutoffHz = cutoffNormToHz(m_filterCutoff);
            float f = std::min(2.0f * cutoffHz / (float)m_sampleRate, 0.99f);
            float q = 1.0f - m_filterResonance * 0.98f;
            float highL = sL - voice.filterLow - q * voice.filterBand;
            voice.filterBand += f * highL;
            voice.filterLow  += f * voice.filterBand;
            float filteredL = voice.filterLow;
            if (m_sampleChannels > 1) {
                float filterRatio = (std::abs(sL) > 0.0001f) ? filteredL / sL : 1.0f;
                sR *= filterRatio;
            } else {
                sR = filteredL;
            }
            sL = filteredL;

            float env = voice.env.process();
            float gain = env * voice.velocity * m_volume;

            buffer[i * numChannels + 0] += sL * gain;
            if (numChannels > 1)
                buffer[i * numChannels + 1] += sR * gain;

            voice.playPos += m_reverse ? -voice.playSpeed : voice.playSpeed;
        }

        if (voice.env.isIdle())
            voice.active = false;
    }
}

void Sampler::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active = true;
    v.note = note; v.channel = ch;
    v.velocity = velocityToGain(vel16);
    // Start from loop end if reversed, loop start otherwise
    if (m_reverse)
        v.playPos = static_cast<double>(m_loopEnd * m_sampleFrames) - 1.0;
    else
        v.playPos = static_cast<double>(m_loopStart * m_sampleFrames);
    v.playSpeed = std::pow(2.0, ((int)note - m_rootNote) / 12.0);
    v.startOrder = m_voiceCounter++;
    v.filterLow = v.filterBand = 0.0f;
    v.env.setADSR(m_attack, m_decay, m_sustain, m_release);
    v.env.gate(true);
}

void Sampler::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices)
        if (v.active && v.note == note && v.channel == ch)
            v.env.gate(false);
}

int Sampler::findFreeVoice() {
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_voices[i].active) return i;
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (m_voices[i].startOrder < m_voices[oldest].startOrder)
            oldest = i;
    return oldest;
}

void Sampler::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

} // namespace instruments
} // namespace yawn
