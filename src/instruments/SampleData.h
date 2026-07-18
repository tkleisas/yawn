#pragma once

#include <vector>

namespace yawn {
namespace instruments {

// Immutable sample buffer shared between the UI and audio threads.
// The UI builds a fresh one, publishes it via atomic pointer swap,
// and retires the previous one (see Instrument::retireObject) so
// in-flight audio blocks finish on valid memory. Never mutated after
// publication.
struct SampleData {
    std::vector<float> samples;   // interleaved
    int frames   = 0;
    int channels = 1;
};

} // namespace instruments
} // namespace yawn
