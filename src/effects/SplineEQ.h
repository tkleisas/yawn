#pragma once
// SplineEQ — N-node parametric EQ with a freeform spline-style editor.
//
// The DSP itself is N parallel-cascaded biquads (one per active
// node), so "spline" is the EDITOR aesthetic, not the filter math
// — that's how every modern parametric EQ (FabFilter Pro-Q, etc.)
// works. The smooth curve drawn between nodes is the magnitude
// response of the cascaded filters; the user grabs nodes and drags
// them around to shape it.
//
// Per-node parameters: enabled / type / frequency / gain / Q.
// Five fields × eight nodes = 40 params. The flat parameter list is
// indexed nodeIdx*5 + field — see ParamField below for the offsets.
//
// Per-node types:
//   0 Peak       (bell, the workhorse)
//   1 Low Shelf  (gentle bass tilt)
//   2 High Shelf (treble tilt)
//   3 Notch      (narrow rejection — feedback hunts, hum removal)
//   4 Low Cut    (highpass — rolls off below cutoff)
//   5 High Cut   (lowpass  — rolls off above cutoff)
//
// Spectrum analyzer support: getInputSpectrum() / getOutputSpectrum()
// expose pre-EQ and post-EQ magnitude ring buffers so the display
// panel can overlay them in different colours. FFT runs once per
// audio block (cheap; not per-sample) into a smoothed magnitude
// array with simple peak-and-decay ballistics.

#include "effects/AudioEffect.h"
#include "effects/Biquad.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace yawn {
namespace effects {

class SplineEQ : public AudioEffect {
public:
    static constexpr int kMaxNodes = 8;
    static constexpr int kFieldsPerNode = 5;
    static constexpr int kSpectrumBins = 256;   // log-binned, not raw FFT bins
    static constexpr int kFftSize = 1024;       // raw FFT (radix-2)

    enum NodeType {
        ntPeak = 0,
        ntLowShelf,
        ntHighShelf,
        ntNotch,
        ntLowCut,
        ntHighCut,
        ntCount
    };

    enum ParamField {
        pfEnabled = 0,
        pfType,
        pfFreq,
        pfGain,
        pfQ
    };

    static constexpr int kParamCount = kMaxNodes * kFieldsPerNode;

    static int nodeParam(int node, ParamField f) {
        return node * kFieldsPerNode + static_cast<int>(f);
    }

    const char* name() const override { return "Spline EQ"; }
    const char* id()   const override { return "splineeq"; }

    void init(double sampleRate, int maxBlockSize) override {
        m_sampleRate = sampleRate;
        m_maxBlockSize = maxBlockSize;
        m_inFftBuf.assign(kFftSize, 0.0f);
        m_outFftBuf.assign(kFftSize, 0.0f);
        m_window.assign(kFftSize, 0.0f);
        m_inMags.assign(kSpectrumBins, 0.0f);
        m_outMags.assign(kSpectrumBins, 0.0f);

        // Initialise per-node defaults. ParameterInfo only carries
        // ONE default-per-field (the same Type/Freq/Gain/Q template
        // for every node), so we set up a sensible "3-band EQ"
        // layout here in code: nodes 0–2 enabled with Low Shelf /
        // Peak / High Shelf at 100 Hz / 1 kHz / 10 kHz, all at 0 dB
        // so the device is transparent on first insert. Nodes 3–7
        // disabled until the user enables them via the panel or
        // a knob.
        for (int n = 0; n < kMaxNodes; ++n) {
            m_params[nodeParam(n, pfEnabled)] = (n < 3) ? 1.0f : 0.0f;
            m_params[nodeParam(n, pfGain)]    = 0.0f;
            m_params[nodeParam(n, pfQ)]       = 0.707f;
            if (n == 0) {
                m_params[nodeParam(n, pfType)] = static_cast<float>(ntLowShelf);
                m_params[nodeParam(n, pfFreq)] = hzToTune(100.0f);
            } else if (n == 1) {
                m_params[nodeParam(n, pfType)] = static_cast<float>(ntPeak);
                m_params[nodeParam(n, pfFreq)] = hzToTune(1000.0f);
            } else if (n == 2) {
                m_params[nodeParam(n, pfType)] = static_cast<float>(ntHighShelf);
                m_params[nodeParam(n, pfFreq)] = hzToTune(10000.0f);
            } else {
                // Spread the disabled nodes across the spectrum so
                // when the user enables one its starting frequency
                // isn't piled up on the same spot as the others.
                const float t = static_cast<float>(n - 3) / 4.0f;
                m_params[nodeParam(n, pfType)] = static_cast<float>(ntPeak);
                m_params[nodeParam(n, pfFreq)] = 0.20f + t * 0.60f;
            }
            m_nodeEnabled[n] = m_params[nodeParam(n, pfEnabled)] >= 0.5f;
        }
        // Hann window — simple and well-behaved for a magnitude
        // analyser. The display side uses smoothed peak-decay so
        // we don't need fancier window functions for legibility.
        for (int i = 0; i < kFftSize; ++i)
            m_window[i] = 0.5f * (1.0f - std::cos(
                2.0f * 3.14159265f * i / static_cast<float>(kFftSize - 1)));
        reset();
        for (int n = 0; n < kMaxNodes; ++n) {
            m_dirty[n] = true;
            redesignNode(n);
        }
    }

