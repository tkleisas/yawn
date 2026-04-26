#include "instruments/GranularSynth.h"

namespace yawn::instruments {

void GranularSynth::init(double sampleRate, int maxBlockSize) {
    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    for (auto& v : m_voices) v = Voice{};
    for (auto& g : m_grains) g = Grain{};
    m_scRingSize = static_cast<int>(sampleRate * kSidechainRingSeconds);
    m_scRing.resize(m_scRingSize, 0.0f);
    m_scWritePos = 0;
    m_scFilled = 0;
    resetParams();
}

void GranularSynth::reset() {
    for (auto& v : m_voices) v.active = false;
    for (auto& g : m_grains) g.active = false;
    m_grainTimer = 0.0;
    m_scanPos = 0.0;
    m_rngState = 12345u;
}

void GranularSynth::loadSample(const float* data, int numFrames,
                                int numChannels, int rootNote) {
    m_sampleData.assign(data, data + numFrames * numChannels);
    m_sampleFrames = numFrames;
    m_sampleChannels = numChannels;
    m_rootNote = rootNote;
}

void GranularSynth::clearSample() {
    m_sampleData.clear();
    m_sampleFrames = 0;
    m_sampleChannels = 1;
}

void GranularSynth::process(float* buffer, int numFrames, int numChannels,
                             const midi::MidiBuffer& midi) {
    if (m_bypassed) return;

    const bool useSidechain = m_params[kSidechainInput] > 0.5f && m_sidechainBuffer;
    if (m_sidechainBuffer && m_scRingSize > 0) {
        for (int f = 0; f < numFrames; ++f) {
            float sL = m_sidechainBuffer[f * numChannels];
            float sR = (numChannels > 1) ? m_sidechainBuffer[f * numChannels + 1] : sL;
            m_scRing[m_scWritePos] = (sL + sR) * 0.5f;
            m_scWritePos = (m_scWritePos + 1) % m_scRingSize;
            if (m_scFilled < m_scRingSize) m_scFilled++;
        }
    }

    if (!useSidechain && m_sampleFrames == 0) return;

    const float position    = m_params[kPosition];
    const float grainSizeMs = m_params[kGrainSize];
    const float density     = m_params[kDensity];
    const float spread      = m_params[kSpread];
    const float pitchSt     = m_params[kPitch];
    const float sprayMs     = m_params[kSpray];
    const int   shape       = static_cast<int>(m_params[kShape]);
    const float scan        = m_params[kScan];
    const float filterCut   = cutoffNormToHz(m_params[kFilterCutoff]);
    const float filterReso  = m_params[kFilterReso];
    const float stereoW     = m_params[kStereoWidth];
    const bool  reverse     = m_params[kReverse] > 0.5f;
    const float jitter      = m_params[kJitter];
    const float volume      = m_params[kVolume];

    const int grainSizeSamples = std::max(1, static_cast<int>(grainSizeMs * 0.001 * m_sampleRate));
    const double grainInterval = (density > 0.01) ? (m_sampleRate / density) : m_sampleRate;

    for (int i = 0; i < midi.count(); ++i) {
        const auto& ev = midi[i];
        if (ev.isNoteOn()) {
            noteOn(ev.note, ev.velocity, ev.channel);
        } else if (ev.isNoteOff()) {
            noteOff(ev.note, ev.channel);
        } else if (ev.isCC() && ev.ccNumber == 123) {
            for (auto& v : m_voices) {
                if (v.active) { v.ampEnv.gate(false); }
            }
        }
    }

    int activeVoices = 0;
    for (auto& v : m_voices)
        if (v.active) ++activeVoices;
    if (activeVoices == 0) return;

    m_scanPos += scan * (1.0 / m_sampleRate) * 0.5;
    m_scanPos -= std::floor(m_scanPos);

    const float cutNorm = std::clamp(filterCut / static_cast<float>(m_sampleRate), 0.001f, 0.499f);
    const float g = std::tan(3.14159265f * cutNorm);
    const float k = 2.0f - 2.0f * std::clamp(filterReso, 0.0f, 0.95f);
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    for (int s = 0; s < numFrames; ++s) {
        m_grainTimer += 1.0;
        if (m_grainTimer >= grainInterval) {
            m_grainTimer -= grainInterval;
            for (auto& v : m_voices) {
                if (!v.active || v.ampEnv.isIdle()) continue;
                spawnGrain(v, position, spread, sprayMs, pitchSt, jitter,
                           grainSizeSamples, shape, reverse, stereoW,
                           useSidechain);
            }
        }

        float mixL = 0.0f, mixR = 0.0f;
        for (auto& g : m_grains) {
            if (!g.active) continue;

            float t = static_cast<float>(g.pos) / static_cast<float>(g.length);
            float window = grainWindow(t, g.shape);

            float samp = readSample(g.readPos, g.fromSidechain);
            float val = samp * window * g.velocity;

            mixL += val * g.panL;
            mixR += val * g.panR;

            g.readPos += g.speed;
            int wrapLen = g.fromSidechain ? m_scFilled : m_sampleFrames;
            if (wrapLen > 0) {
                if (g.readPos < 0) g.readPos += wrapLen;
                if (g.readPos >= wrapLen) g.readPos -= wrapLen;
            }
            g.pos++;
            if (g.pos >= g.length) g.active = false;
        }

        float envSum = 0.0f;
        for (auto& v : m_voices) {
            if (!v.active) continue;
            float e = v.ampEnv.process();
            envSum += e;
            if (v.ampEnv.isIdle()) v.active = false;
        }
        float envGain = (activeVoices > 0) ? (envSum / activeVoices) : 0.0f;
        mixL *= envGain;
        mixR *= envGain;

        if (filterCut < 19999.0f) {
            float v3 = mixL - m_filterIcL[1];
            float v1 = a1 * m_filterIcL[0] + a2 * v3;
            float v2 = m_filterIcL[1] + a2 * m_filterIcL[0] + a3 * v3;
            m_filterIcL[0] = 2.0f * v1 - m_filterIcL[0];
            m_filterIcL[1] = 2.0f * v2 - m_filterIcL[1];
            mixL = v2;

            v3 = mixR - m_filterIcR[1];
            v1 = a1 * m_filterIcR[0] + a2 * v3;
            v2 = m_filterIcR[1] + a2 * m_filterIcR[0] + a3 * v3;
            m_filterIcR[0] = 2.0f * v1 - m_filterIcR[0];
            m_filterIcR[1] = 2.0f * v2 - m_filterIcR[1];
            mixR = v2;
        }

        buffer[s * numChannels]     += mixL * volume;
        if (numChannels > 1)
            buffer[s * numChannels + 1] += mixR * volume;
    }

    activeVoices = 0;
    for (auto& v : m_voices)
        if (v.active) ++activeVoices;
}

float GranularSynth::grainWindow(float t, int shape) const {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
    case 0:
        return 0.5f * (1.0f - std::cos(6.28318530f * t));
    case 1:
        return (t < 0.5f) ? (2.0f * t) : (2.0f * (1.0f - t));
    case 2: {
        float x = (t - 0.5f) * 4.0f;
        return std::exp(-0.5f * x * x);
    }
    case 3: {
        constexpr float alpha = 0.5f;
        if (t < alpha * 0.5f)
            return 0.5f * (1.0f - std::cos(6.28318530f * t / alpha));
        else if (t > 1.0f - alpha * 0.5f)
            return 0.5f * (1.0f - std::cos(6.28318530f * (1.0f - t) / alpha));
        return 1.0f;
    }
    default:
        return 0.5f * (1.0f - std::cos(6.28318530f * t));
    }
}

