#pragma once

#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace instruments {

// Band-limited oscillator with polyBLEP anti-aliasing.
class Oscillator {
public:
    enum Waveform : uint8_t { Sine = 0, Saw, Square, Triangle, Noise, kNumWaveforms };

    void setSampleRate(double sr) { m_sr = sr; }
    void setFrequency(double hz)  { m_inc = hz / m_sr; }
    void setWaveform(Waveform w)  { m_wave = w; }

    float process() {
        float out = 0.0f;
        double t  = m_phase;
        double dt = std::abs(m_inc);

        switch (m_wave) {
            case Sine:
                out = (float)std::sin(2.0 * M_PI * t);
                break;
            case Saw:
                out = 2.0f * (float)t - 1.0f;
                out -= polyBLEP(t, dt);
                break;
            case Square:
                out = (t < 0.5) ? 1.0f : -1.0f;
                out += polyBLEP(t, dt);
                out -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
                break;
            case Triangle: {
                float sq = (t < 0.5) ? 1.0f : -1.0f;
                sq += polyBLEP(t, dt);
                sq -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
                m_triAccum += 4.0f * (float)dt * sq;
                m_triAccum *= 0.999f;
                out = m_triAccum;
                break;
            }
            case Noise:
                out = noise();
                break;
            default: break;
        }

        m_phase += m_inc;
        while (m_phase >= 1.0) m_phase -= 1.0;
        while (m_phase <  0.0) m_phase += 1.0;
        return out;
    }

    void   reset() { m_phase = 0.0; m_triAccum = 0.0f; }
    double phase() const { return m_phase; }

private:
    static float polyBLEP(double t, double dt) {
        if (dt <= 0.0) return 0.0f;
        if (t < dt) { double x = t / dt; return (float)(x + x - x * x - 1.0); }
        if (t > 1.0 - dt) { double x = (t - 1.0) / dt; return (float)(x * x + x + x + 1.0); }
        return 0.0f;
    }

    float noise() {
        m_rng ^= m_rng << 13; m_rng ^= m_rng >> 17; m_rng ^= m_rng << 5;
        return (float)(m_rng & 0xFFFF) / 32768.0f - 1.0f;
    }

    double   m_sr       = 44100.0;
    double   m_phase    = 0.0;
    double   m_inc      = 0.0;
    Waveform m_wave     = Sine;
    float    m_triAccum = 0.0f;
    uint32_t m_rng      = 12345;
};

} // namespace instruments
} // namespace yawn
