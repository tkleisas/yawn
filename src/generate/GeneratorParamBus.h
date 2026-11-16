#pragma once

// GeneratorParamBus — lock-free bridge from MIDI Learn (audio thread)
// to the generative-song panel (UI thread). Slots are fixed indices so
// persisted MIDI mappings stay stable across sessions.

#include <array>
#include <atomic>

namespace yawn {
namespace generate {

enum GeneratorParamSlot : int {
    kGenDensity    = 0,   // 0..1
    kGenNoteLength = 1,   // 0.1..2
    kGenSwing      = 2,   // 0..0.75
    kGenTempo      = 3,   // 0..300 (0 = auto)
    kGenStyleBase  = 100, // + style index (0..31)
    kGenSlotCount  = 132,
};

class GeneratorParamBus {
public:
    static GeneratorParamBus& instance() {
        static GeneratorParamBus bus;
        return bus;
    }

    void write(int slot, float value) {
        if (slot < 0 || slot >= kGenSlotCount) return;
        m_values[static_cast<size_t>(slot)].store(value,
                                                  std::memory_order_relaxed);
        m_version.fetch_add(1, std::memory_order_release);
    }

    float read(int slot) const {
        if (slot < 0 || slot >= kGenSlotCount) return 0.0f;
        return m_values[static_cast<size_t>(slot)].load(
            std::memory_order_relaxed);
    }

    uint32_t version() const {
        return m_version.load(std::memory_order_acquire);
    }

private:
    GeneratorParamBus() {
        for (auto& v : m_values) v.store(0.0f);
    }
    std::array<std::atomic<float>, kGenSlotCount> m_values;
    std::atomic<uint32_t> m_version{0};
};

} // namespace generate
} // namespace yawn