    void reset() override {
        for (auto& bq : m_filtersL) bq.reset();
        for (auto& bq : m_filtersR) bq.reset();
        m_fftWritePos = 0;
        std::fill(m_inFftBuf.begin(), m_inFftBuf.end(), 0.0f);
        std::fill(m_outFftBuf.begin(), m_outFftBuf.end(), 0.0f);
        std::fill(m_inMags.begin(), m_inMags.end(), 0.0f);
        std::fill(m_outMags.begin(), m_outMags.end(), 0.0f);
    }

    void process(float* buffer, int numFrames, int numChannels) override {
        if (m_bypassed) return;

        // Re-design any nodes whose params changed since last block.
        // Cheap — at most kMaxNodes biquad coefficient computes per
        // block (8 × ~20 ops = 160 ops, well under the per-sample
        // filter cost).
        for (int n = 0; n < kMaxNodes; ++n) {
            if (m_dirty[n]) {
                redesignNode(n);
                m_dirty[n] = false;
            }
        }

        const bool stereo = (numChannels > 1);

        // Per-sample: cascade through the enabled nodes for each side,
        // mixing dry/wet via the chain m_mix and the per-effect mix
        // baked into kFieldsPerNode? Actually no kMix here — the
        // chain-level mix knob is sufficient for an EQ since dry-mix
        // an EQ at <100% creates phase issues that aren't musically
        // useful. (Users who want parallel EQ can use a return bus.)
        for (int i = 0; i < numFrames; ++i) {
            float l = buffer[i * numChannels];
            float r = stereo ? buffer[i * numChannels + 1] : l;

            // Cache the input for the spectrum analyser BEFORE the EQ
            // chain — both pre and post show in the display.
            const float drySumForFft = (l + r) * 0.5f;

            for (int n = 0; n < kMaxNodes; ++n) {
                if (!m_nodeEnabled[n]) continue;
                l = m_filtersL[n].process(l);
                r = m_filtersR[n].process(r);
            }

            buffer[i * numChannels] = l;
            if (stereo) buffer[i * numChannels + 1] = r;

            // Spectrum FFT input: post-EQ sum + pre-EQ sum into ring
            // buffers. We only run the FFT every kFftSize samples
            // (when the buffer is filled), not per sample.
            const float wetSumForFft = (l + r) * 0.5f;
            m_inFftBuf[m_fftWritePos]  = drySumForFft;
            m_outFftBuf[m_fftWritePos] = wetSumForFft;
            m_fftWritePos = (m_fftWritePos + 1) % kFftSize;
            if (m_fftWritePos == 0) m_fftReady = true;
        }

        if (m_fftReady) {
            m_fftReady = false;
            updateSpectrum();
        }
    }

    int parameterCount() const override { return kParamCount; }

    static constexpr const char* kTypeLabels[ntCount] = {
        "Peak", "LowShelf", "HighShelf", "Notch", "LowCut", "HighCut"
    };

    const ParameterInfo& parameterInfo(int index) const override {
        // Build per-node infos lazily-once. Each node has the same
        // five fields with identical ranges, so we generate a 5-entry
        // template and serve from it indexed by the field offset.
        static const ParameterInfo infos[kFieldsPerNode] = {
            {"On",   0.0f,    1.0f,    0.0f,  "",  false, false, WidgetHint::Toggle},
            {"Type", 0.0f, ntCount-1.0f, 0.0f, "", false, false, WidgetHint::StepSelector,
                kTypeLabels, ntCount},
            {"Freq", 0.0f,    1.0f,    0.50f, "",  false, false, WidgetHint::Knob,
                nullptr, 0, &formatFreqHz},
            {"Gain",-18.0f,   18.0f,   0.0f,  "dB",false, false, WidgetHint::DentedKnob},
            {"Q",    0.10f,  10.0f,   0.707f,"",   false, false, WidgetHint::DentedKnob},
        };
        const int field = index % kFieldsPerNode;
        return infos[std::clamp(field, 0, kFieldsPerNode - 1)];
    }

