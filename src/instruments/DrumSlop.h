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

class DrumSlop : public Instrument {
public:
    static constexpr int kNumPads   = 16;
    static constexpr int kMaxSlices = 16;

    enum SliceMode { Auto = 0, Even = 1, Manual = 2 };

    enum Params {
        kSliceCount = 0, kSliceMode, kOriginalBPM,
        kBaseNote, kSwing, kVolume,
        kNumGlobalParams,
        kPadVolume = kNumGlobalParams, kPadPan, kPadPitch, kPadReverse,
        kPadFilterCutoff, kPadFilterReso,
        kPadAttack, kPadDecay, kPadSustain, kPadRelease,
        kNumParams
    };
    static constexpr int kNumPadParams = kNumParams - kNumGlobalParams;

    struct Pad {
        int64_t sliceStart = 0;
        int64_t sliceEnd   = 0;
        float volume      = 1.0f;
        float pan          = 0.0f;
        float pitch        = 0.0f;
        bool  reverse      = false;
        float filterCutoff = 20000.0f;
        float filterReso   = 0.0f;
        float startOffset  = 0.0f;
        float attack  = 0.001f;
        float decay   = 0.05f;
        float sustain = 1.0f;
        float release = 0.05f;
        bool   playing   = false;
        double playPos   = 0.0;
        double playSpeed = 1.0;
        float  velocity  = 1.0f;
        Envelope env;
        float filterLow  = 0.0f;
        float filterBand = 0.0f;
        effects::EffectChain effects;
    };

    void init(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numFrames, int numChannels,
                 const midi::MidiBuffer& midi) override;

    const char* name() const override { return "DrumSlop"; }
    const char* id()   const override { return "drumslop"; }

    int  parameterCount() const override { return kNumParams; }
    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static constexpr const char* kSliceModeLabels[] = {"Equal", "Transient", "Beat"};
        static const InstrumentParameterInfo p[kNumParams] = {
            {"Slice Count", 2,  16, 8,       "",    false, false, WidgetHint::StepSelector},
            {"Slice Mode",  0,   2, 0,       "",    false, false, WidgetHint::StepSelector, kSliceModeLabels, 3},
            {"Orig BPM",   30, 300, 120,     "BPM", false, false, WidgetHint::DentedKnob},
            {"Base Note",   0, 112, 36,      "",    false, false, WidgetHint::StepSelector},
            {"Swing",       0,   1, 0,       "",    false, false},
            {"Volume",      0,   1, 0.8f,    "",    false, false, WidgetHint::DentedKnob},
            {"Pad Vol",     0,     2,     1.0f,     "x",  false, true, WidgetHint::DentedKnob},
            {"Pad Pan",    -1,     1,     0.0f,     "%",  false, true, WidgetHint::DentedKnob},
            {"Pad Pitch", -24,    24,     0.0f,     "st", false, true, WidgetHint::DentedKnob},
            {"Pad Rev",     0,     1,     0.0f,     "",  true,  true},
            {"Pad Cutoff", 20, 20000, 20000.0f,     "Hz", false, true},
            {"Pad Reso",    0,     1,     0.0f,     "",  false, true},
            {"Pad Atk",     0.001f, 2.0f, 0.001f,  "s", false, true},
            {"Pad Dec",     0.001f, 2.0f, 0.05f,   "s", false, true},
            {"Pad Sus",     0,     1,     1.0f,     "",  false, true, WidgetHint::DentedKnob},
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
            case kOriginalBPM: m_originalBPM = std::clamp(value, 30.0f, 300.0f); break;
            case kBaseNote:    m_baseNote = std::clamp((int)value, 0, 112); break;
            case kSwing:       m_swing = std::clamp(value, 0.0f, 1.0f); break;
            case kVolume:      m_volume = std::clamp(value, 0.0f, 1.0f); break;
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

    Pad&       pad(int i)       { return m_pads[std::clamp(i, 0, kNumPads - 1)]; }
    const Pad& pad(int i) const { return m_pads[std::clamp(i, 0, kNumPads - 1)]; }

    void setPadVolume(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].volume = std::clamp(v, 0.0f, 2.0f); }
    void setPadPan(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].pan = std::clamp(v, -1.0f, 1.0f); }
    void setPadPitch(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].pitch = std::clamp(v, -24.0f, 24.0f); }
    void setPadReverse(int i, bool v) { if (i >= 0 && i < kNumPads) m_pads[i].reverse = v; }
    void setPadFilterCutoff(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].filterCutoff = std::clamp(v, 20.0f, 20000.0f); }
    void setPadFilterReso(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].filterReso = std::clamp(v, 0.0f, 1.0f); }
    void setPadStartOffset(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].startOffset = std::clamp(v, -0.1f, 0.1f); }
    void setPadAttack(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].attack = std::max(0.001f, v); }
    void setPadDecay(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].decay = std::max(0.001f, v); }
    void setPadSustain(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].sustain = std::clamp(v, 0.0f, 1.0f); }
    void setPadRelease(int i, float v) { if (i >= 0 && i < kNumPads) m_pads[i].release = std::max(0.001f, v); }

    int  selectedPad() const      { return m_selectedPad; }
    void setSelectedPad(int idx)  { m_selectedPad = std::clamp(idx, 0, kNumPads - 1); }
    bool isPadPlaying(int i) const { return i >= 0 && i < kNumPads && (m_pads[i].playing || !m_pads[i].env.isIdle()); }

    void loadLoop(const float* data, int numFrames, int numChannels);
    void clearLoop();
    bool hasLoop() const { return m_loopFrames > 0; }
    const float* loopData()     const { return m_loopData.data(); }
    int          loopFrames()   const { return m_loopFrames; }
    int          loopChannels() const { return m_loopChannels; }
    int sliceCount() const { return m_sliceCount; }
    int64_t sliceBoundary(int idx) const;
    void sliceLoop();
    void setSliceBoundary(int sliceIdx, int64_t frame);

private:
    void triggerPad(int idx, uint16_t vel16);
    void releasePad(int idx);
    void sliceEven();
    void sliceAuto();
    void applyDefaults();

    Pad m_pads[kNumPads];
    int m_selectedPad = 0;
    std::vector<float> m_loopData;
    int m_loopFrames   = 0;
    int m_loopChannels = 1;
    int       m_sliceCount  = 8;
    SliceMode m_sliceMode   = Auto;
    float     m_originalBPM = 120.0f;
    int       m_baseNote    = 36;
    float     m_swing       = 0.0f;
    float     m_volume      = 0.8f;
    std::vector<float> m_padBuffer;
};

} // namespace instruments
} // namespace yawn
