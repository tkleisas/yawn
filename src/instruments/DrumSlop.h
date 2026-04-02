#pragma once

#include "instruments/Instrument.h"
#include "instruments/Envelope.h"
#include "audio/TransientDetector.h"
#include "audio/AudioBuffer.h"
#include "effects/EffectChain.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// DrumSlop — sample-based drum machine with loop slicing, per-pad ADSR,
// SVF filter, and insert effect chains.  Auto-slices via TransientDetector
// or evenly by beat count.  16 pads mapped to configurable MIDI base note.
class DrumSlop : public Instrument {
public:
    static constexpr int kNumPads   = 16;
    static constexpr int kMaxSlices = 16;

    // ---------- slice modes ----------
    enum SliceMode { Auto = 0, Even = 1, Manual = 2 };

    // ---------- global parameters (exposed via Instrument API) ----------
    enum Params {
        kSliceCount = 0, kSliceMode, kOriginalBPM,
        kBaseNote, kSwing, kVolume,
        kNumGlobalParams,
        // Per-pad params (operate on selectedPad)
        kPadVolume = kNumGlobalParams, kPadPan, kPadPitch, kPadReverse,
        kPadFilterCutoff, kPadFilterReso,
        kPadAttack, kPadDecay, kPadSustain, kPadRelease,
        kNumParams
    };
    static constexpr int kNumPadParams = kNumParams - kNumGlobalParams;

    // ---------- per-pad data ----------
    struct Pad {
        // Slice region (sample frames)
        int64_t sliceStart = 0;
        int64_t sliceEnd   = 0;

        // Per-pad controls
        float volume      = 1.0f;   // 0–2
        float pan          = 0.0f;   // -1..1
        float pitch        = 0.0f;   // semitones -24..24
        bool  reverse      = false;
        float filterCutoff = 20000.0f;
        float filterReso   = 0.0f;
        float startOffset  = 0.0f;   // normalised fine-tune -0.1..0.1

        // ADSR
        float attack  = 0.001f;
        float decay   = 0.05f;
        float sustain = 1.0f;
        float release = 0.05f;

        // Playback state
        bool   playing   = false;
        double playPos   = 0.0;
        double playSpeed = 1.0;
        float  velocity  = 1.0f;
        Envelope env;

        // SVF filter state
        float filterLow  = 0.0f;
        float filterBand = 0.0f;

        // Per-pad effect chain
        effects::EffectChain effects;
    };