    float getParameter(int index) const override {
        if (index < 0 || index >= kParamCount) return 0.0f;
        return m_params[index];
    }

    void setParameter(int index, float value) override {
        if (index < 0 || index >= kParamCount) return;
        const auto& pi = parameterInfo(index);
        const float clamped = std::clamp(value, pi.minValue, pi.maxValue);
        if (m_params[index] == clamped) return;
        m_params[index] = clamped;
        const int node = index / kFieldsPerNode;
        const int field = index % kFieldsPerNode;
        if (field == pfEnabled) {
            m_nodeEnabled[node] = clamped >= 0.5f;
            // No coefficient redesign needed for enable toggle —
            // disable just skips the node in the cascade. We DO
            // reset the biquad memory though, so re-enabling
            // doesn't kick out a stale value.
            if (!m_nodeEnabled[node]) {
                m_filtersL[node].reset();
                m_filtersR[node].reset();
            }
        } else {
            m_dirty[node] = true;
        }
    }

    // ── Spectrum-analyser readout for the display panel ──
    // Both arrays are kSpectrumBins long, log-frequency-binned over
    // 20 Hz – 20 kHz. Values are in linear magnitude (0..~1 typical
    // ceiling). The display panel is responsible for converting to
    // dB and any further smoothing.
    const float* inputSpectrum()  const { return m_inMags.data(); }
    const float* outputSpectrum() const { return m_outMags.data(); }
    int spectrumSize() const { return kSpectrumBins; }

    // Per-node accessors for the display-panel curve drawer. These
    // skip the m_params array indirection for the hot rendering loop.
    bool      nodeEnabled(int n) const { return m_nodeEnabled[n]; }
    NodeType  nodeType(int n)    const {
        return static_cast<NodeType>(static_cast<int>(
            m_params[nodeParam(n, pfType)]));
    }
    float nodeFreqHz(int n) const {
        return tuneToHz(m_params[nodeParam(n, pfFreq)]);
    }
    float nodeGainDb(int n) const { return m_params[nodeParam(n, pfGain)]; }
    float nodeQ(int n)      const { return m_params[nodeParam(n, pfQ)];    }

    // Inverse of tuneToHz — display panel uses this when the user
    // drags a node so the result lands on the same normalised value
    // the knob would have produced.
    static float hzToTune(float hz) {
        const float h = std::clamp(hz, 20.0f, 20000.0f);
        return std::log(h / 20.0f) / std::log(1000.0f);
    }
    static float tuneToHz(float t) {
        return 20.0f * std::pow(1000.0f, std::clamp(t, 0.0f, 1.0f));
    }

private:
    static void formatFreqHz(float v, char* buf, int n) {
        const float hz = tuneToHz(v);
        if (hz >= 1000.0f) std::snprintf(buf, n, "%.2f kHz", hz / 1000.0f);
        else                std::snprintf(buf, n, "%.0f Hz", hz);
    }

    void redesignNode(int n) {
        const NodeType nt = nodeType(n);
        const float fc    = nodeFreqHz(n);
        const float gdb   = nodeGainDb(n);
        const float q     = nodeQ(n);

        const float fcClamped = std::clamp(fc, 20.0f,
            static_cast<float>(m_sampleRate) * 0.49f);

        Biquad::Type bt = Biquad::Type::Peak;
        switch (nt) {
            case ntPeak:      bt = Biquad::Type::Peak;      break;
            case ntLowShelf:  bt = Biquad::Type::LowShelf;  break;
            case ntHighShelf: bt = Biquad::Type::HighShelf; break;
            case ntNotch:     bt = Biquad::Type::Notch;     break;
            case ntLowCut:    bt = Biquad::Type::HighPass;  break;  // HP rolls off LOWS
            case ntHighCut:   bt = Biquad::Type::LowPass;   break;
            default:          bt = Biquad::Type::Peak;      break;
        }
        m_filtersL[n].compute(bt, m_sampleRate, fcClamped, gdb, q);
        m_filtersR[n].compute(bt, m_sampleRate, fcClamped, gdb, q);
    }

