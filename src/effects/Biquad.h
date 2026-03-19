#pragma once

// Biquad — second-order IIR filter building block.
// Used by EQ, Filter, and other effects that need frequency-selective processing.
// Transposed Direct Form II for numerical stability.

#include <algorithm>
#include <cmath>

namespace yawn {
namespace effects {

class Biquad {
public:
    enum class Type { LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf };

    void reset() { z1 = z2 = 0.0f; }

    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        // Clamp state to prevent blowup from rapid coefficient changes
        z1 = std::clamp(z1, -10.0f, 10.0f);
        z2 = std::clamp(z2, -10.0f, 10.0f);
        return y;
    }

    // Compute coefficients for various filter types
    void compute(Type type, double sampleRate, double freq, double gainDB, double Q) {
        double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        double cosw0 = std::cos(w0);
        double sinw0 = std::sin(w0);
        double alpha = sinw0 / (2.0 * Q);

        double A = 0.0;
        if (type == Type::Peak || type == Type::LowShelf || type == Type::HighShelf)
            A = std::pow(10.0, gainDB / 40.0);

        double b0d, b1d, b2d, a0d, a1d, a2d;

        switch (type) {
        case Type::LowPass:
            b0d = (1.0 - cosw0) / 2.0;
            b1d = 1.0 - cosw0;
            b2d = (1.0 - cosw0) / 2.0;
            a0d = 1.0 + alpha;
            a1d = -2.0 * cosw0;
            a2d = 1.0 - alpha;
            break;
        case Type::HighPass:
            b0d = (1.0 + cosw0) / 2.0;
            b1d = -(1.0 + cosw0);
            b2d = (1.0 + cosw0) / 2.0;
            a0d = 1.0 + alpha;
            a1d = -2.0 * cosw0;
            a2d = 1.0 - alpha;
            break;
        case Type::BandPass:
            b0d = alpha;
            b1d = 0.0;
            b2d = -alpha;
            a0d = 1.0 + alpha;
            a1d = -2.0 * cosw0;
            a2d = 1.0 - alpha;
            break;
        case Type::Notch:
            b0d = 1.0;
            b1d = -2.0 * cosw0;
            b2d = 1.0;
            a0d = 1.0 + alpha;
            a1d = -2.0 * cosw0;
            a2d = 1.0 - alpha;
            break;
        case Type::Peak:
            b0d = 1.0 + alpha * A;
            b1d = -2.0 * cosw0;
            b2d = 1.0 - alpha * A;
            a0d = 1.0 + alpha / A;
            a1d = -2.0 * cosw0;
            a2d = 1.0 - alpha / A;
            break;
        case Type::LowShelf: {
            double sqrtA = std::sqrt(A);
            b0d = A * ((A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
            b1d = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
            b2d = A * ((A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
            a0d = (A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
            a1d = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
            a2d = (A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
            break;
        }
        case Type::HighShelf: {
            double sqrtA = std::sqrt(A);
            b0d = A * ((A + 1.0) + (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha);
            b1d = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
            b2d = A * ((A + 1.0) + (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha);
            a0d = (A + 1.0) - (A - 1.0) * cosw0 + 2.0 * sqrtA * alpha;
            a1d = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
            a2d = (A + 1.0) - (A - 1.0) * cosw0 - 2.0 * sqrtA * alpha;
            break;
        }
        }

        double inv_a0 = 1.0 / a0d;
        b0 = static_cast<float>(b0d * inv_a0);
        b1 = static_cast<float>(b1d * inv_a0);
        b2 = static_cast<float>(b2d * inv_a0);
        a1 = static_cast<float>(a1d * inv_a0);
        a2 = static_cast<float>(a2d * inv_a0);
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

} // namespace effects
} // namespace yawn