    // ================================================================
    //  Instrument interface
    // ================================================================

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate   = sampleRate;
        m_maxBlockSize = maxBlockSize;
        for (auto& p : m_pads) {
            p.env.setSampleRate(sampleRate);
            p.effects.init(sampleRate, maxBlockSize);
        }
        m_padBuffer.resize(maxBlockSize * 2, 0.0f);
        applyDefaults();
    }

    void reset() override {
        for (auto& p : m_pads) {
            p.playing = false;
            p.env.reset();
            p.filterLow = p.filterBand = 0.0f;
            p.effects.reset();
        }
    }

    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override {
        if (m_loopFrames == 0) return;

        // Handle MIDI
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

        // Render each playing pad
        for (int pi = 0; pi < m_sliceCount; ++pi) {
            auto& pad = m_pads[pi];
            if (!pad.playing && pad.env.isIdle()) continue;

            int64_t sliceLen = pad.sliceEnd - pad.sliceStart;
            if (sliceLen <= 0) continue;

            // Render pad into temp buffer
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

                // Boundary check
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

                // Linear interpolation
                float sL = m_loopData[pos0 * m_loopChannels] * (1.0f - frac)
                         + m_loopData[pos1 * m_loopChannels] * frac;
                float sR = sL;
                if (m_loopChannels > 1) {
                    sR = m_loopData[pos0 * m_loopChannels + 1] * (1.0f - frac)
                       + m_loopData[pos1 * m_loopChannels + 1] * frac;
                }

                // SVF filter
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

            // Run per-pad effect chain on the temp buffer
            if (!pad.effects.empty())
                pad.effects.process(m_padBuffer.data(), numFrames, numChannels);

            // Mix into main output (additive)
            for (int s = 0; s < numFrames * numChannels; ++s)
                buffer[s] += m_padBuffer[s];
        }
    }

    const char* name() const override { return "DrumSlop"; }
    const char* id()   const override { return "drumslop"; }

    // ================================================================
    //  Global parameter API (Instrument interface)
    // ================================================================

    int parameterCount() const override { return kNumParams; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo p[kNumParams] = {
            // Global
            {"Slice Count", 2,  16, 8,       "",    false, false},
            {"Slice Mode",  0,   2, 0,       "",    false, false},
            {"Orig BPM",   30, 300, 120,     "BPM", false, false},
            {"Base Note",   0, 112, 36,      "",    false, false},
            {"Swing",       0,   1, 0,       "",    false, false},
            {"Volume",      0,   1, 0.8f,    "",    false, false},
            // Per-pad (isPerVoice = true)
            {"Pad Vol",     0,     2,     1.0f,     "x",  false, true},
            {"Pad Pan",    -1,     1,     0.0f,     "%",  false, true},
            {"Pad Pitch", -24,    24,     0.0f,     "st", false, true},
            {"Pad Rev",     0,     1,     0.0f,     "",  true,  true},
            {"Pad Cutoff", 20, 20000, 20000.0f,     "Hz", false, true},
            {"Pad Reso",    0,     1,     0.0f,     "",  false, true},
            {"Pad Atk",     0.001f, 2.0f, 0.001f,  "s", false, true},
            {"Pad Dec",     0.001f, 2.0f, 0.05f,   "s", false, true},
            {"Pad Sus",     0,     1,     1.0f,     "",  false, true},
            {"Pad Rel",     0.001f, 2.0f, 0.05f,   "s", false, true},
        };
        return p[std::clamp(index, 0, kNumParams - 1)];
    }

    float getParameter(int index) const override {
        switch (index) {
            case kSliceCount:  return (float)m_sliceCount;
            case kSliceMode:   return (float)m_sliceMode;
            case kOriginalBPM: return m_originalBPM;
            case kBaseNote:    return (float)m_baseNote;
            case kSwing:       return m_swing;
            case kVolume:      return m_volume;
            // Per-pad (selected pad)
            case kPadVolume:       return m_pads[m_selectedPad].volume;
            case kPadPan:          return m_pads[m_selectedPad].pan;
            case kPadPitch:        return m_pads[m_selectedPad].pitch;
            case kPadReverse:      return m_pads[m_selectedPad].reverse ? 1.0f : 0.0f;
            case kPadFilterCutoff: return m_pads[m_selectedPad].filterCutoff;
            case kPadFilterReso:   return m_pads[m_selectedPad].filterReso;
            case kPadAttack:       return m_pads[m_selectedPad].attack;
            case kPadDecay:        return m_pads[m_selectedPad].decay;
            case kPadSustain:      return m_pads[m_selectedPad].sustain;
            case kPadRelease:      return m_pads[m_selectedPad].release;
            default:           return 0.0f;
        }
    }

    void setParameter(int index, float value) override {
        switch (index) {
            case kSliceCount:
                m_sliceCount = std::clamp((int)value, 2, 16);
                if (m_loopFrames > 0) sliceLoop();
                break;
            case kSliceMode:
                m_sliceMode = (SliceMode)std::clamp((int)value, 0, 2);
                if (m_loopFrames > 0) sliceLoop();
                break;
            case kOriginalBPM:
                m_originalBPM = std::clamp(value, 30.0f, 300.0f);
                break;
            case kBaseNote:
                m_baseNote = std::clamp((int)value, 0, 112);
                break;
            case kSwing:
                m_swing = std::clamp(value, 0.0f, 1.0f);
                break;
            case kVolume:
                m_volume = std::clamp(value, 0.0f, 1.0f);
                break;
            // Per-pad (selected pad)
            case kPadVolume:       setPadVolume(m_selectedPad, value); break;
            case kPadPan:          setPadPan(m_selectedPad, value); break;
            case kPadPitch:        setPadPitch(m_selectedPad, value); break;
            case kPadReverse:      setPadReverse(m_selectedPad, value > 0.5f); break;
            case kPadFilterCutoff: setPadFilterCutoff(m_selectedPad, value); break;
            case kPadFilterReso:   setPadFilterReso(m_selectedPad, value); break;
            case kPadAttack:       setPadAttack(m_selectedPad, value); break;
            case kPadDecay:        setPadDecay(m_selectedPad, value); break;
            case kPadSustain:      setPadSustain(m_selectedPad, value); break;
            case kPadRelease:      setPadRelease(m_selectedPad, value); break;
        }
    }

    // ================================================================
    //  Per-pad parameter accessors
    // ================================================================

    Pad&       pad(int i)       { return m_pads[std::clamp(i, 0, kNumPads - 1)]; }
    const Pad& pad(int i) const { return m_pads[std::clamp(i, 0, kNumPads - 1)]; }

    void setPadVolume(int i, float v) {
        if (i >= 0 && i < kNumPads) m_pads[i].volume = std::clamp(v, 0.0f, 2.0f);
    }
    void setPadPan(int i, float v) {
        if (i >= 0 && i < kNumPads) m_pads[i].pan = std::clamp(v, -1.0f, 1.0f);
    }
    void setPadPitch(int i, float v) {
        if (i >= 0 && i < kNumPads) m_pads[i].pitch = std::clamp(v, -24.0f, 24.0f);
    }
    void setPadReverse(int i, bool v) {
        if (i >= 0 && i < kNumPads) m_pads[i].reverse = v;
    }
    void setPadFilterCutoff(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].filterCutoff = std::clamp(v, 20.0f, 20000.0f);
    }
    void setPadFilterReso(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].filterReso = std::clamp(v, 0.0f, 1.0f);
    }
    void setPadStartOffset(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].startOffset = std::clamp(v, -0.1f, 0.1f);
    }
    void setPadAttack(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].attack = std::max(0.001f, v);
    }
    void setPadDecay(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].decay = std::max(0.001f, v);
    }
    void setPadSustain(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].sustain = std::clamp(v, 0.0f, 1.0f);
    }
    void setPadRelease(int i, float v) {
        if (i >= 0 && i < kNumPads)
            m_pads[i].release = std::max(0.001f, v);
    }

    // ================================================================
    //  Pad selection (for UI — which pad's params are being edited)
    // ================================================================

    int  selectedPad() const      { return m_selectedPad; }
    void setSelectedPad(int idx)  { m_selectedPad = std::clamp(idx, 0, kNumPads - 1); }

    // Returns true if a pad is currently sounding
    bool isPadPlaying(int i) const {
        if (i < 0 || i >= kNumPads) return false;
        return m_pads[i].playing || !m_pads[i].env.isIdle();
    }

    // ================================================================
    //  Loop loading & slicing
    // ================================================================

    // Load interleaved audio data as the loop source
    void loadLoop(const float* data, int numFrames, int numChannels) {
        if (!data || numFrames <= 0) return;
        m_loopData.assign(data, data + numFrames * numChannels);
        m_loopFrames   = numFrames;
        m_loopChannels = numChannels;
        sliceLoop();
    }

    bool hasLoop() const { return m_loopFrames > 0; }
    const float* loopData()     const { return m_loopData.data(); }
    int          loopFrames()   const { return m_loopFrames; }
    int          loopChannels() const { return m_loopChannels; }

    // Detected/computed slice boundaries (for UI display)
    int sliceCount() const { return m_sliceCount; }
    int64_t sliceBoundary(int idx) const {
        if (idx < 0 || idx > m_sliceCount) return 0;
        if (idx < m_sliceCount) return m_pads[idx].sliceStart;
        return m_loopFrames; // one past last
    }

    // Re-slice the loop based on current mode & count
    void sliceLoop() {
        if (m_loopFrames == 0) return;

        if (m_sliceMode == Auto) {
            sliceAuto();
        } else {
            sliceEven();
        }
    }

    // Set a manual slice boundary (for Manual mode editing)
    void setSliceBoundary(int sliceIdx, int64_t frame) {
        if (sliceIdx < 0 || sliceIdx >= m_sliceCount) return;
        frame = std::clamp(frame, (int64_t)0, (int64_t)m_loopFrames);
        m_pads[sliceIdx].sliceStart = frame;
        if (sliceIdx > 0)
            m_pads[sliceIdx - 1].sliceEnd = frame;
    }