    // ── FFT (radix-2 Cooley-Tukey) ──
    // Direct port of the SpectrumAnalyzer's pattern. We don't link a
    // dedicated FFT library; this is fine at kFftSize=1024 running
    // once per ~21 ms (1024 / 48000) — about 50 Hz update rate, more
    // than enough for an analyser display.
    void fftRadix2(std::vector<float>& re, std::vector<float>& im) const {
        const int n = static_cast<int>(re.size());
        // Bit-reverse permutation
        int j = 0;
        for (int i = 1; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        // Cooley-Tukey
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = -2.0f * 3.14159265f / len;
            const float wRe = std::cos(ang), wIm = std::sin(ang);
            for (int i = 0; i < n; i += len) {
                float pRe = 1.0f, pIm = 0.0f;
                for (int k = 0; k < len / 2; ++k) {
                    const float uR = re[i + k], uI = im[i + k];
                    const float vR = re[i + k + len/2] * pRe - im[i + k + len/2] * pIm;
                    const float vI = re[i + k + len/2] * pIm + im[i + k + len/2] * pRe;
                    re[i + k]           = uR + vR;
                    im[i + k]           = uI + vI;
                    re[i + k + len/2]   = uR - vR;
                    im[i + k + len/2]   = uI - vI;
                    const float npRe = pRe * wRe - pIm * wIm;
                    pIm = pRe * wIm + pIm * wRe;
                    pRe = npRe;
                }
            }
        }
    }

    void updateSpectrum() {
        // Window + FFT both pre and post buffers, log-bin the
        // magnitudes into m_inMags / m_outMags, peak-and-decay
        // smoothing to make the display readable.
        std::vector<float> reIn(kFftSize), imIn(kFftSize, 0.0f);
        std::vector<float> reOut(kFftSize), imOut(kFftSize, 0.0f);
        // The ring buffer's "newest" sample is at m_fftWritePos-1 and
        // wraps back. Re-order into a contiguous time-ordered array
        // before windowing.
        for (int i = 0; i < kFftSize; ++i) {
            const int src = (m_fftWritePos + i) % kFftSize;
            reIn[i]  = m_inFftBuf[src]  * m_window[i];
            reOut[i] = m_outFftBuf[src] * m_window[i];
        }
        fftRadix2(reIn,  imIn);
        fftRadix2(reOut, imOut);

        // Log-bin the lower half of the FFT into kSpectrumBins.
        // Bin centres span 20 Hz – sampleRate/2 in log space.
        const float nyquist = static_cast<float>(m_sampleRate) * 0.5f;
        const float logLow  = std::log(20.0f);
        const float logHigh = std::log(nyquist);
        const int  half     = kFftSize / 2;
        for (int b = 0; b < kSpectrumBins; ++b) {
            const float t = static_cast<float>(b) /
                            static_cast<float>(kSpectrumBins - 1);
            const float fc = std::exp(logLow + t * (logHigh - logLow));
            const int   bin = std::clamp(
                static_cast<int>(fc * kFftSize / m_sampleRate), 1, half - 1);
            const float magIn  = std::sqrt(
                reIn[bin] * reIn[bin] + imIn[bin] * imIn[bin]) / kFftSize;
            const float magOut = std::sqrt(
                reOut[bin] * reOut[bin] + imOut[bin] * imOut[bin]) / kFftSize;
            // Peak-and-decay smoothing: fast rise, slow fall. Keeps
            // transient peaks visible long enough to register.
            m_inMags[b]  = std::max(magIn,  m_inMags[b]  * 0.85f);
            m_outMags[b] = std::max(magOut, m_outMags[b] * 0.85f);
        }
    }

    float  m_params[kParamCount] = {};
    bool   m_nodeEnabled[kMaxNodes] = {};
    bool   m_dirty[kMaxNodes] = {};
    Biquad m_filtersL[kMaxNodes];
    Biquad m_filtersR[kMaxNodes];

    // Spectrum analyser ring buffers + state
    std::vector<float> m_inFftBuf, m_outFftBuf;
    std::vector<float> m_window;
    std::vector<float> m_inMags, m_outMags;
    int                m_fftWritePos = 0;
    bool               m_fftReady = false;
};

} // namespace effects
} // namespace yawn
