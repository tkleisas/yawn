#include <gtest/gtest.h>
#include "audio/Oversampling.h"
#include <cmath>
#include <vector>

using namespace yawn::audio::dsp;

namespace {
constexpr double kPi = 3.14159265358979323846;

// |X(f)| via Goertzel — unnormalised, used for relative comparison only.
double mag(const std::vector<float>& x, double f, double sr) {
    const double w = 2.0 * kPi * f / sr;
    const double cw = std::cos(w), sw = std::sin(w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x) { const double s0 = v + 2.0 * cw * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * cw, im = s2 * sw;
    return std::sqrt(re * re + im * im);
}
} // namespace

TEST(Oversampling, HalfbandUnityDcGain) {
    float h[Downsampler2x::kTaps];
    designHalfband(h, Downsampler2x::kTaps);
    double sum = 0.0;
    for (float c : h) sum += c;
    EXPECT_NEAR(sum, 1.0, 1e-5);  // unity DC gain
}

TEST(Oversampling, IdentityReconstructsPassbandSine) {
    // f = identity → the up/down cascade should reproduce a passband signal.
    Oversampler2x os;
    const double sr = 48000.0, f = 1000.0;
    std::vector<float> in, out;
    for (int n = 0; n < 4096; ++n) {
        const float x = static_cast<float>(std::sin(2.0 * kPi * f * n / sr));
        in.push_back(x);
        out.push_back(os.process(x, [](float v) { return v; }));
    }
    const double mi = mag(in, f, sr), mo = mag(out, f, sr);
    EXPECT_NEAR(mo, mi, 0.05 * mi);  // ~unity passband gain
}

TEST(Oversampling, SuppressesNonlinearAliasing) {
    // An 18 kHz sine squared has a 36 kHz component; at 48 kHz that folds to
    // 12 kHz. Oversampling should keep the 12 kHz alias far lower.
    const double sr = 48000.0, f = 18000.0, aliasF = 12000.0;
    auto sq = [](float v) { return v * v; };

    Oversampler2x os;
    std::vector<float> base, ovs;
    for (int n = 0; n < 8192; ++n) {
        const float x = static_cast<float>(0.7 * std::sin(2.0 * kPi * f * n / sr));
        base.push_back(sq(x));            // nonlinearity at base rate
        ovs.push_back(os.process(x, sq)); // oversampled
    }
    // Skip the filter warm-up.
    std::vector<float> b(base.begin() + 256, base.end());
    std::vector<float> o(ovs.begin() + 256, ovs.end());
    const double aliasBase = mag(b, aliasF, sr);
    const double aliasOvs  = mag(o, aliasF, sr);

    EXPECT_GT(aliasBase, 1e-2);                 // base case really does alias
    EXPECT_LT(aliasOvs, aliasBase * 0.1);       // oversampling cuts it ≥ 20 dB
}