float GranularSynth::readSample(double pos, bool fromSidechain) const {
    if (fromSidechain) {
        if (m_scFilled == 0) return 0.0f;
        int len = m_scFilled;
        int idx0 = static_cast<int>(pos);
        int idx1 = idx0 + 1;
        idx0 = ((idx0 % len) + len) % len;
        idx1 = ((idx1 % len) + len) % len;
        int base = (m_scWritePos - m_scFilled + m_scRingSize) % m_scRingSize;
        float s0 = m_scRing[(base + idx0) % m_scRingSize];
        float s1 = m_scRing[(base + idx1) % m_scRingSize];
        float frac = static_cast<float>(pos - std::floor(pos));
        return s0 + frac * (s1 - s0);
    }

    if (m_sampleFrames == 0) return 0.0f;
    int idx0 = static_cast<int>(pos);
    int idx1 = idx0 + 1;
    if (idx0 < 0) idx0 += m_sampleFrames;
    if (idx1 >= m_sampleFrames) idx1 -= m_sampleFrames;
    float frac = static_cast<float>(pos - std::floor(pos));

    float s0 = 0.0f, s1 = 0.0f;
    for (int ch = 0; ch < m_sampleChannels; ++ch) {
        s0 += m_sampleData[static_cast<size_t>(idx0) * m_sampleChannels + ch];
        s1 += m_sampleData[static_cast<size_t>(idx1) * m_sampleChannels + ch];
    }
    s0 /= m_sampleChannels;
    s1 /= m_sampleChannels;
    return s0 + frac * (s1 - s0);
}

