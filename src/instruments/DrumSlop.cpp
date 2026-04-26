#include "instruments/DrumSlop.h"

namespace yawn {
namespace instruments {

void DrumSlop::init(double sampleRate, int maxBlockSize) {
    m_sampleRate   = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& p : m_pads) {
        p.env.setSampleRate(sampleRate);
        p.effects.init(sampleRate, maxBlockSize);
    }
    m_padBuffer.resize(maxBlockSize * 2, 0.0f);
    applyDefaults();
}

void DrumSlop::reset() {
    for (auto& p : m_pads) {
        p.playing = false;
        p.env.reset();
        p.filterLow = p.filterBand = 0.0f;
        p.effects.reset();
    }
}

void DrumSlop::process(float* buffer, int numFrames, int numChannels,
                        const midi::MidiBuffer& midi) {
    if (m_loopFrames == 0) return;

    for (int i = 0; i < midi.count(); ++i) {
        const auto& msg = midi[i];
        if (msg.isNoteOn()) {
            int padIdx = (msg.note & 0x7F) - m_baseNote;
            if (padIdx >= 0 && padIdx < kNumPads && padIdx < m_sliceCount) {
                m_selectedPad = padIdx;
                triggerPad(padIdx, msg.velocity);
            }
        } else if (msg.isNoteOff()) {
            int padIdx = (msg.note & 0x7F) - m_baseNote;
            if (padIdx >= 0 && padIdx < kNumPads && padIdx < m_sliceCount)
                releasePad(padIdx);
        }
    }

    for (int pi = 0; pi < m_sliceCount; ++pi) {
        auto& pad = m_pads[pi];
        if (!pad.playing && pad.env.isIdle()) continue;

        int64_t sliceLen = pad.sliceEnd - pad.sliceStart;
        if (sliceLen <= 0) continue;

        std::fill(m_padBuffer.begin(),
                  m_padBuffer.begin() + numFrames * numChannels, 0.0f);

        float angle = (pad.pan + 1.0f) * 0.25f * (float)M_PI;
        float gL = std::cos(angle);
        float gR = std::sin(angle);

        float f = std::min(2.0f * pad.filterCutoff / (float)m_sampleRate, 0.99f);
        float q = 1.0f - pad.filterReso * 0.98f;

        for (int i = 0; i < numFrames; ++i) {
            float env = pad.env.process();
            if (pad.env.isIdle()) { pad.playing = false; break; }

            int64_t pos0 = (int64_t)pad.playPos;

            if (!pad.reverse && pos0 >= pad.sliceEnd) {
                pad.playing = false;
                pad.env.gate(false);
                continue;
            }
            if (pad.reverse && pos0 < pad.sliceStart) {
                pad.playing = false;
                pad.env.gate(false);
                continue;
            }

            pos0 = std::clamp(pos0, (int64_t)0, (int64_t)(m_loopFrames - 1));
            int64_t pos1 = std::clamp(pos0 + (pad.reverse ? -1 : 1),
                                      (int64_t)0, (int64_t)(m_loopFrames - 1));
            float frac = (float)(pad.playPos - (double)pos0);
            if (frac < 0) frac = -frac;

            float sL = m_loopData[pos0 * m_loopChannels] * (1.0f - frac)
                     + m_loopData[pos1 * m_loopChannels] * frac;
            float sR = sL;
            if (m_loopChannels > 1) {
                sR = m_loopData[pos0 * m_loopChannels + 1] * (1.0f - frac)
                   + m_loopData[pos1 * m_loopChannels + 1] * frac;
            }

            float highL = sL - pad.filterLow - q * pad.filterBand;
            pad.filterBand += f * highL;
            pad.filterLow  += f * pad.filterBand;
            float filteredL = pad.filterLow;
            if (m_loopChannels > 1) {
                float ratio = (std::abs(sL) > 0.0001f)
                              ? filteredL / sL : 1.0f;
                sR *= ratio;
            } else {
                sR = filteredL;
            }
            sL = filteredL;

            float gain = env * pad.velocity * pad.volume * m_volume;
            m_padBuffer[i * numChannels + 0] = sL * gain * gL;
            if (numChannels > 1)
                m_padBuffer[i * numChannels + 1] = sR * gain * gR;

            pad.playPos += pad.reverse ? -pad.playSpeed : pad.playSpeed;
        }

        if (!pad.effects.empty())
            pad.effects.process(m_padBuffer.data(), numFrames, numChannels);

        for (int s = 0; s < numFrames * numChannels; ++s)
            buffer[s] += m_padBuffer[s];
    }
}

void DrumSlop::loadLoop(const float* data, int numFrames, int numChannels) {
    if (!data || numFrames <= 0) return;
    m_loopData.assign(data, data + numFrames * numChannels);
    m_loopFrames   = numFrames;
    m_loopChannels = numChannels;
    sliceLoop();
}

void DrumSlop::clearLoop() {
    m_loopData.clear();
    m_loopFrames = 0;
    m_loopChannels = 1;
}

int64_t DrumSlop::sliceBoundary(int idx) const {
    if (idx < 0 || idx > m_sliceCount) return 0;
    if (idx < m_sliceCount) return m_pads[idx].sliceStart;
    return m_loopFrames;
}

void DrumSlop::sliceLoop() {
    if (m_loopFrames == 0) return;
    if (m_sliceMode == Auto) sliceAuto();
    else sliceEven();
}

void DrumSlop::setSliceBoundary(int sliceIdx, int64_t frame) {
    if (sliceIdx < 0 || sliceIdx >= m_sliceCount) return;
    frame = std::clamp(frame, (int64_t)0, (int64_t)m_loopFrames);
    m_pads[sliceIdx].sliceStart = frame;
    if (sliceIdx > 0)
        m_pads[sliceIdx - 1].sliceEnd = frame;
}

void DrumSlop::triggerPad(int idx, uint16_t vel16) {
    auto& pad = m_pads[idx];
    int64_t sliceLen = pad.sliceEnd - pad.sliceStart;
    if (sliceLen <= 0) return;

    pad.playing   = true;
    pad.velocity  = velocityToGain(vel16);
    pad.playSpeed = std::pow(2.0, pad.pitch / 12.0);

    int64_t offset = (int64_t)(pad.startOffset * sliceLen);

    if (pad.reverse)
        pad.playPos = (double)(pad.sliceEnd - 1 + offset);
    else
        pad.playPos = (double)(pad.sliceStart + offset);

    pad.filterLow = pad.filterBand = 0.0f;
    pad.env.setADSR(pad.attack, pad.decay, pad.sustain, pad.release);
    pad.env.gate(true);
}

void DrumSlop::releasePad(int idx) {
    m_pads[idx].env.gate(false);
}

void DrumSlop::sliceEven() {
    int64_t sliceLen = m_loopFrames / m_sliceCount;
    for (int i = 0; i < m_sliceCount; ++i) {
        m_pads[i].sliceStart = i * sliceLen;
        m_pads[i].sliceEnd   = (i == m_sliceCount - 1)
                               ? m_loopFrames
                               : (i + 1) * sliceLen;
    }
}

void DrumSlop::sliceAuto() {
    audio::AudioBuffer abuf(m_loopChannels, m_loopFrames);
    for (int ch = 0; ch < m_loopChannels; ++ch) {
        float* dst = abuf.channelData(ch);
        for (int f = 0; f < m_loopFrames; ++f)
            dst[f] = m_loopData[f * m_loopChannels + ch];
    }

    audio::TransientDetector::Config cfg;
    cfg.sampleRate = m_sampleRate;
    cfg.sensitivity = 0.5f;
    auto transients = audio::TransientDetector::detect(abuf, cfg);

    if ((int)transients.size() < m_sliceCount - 1) {
        sliceEven();
        return;
    }

    m_pads[0].sliceStart = 0;
    for (int i = 1; i < m_sliceCount; ++i) {
        int64_t boundary = (i - 1 < (int)transients.size())
                           ? transients[i - 1] : m_loopFrames;
        m_pads[i].sliceStart     = boundary;
        m_pads[i - 1].sliceEnd   = boundary;
    }
    m_pads[m_sliceCount - 1].sliceEnd = m_loopFrames;
}

void DrumSlop::applyDefaults() {
    for (int i = 0; i < kNumParams; ++i)
        setParameter(i, parameterInfo(i).defaultValue);
}

} // namespace instruments
} // namespace yawn
