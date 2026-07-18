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

void Sampler::publishSample(std::shared_ptr<const SampleData> sd) {
    // Publish first, then retire the previous owner — the audio thread
    // always sees a valid buffer (old or new), and the retired one
    // outlives any in-flight block.
    m_rtSample.store(sd.get(), std::memory_order_release);
    auto old = std::move(m_uiSample);
    m_uiSample = std::move(sd);
    if (old) retireObject(std::move(old));
}

void Sampler::loadSample(const float* data, int numFrames, int numChannels,
                int rootNote) {
    auto sd = std::make_shared<SampleData>();
    sd->samples.assign(data, data + numFrames * numChannels);
    sd->frames = numFrames;
    sd->channels = numChannels;
    publishSample(std::move(sd));
    m_rootNote = rootNote;
    m_loopStart = 0.0f;
    m_loopEnd = 1.0f;
}

void Sampler::clearSample() {
    publishSample(nullptr);
}

void Sampler::process(float* buffer, int numFrames, int numChannels,
             const midi::MidiBuffer& midi) {
    // Load the published buffer once per block; voices read through
    // this local for the whole block, so a UI-side swap can't change
    // (or free) the data mid-render.
    const SampleData* sd = m_rtSample.load(std::memory_order_acquire);
    if (!sd || sd->frames == 0) return;
    const int sampleFrames = sd->frames;
    const int sampleChannels = sd->channels;
    const float* sampleData = sd->samples.data();

    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn()) noteOn(msg.note, msg.velocity, msg.channel, sampleFrames);
        else if (msg.isNoteOff()) noteOff(msg.note, msg.channel);
        else if (msg.isCC() && msg.ccNumber == 123) {
            for (auto& v : m_voices)
                if (v.active) { v.env.gate(false); }
        }
    }

    // Compute loop boundaries in frames
    int loopStartFrame = static_cast<int>(m_loopStart * sampleFrames);
    int loopEndFrame   = static_cast<int>(m_loopEnd * sampleFrames);
    if (loopEndFrame <= loopStartFrame) loopEndFrame = sampleFrames;
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
                if (!m_reverse && pos0 >= sampleFrames) {
                    voice.active = false; break;
                } else if (m_reverse && pos0 < 0) {
                    voice.active = false; break;
                }
            }

            pos0 = std::clamp(pos0, 0, sampleFrames - 1);
            int pos1 = std::clamp(pos0 + (m_reverse ? -1 : 1), 0, sampleFrames - 1);
            float frac = (float)(voice.playPos - pos0);
            if (frac < 0) frac = -frac;

            // Linear interpolation
            float sL = sampleData[pos0 * sampleChannels] * (1.0f - frac)
                     + sampleData[pos1 * sampleChannels] * frac;
            float sR = sL;
            if (sampleChannels > 1) {
                sR = sampleData[pos0 * sampleChannels + 1] * (1.0f - frac)
                   + sampleData[pos1 * sampleChannels + 1] * frac;
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
            if (sampleChannels > 1) {
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

void Sampler::noteOn(uint8_t note, uint16_t vel16, uint8_t ch, int sampleFrames) {
    int slot = findFreeVoice();
    auto& v = m_voices[slot];
    v.active = true;
    v.note = note; v.channel = ch;
    v.velocity = velocityToGain(vel16);
    // Start from loop end if reversed, loop start otherwise
    if (m_reverse)
        v.playPos = static_cast<double>(m_loopEnd * sampleFrames) - 1.0;
    else
        v.playPos = static_cast<double>(m_loopStart * sampleFrames);
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