float GranularSynth::randFloat() {
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return static_cast<float>(m_rngState & 0x7FFFFF) / 8388607.0f;
}

void GranularSynth::spawnGrain(Voice& v, float position, float spread,
                               float sprayMs, float pitchSt, float jitter,
                               int grainSizeSamples, int shape, bool reverse,
                               float stereoWidth, bool fromSidechain) {
    Grain* slot = nullptr;
    for (auto& g : m_grains) {
        if (!g.active) { slot = &g; break; }
    }
    if (!slot) {
        int minRemain = INT_MAX;
        for (auto& g : m_grains) {
            int remain = g.length - g.pos;
            if (remain < minRemain) { minRemain = remain; slot = &g; }
        }
    }
    if (!slot) return;

    int effLen = fromSidechain ? m_scFilled : m_sampleFrames;
    if (effLen <= 0) return;

    float pos = position + static_cast<float>(m_scanPos);
    pos -= std::floor(pos);
    if (spread > 0.0f)
        pos += (randFloat() - 0.5f) * spread;
    pos -= std::floor(pos);
    if (pos < 0.0f) pos += 1.0f;

    double readOffset = 0.0;
    if (sprayMs > 0.0f)
        readOffset = (randFloat() - 0.5f) * sprayMs * 0.001 * m_sampleRate;

    double startPos = pos * effLen + readOffset;
    while (startPos < 0) startPos += effLen;
    while (startPos >= effLen) startPos -= effLen;

    float noteShift = static_cast<float>((int)v.note - m_rootNote);
    float totalPitch = noteShift + pitchSt;
    if (jitter > 0.0f)
        totalPitch += (randFloat() - 0.5f) * 2.0f * jitter;
    double speed = std::pow(2.0, totalPitch / 12.0);
    if (reverse) speed = -speed;

    float pan = (stereoWidth > 0.0f) ? (randFloat() - 0.5f) * stereoWidth : 0.0f;
    float panL = std::cos((pan + 0.5f) * 1.5707963f);
    float panR = std::sin((pan + 0.5f) * 1.5707963f);

    slot->active = true;
    slot->readPos = startPos;
    slot->speed = speed;
    slot->pos = 0;
    slot->length = grainSizeSamples;
    slot->shape = shape;
    slot->velocity = v.velocity;
    slot->panL = panL;
    slot->panR = panR;
    slot->fromSidechain = fromSidechain;
}

void GranularSynth::noteOn(uint8_t note, uint16_t vel16, uint8_t ch) {
    Voice* slot = nullptr;
    for (auto& v : m_voices) {
        if (!v.active) { slot = &v; break; }
    }
    if (!slot) {
        int64_t oldest = INT64_MAX;
        for (auto& v : m_voices) {
            if (v.startOrder < oldest) { oldest = v.startOrder; slot = &v; }
        }
    }
    if (!slot) return;

    slot->active = true;
    slot->note = note;
    slot->channel = ch;
    slot->velocity = velocityToGain(vel16);
    slot->startOrder = m_voiceCounter++;

    float atkSec = m_params[kAttack] * 0.001f;
    float relSec = m_params[kRelease] * 0.001f;
    slot->ampEnv.setSampleRate(m_sampleRate);
    slot->ampEnv.setADSR(atkSec, 0.001f, 1.0f, relSec);
    slot->ampEnv.gate(true);
}

void GranularSynth::noteOff(uint8_t note, uint8_t ch) {
    for (auto& v : m_voices) {
        if (v.active && v.note == note && v.channel == ch) {
            v.ampEnv.gate(false);
            break;
        }
    }
}

void GranularSynth::resetParams() {
    for (int i = 0; i < kParamCount; ++i)
        m_params[i] = parameterInfo(i).defaultValue;
    m_grainTimer = 0.0;
    m_scanPos = 0.0;
    m_rngState = 12345u;
    m_voiceCounter = 0;
    m_filterIcL[0] = m_filterIcL[1] = 0.0f;
    m_filterIcR[0] = m_filterIcR[1] = 0.0f;
}

} // namespace yawn::instruments
