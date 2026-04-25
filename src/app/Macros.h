#pragma once

// Per-track macro device — eight always-on values (0..1) plus a list
// of mappings that route each macro to one or more parameters across
// the track's instruments / effects / shaders.
//
// One MacroDevice always lives on every Track (audio, MIDI, visual
// alike). It's the "leftmost device" in the conceptual chain: the
// canonical destination for hardware encoder banks (Push 1..8 / Move
// encoders) and the user-facing layer between hardware / automation
// and the per-device parameters underneath.
//
// Phase 4.1 scope (initial): data model + per-frame mapping
// evaluation for visual tracks only. Audio + MIDI mapping kinds are
// already declared but the App-side application path arrives in 4.2.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace yawn {

// Saved per-knob LFO state. Lifted out of VisualClip in Phase 4.1
// because LFOs modulate the macro that drives the knob, not the
// clip — moving them keeps the modulator next to its own values.
struct SavedKnobLFO {
    bool    enabled = false;
    uint8_t shape   = 0;        // VisualLFO::Shape
    float   rate    = 1.0f;
    float   depth   = 0.3f;
    bool    sync    = true;
};

// Identifies a parameter target — what a macro mapping points at.
// `kind` selects the namespace, `index` resolves a chain slot when
// applicable (which audio FX / which chain pass), `paramName` is the
// canonical engine-known name for the field. Together they map
// 1:1 to one of the engine's setX(track, idx, name, value) APIs.
struct MacroTarget {
    enum class Kind : uint8_t {
        None = 0,
        // Visual targets (Phase 4.1 — wired through immediately).
        VisualSourceParam,        // visual clip source @range param
        VisualChainParam,         // visual track effectChain[index] param
        // Audio / MIDI targets — declared now, application path lands
        // alongside the audio/MIDI macro UI in Phase 4.2.
        AudioInstrumentParam,     // track instrument param
        AudioEffectParam,         // track FX chain[index] param
        MidiEffectParam,          // track MIDI-FX chain[index] param
        // Track-level controls. Universal across track types.
        TrackVolume,
        TrackPan,
    };
    Kind        kind  = Kind::None;
    int         index = 0;
    std::string paramName;
};

// One macro -> target mapping. The macro's 0..1 value is mapped
// linearly into [rangeMin, rangeMax] of the target's normalised
// range. rangeMin > rangeMax is intentionally allowed — it inverts
// the mapping (useful for "knob up = filter close + reverb open"
// style musical relationships).
struct MacroMapping {
    int         macroIdx = 0;        // 0..7 (macro 1..8)
    MacroTarget target;
    float       rangeMin = 0.0f;
    float       rangeMax = 1.0f;
};

// Per-track macro device. Always present on every Track (no add /
// remove gesture in the UI). Default values sit at 0.5 so a fresh
// track behaves identically to a centred encoder bank with no
// mappings active. Labels are user-renameable; empty falls back to
// "Macro N" when rendered.
struct MacroDevice {
    static constexpr int kNumMacros = 8;

    std::array<float, kNumMacros>        values { 0.5f, 0.5f, 0.5f, 0.5f,
                                                  0.5f, 0.5f, 0.5f, 0.5f };
    std::array<std::string, kNumMacros>  labels {};
    std::array<SavedKnobLFO, kNumMacros> lfos   {};
    std::vector<MacroMapping>            mappings;
};

} // namespace yawn
