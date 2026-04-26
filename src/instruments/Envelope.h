#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "core/Constants.h"

namespace yawn {
namespace instruments {

// ADSR envelope generator — per-sample, per-voice.
// Linear attack, exponential decay toward sustain, exponential release.
class Envelope {
public:
    enum Stage : uint8_t { Idle = 0, Attack, Decay, Sustain, Release };

    void setSampleRate(double sr) { m_sr = (float)sr; recalc(); }

    void setAttack(float s)  { m_aTime = std::max(0.001f, s); recalc(); }
    void setDecay(float s)   { m_dTime = std::max(0.001f, s); recalc(); }
    void setSustain(float l) { m_sLevel = std::clamp(l, 0.0f, 1.0f); }
    void setRelease(float s) { m_rTime = std::max(0.001f, s); recalc(); }

    void setADSR(float a, float d, float s, float r) {
        m_aTime  = std::max(0.001f, a);
        m_dTime  = std::max(0.001f, d);
        m_sLevel = std::clamp(s, 0.0f, 1.0f);
        m_rTime  = std::max(0.001f, r);
        recalc();
    }

    void gate(bool on) {
        if (on)  m_stage = Attack;
        else if (m_stage != Idle) m_stage = Release;
    }

    float process() {
        switch (m_stage) {
            case Attack:
                m_level += m_aInc;
                if (m_level >= 1.0f) { m_level = 1.0f; m_stage = Decay; }
                break;
            case Decay:
                m_level = m_sLevel + (m_level - m_sLevel) * m_dCoeff;
                if (m_level - m_sLevel < 0.0005f) {
                    m_level = m_sLevel;
                    m_stage = Sustain;
                }
                break;
            case Sustain:
                m_level = m_sLevel;
                break;
            case Release:
                m_level *= m_rCoeff;
                if (m_level < 0.0001f) { m_level = 0.0f; m_stage = Idle; }
                break;
            case Idle:
                break;
        }
        return m_level;
    }

    bool  isIdle() const { return m_stage == Idle; }
    float level()  const { return m_level; }
    Stage stage()  const { return m_stage; }
    void  reset()        { m_level = 0.0f; m_stage = Idle; }

private:
    void recalc() {
        m_aInc   = 1.0f / std::max(1.0f, m_aTime * m_sr);
        m_dCoeff = std::exp(-5.0f / std::max(1.0f, m_dTime * m_sr));
        m_rCoeff = std::exp(-5.0f / std::max(1.0f, m_rTime * m_sr));
    }

    float m_sr     = static_cast<float>(kDefaultSampleRate);
    Stage m_stage  = Idle;
    float m_level  = 0.0f;
    float m_aTime  = 0.01f;
    float m_dTime  = 0.1f;
    float m_sLevel = 0.7f;
    float m_rTime  = 0.3f;
    float m_aInc   = 0.0f;
    float m_dCoeff = 0.0f;
    float m_rCoeff = 0.0f;
};

} // namespace instruments
} // namespace yawn
