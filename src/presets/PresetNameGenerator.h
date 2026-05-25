#pragma once

// PresetNameGenerator — produces preset names that either describe the
// sound (archetype + salient-parameter flavour, e.g. "Acid Bass",
// "Glacial Pad") or read like an Aphex Twin / Autechre track title
// (a vowel-sparse consonant-cluster grammar, e.g. "Vordhosbn", "Xtal",
// "Bbreflection"). The mix is controlled by an alien-name ratio.
//
// Pure logic, seeded RNG → reproducible. No device or UI dependencies.

#include "presets/PresetGenerator.h"   // DeviceKind, NameContext
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace presets {

// Archetype tables are owned here so naming + sampling agree on counts.
int         archetypeCount(DeviceKind kind);
const char* archetypeLabel(DeviceKind kind, int archetype);

class PresetNameGenerator {
public:
    explicit PresetNameGenerator(float alienRatio = 0.5f)
        : m_alienRatio(alienRatio < 0.0f ? 0.0f : (alienRatio > 1.0f ? 1.0f : alienRatio)) {}

    // Generate a name from a seed + context, avoiding names already in
    // `used` (case-insensitive). Deterministic for a given seed.
    std::string generate(uint64_t seed, const NameContext& ctx,
                         const std::vector<std::string>& used) const;

    // Stand-alone generators (exposed for testing / reuse).
    static std::string alienName(uint64_t seed);
    static std::string descriptiveName(uint64_t seed, const NameContext& ctx);

private:
    float m_alienRatio;
};

} // namespace presets
} // namespace yawn