private:
    // ---------- pad triggering ----------

    void triggerPad(int idx, uint16_t vel16) {
        auto& pad = m_pads[idx];
        int64_t sliceLen = pad.sliceEnd - pad.sliceStart;
        if (sliceLen <= 0) return;

        pad.playing   = true;
        pad.velocity  = velocityToGain(vel16);
        pad.playSpeed = std::pow(2.0, pad.pitch / 12.0);

        // Apply start offset (normalised fraction of slice length)
        int64_t offset = (int64_t)(pad.startOffset * sliceLen);

        if (pad.reverse)
            pad.playPos = (double)(pad.sliceEnd - 1 + offset);
        else
            pad.playPos = (double)(pad.sliceStart + offset);

        pad.filterLow = pad.filterBand = 0.0f;
        pad.env.setADSR(pad.attack, pad.decay, pad.sustain, pad.release);
        pad.env.gate(true);
    }

    void releasePad(int idx) {
        m_pads[idx].env.gate(false);
    }

    // ---------- slicing algorithms ----------

    void sliceEven() {
        int64_t sliceLen = m_loopFrames / m_sliceCount;
        for (int i = 0; i < m_sliceCount; ++i) {
            m_pads[i].sliceStart = i * sliceLen;
            m_pads[i].sliceEnd   = (i == m_sliceCount - 1)
                                   ? m_loopFrames
                                   : (i + 1) * sliceLen;
        }
    }

    void sliceAuto() {
        // Build a temporary non-interleaved AudioBuffer for TransientDetector
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
            // Not enough transients detected — fall back to even slicing
            sliceEven();
            return;
        }

        // Use first (sliceCount - 1) transients as boundaries
        // Pad 0 always starts at frame 0
        m_pads[0].sliceStart = 0;
        for (int i = 1; i < m_sliceCount; ++i) {
            int64_t boundary = (i - 1 < (int)transients.size())
                               ? transients[i - 1] : m_loopFrames;
            m_pads[i].sliceStart     = boundary;
            m_pads[i - 1].sliceEnd   = boundary;
        }
        m_pads[m_sliceCount - 1].sliceEnd = m_loopFrames;
    }

    void applyDefaults() {
        for (int i = 0; i < kNumParams; ++i)
            setParameter(i, parameterInfo(i).defaultValue);
    }

    // ---------- state ----------
    Pad m_pads[kNumPads];
    int m_selectedPad = 0;

    // Loop sample data (interleaved)
    std::vector<float> m_loopData;
    int m_loopFrames    = 0;
    int m_loopChannels  = 1;

    // Global params
    int       m_sliceCount  = 8;
    SliceMode m_sliceMode   = Auto;
    float     m_originalBPM = 120.0f;
    int       m_baseNote    = 36;
    float     m_swing       = 0.0f;
    float     m_volume      = 0.8f;

    // Scratch buffer for per-pad rendering
    std::vector<float> m_padBuffer;
};

} // namespace instruments
} // namespace yawn
