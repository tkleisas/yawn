#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>

#include "instruments/Instrument.h"
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "instruments/Multisampler.h"
#include "instruments/InstrumentRack.h"
#include "instruments/DrumSynth.h"
#include "instruments/StringMachine.h"
#include "instruments/DrawbarOrgan.h"
#include "instruments/ElectricPiano.h"

#include "effects/AudioEffect.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Limiter.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Phaser.h"
#include "effects/Wah.h"
#include "effects/Rotary.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include "effects/Tuner.h"
#include "effects/Bitcrusher.h"
#include "effects/NoiseGate.h"
#include "effects/PingPongDelay.h"
#include "effects/EnvelopeFollower.h"
#include "effects/SplineEQ.h"
#include "effects/NeuralAmp.h"
#include "effects/ConvolutionReverb.h"
#include "effects/BeatRepeat.h"
#include "effects/BufferRepeat.h"
#include "effects/Resampler.h"
#include "effects/ClockDrift.h"
#include "effects/CDError.h"
#include "effects/AutoPanner.h"

#include "midi/MidiEffect.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "midi/LFO.h"

#ifdef YAWN_HAS_VST3
#include "vst3/VST3Instrument.h"
#include "vst3/VST3Effect.h"
#endif

namespace yawn {

// ── Device descriptor tables — the single source of truth ─────────
//
// Every built-in device has ONE row here. Menus iterate the table,
// the serializer's id-based factories and the undo system's
// name-based factories both look up in it, and the factory tests are
// data-driven over it — so a device added in one place can't drift
// out of another (previously the catalog was copy-pasted into 4 menu
// lists + 2 factory if-chains + the library classifier, and "Auto
// Panner" fell out of createAudioEffectByName, silently breaking
// undo of its removal).
//
// displayName : the menu label ("Conv Reverb").
// id          : the serializer's internal id ("convreverb").
// className   : what an instance reports from name() ("Convolution
//               Reverb") — used by undo, which re-creates devices
//               from fx->name().
// alias       : optional legacy/extra name still accepted by the
//               name-based lookup (nullptr for most).

template <typename T>
struct DeviceDescriptor {
    const char* displayName;
    const char* id;
    const char* className;
    const char* alias;
    std::function<std::unique_ptr<T>()> make;
};

inline const std::vector<DeviceDescriptor<effects::AudioEffect>>&
audioEffectDescriptors() {
    // Menu order (Add Effect menus iterate top-to-bottom).
    static const std::vector<DeviceDescriptor<effects::AudioEffect>> table = {
        {"Reverb",            "reverb",       "Reverb",              nullptr,
            [] { return std::make_unique<effects::Reverb>(); }},
        {"Delay",             "delay",        "Delay",               nullptr,
            [] { return std::make_unique<effects::Delay>(); }},
        {"EQ",                "eq",           "EQ",                  nullptr,
            [] { return std::make_unique<effects::EQ>(); }},
        {"Compressor",        "compressor",   "Compressor",          nullptr,
            [] { return std::make_unique<effects::Compressor>(); }},
        {"Limiter",           "limiter",      "Limiter",             nullptr,
            [] { return std::make_unique<effects::Limiter>(); }},
        {"Filter",            "filter",       "Filter",              nullptr,
            [] { return std::make_unique<effects::Filter>(); }},
        {"Chorus",            "chorus",       "Chorus",              nullptr,
            [] { return std::make_unique<effects::Chorus>(); }},
        {"Phaser",            "phaser",       "Phaser",              nullptr,
            [] { return std::make_unique<effects::Phaser>(); }},
        {"Wah",               "wah",          "Wah",                 nullptr,
            [] { return std::make_unique<effects::Wah>(); }},
        {"Rotary",            "rotary",       "Rotary",              nullptr,
            [] { return std::make_unique<effects::Rotary>(); }},
        {"Auto Panner",       "autopanner",   "Auto Panner",         nullptr,
            [] { return std::make_unique<effects::AutoPanner>(); }},
        {"Distortion",        "distortion",   "Distortion",          nullptr,
            [] { return std::make_unique<effects::Distortion>(); }},
        {"Bitcrusher",        "bitcrusher",   "Bitcrusher",          nullptr,
            [] { return std::make_unique<effects::Bitcrusher>(); }},
        {"Beat Repeat",       "beatrepeat",   "Beat Repeat",         nullptr,
            [] { return std::make_unique<effects::BeatRepeat>(); }},
        {"Buffer Repeat",     "bufferrepeat", "Buffer Repeat",       nullptr,
            [] { return std::make_unique<effects::BufferRepeat>(); }},
        {"Resampler",         "resampler",    "Resampler",           nullptr,
            [] { return std::make_unique<effects::Resampler>(); }},
        {"Clock Drift",       "clockdrift",   "Clock Drift",         nullptr,
            [] { return std::make_unique<effects::ClockDrift>(); }},
        {"CD Error",          "cderror",      "CD Error",            nullptr,
            [] { return std::make_unique<effects::CDError>(); }},
        {"Noise Gate",        "noisegate",    "Noise Gate",          nullptr,
            [] { return std::make_unique<effects::NoiseGate>(); }},
        {"Ping-Pong Delay",   "pingpongdelay","Ping-Pong Delay",     nullptr,
            [] { return std::make_unique<effects::PingPongDelay>(); }},
        {"Envelope Follower", "envfollower",  "Envelope Follower",   nullptr,
            [] { return std::make_unique<effects::EnvelopeFollower>(); }},
        {"Spline EQ",         "splineeq",     "Spline EQ",           nullptr,
            [] { return std::make_unique<effects::SplineEQ>(); }},
        {"Neural Amp",        "neuralamp",    "Neural Amp",          nullptr,
            [] { return std::make_unique<effects::NeuralAmp>(); }},
        {"Conv Reverb",       "convreverb",   "Convolution Reverb",  nullptr,
            [] { return std::make_unique<effects::ConvolutionReverb>(); }},
        {"Tape Emulation",    "tape",         "Tape Emulation",      nullptr,
            [] { return std::make_unique<effects::TapeEmulation>(); }},
        {"Amp Simulator",     "amp",          "Amp Simulator",       nullptr,
            [] { return std::make_unique<effects::AmpSimulator>(); }},
        {"Oscilloscope",      "oscilloscope", "Oscilloscope",        nullptr,
            [] { return std::make_unique<effects::Oscilloscope>(); }},
        {"Spectrum",          "spectrum",     "Spectrum",            "Spectrum Analyzer",
            [] { return std::make_unique<effects::SpectrumAnalyzer>(); }},
        {"Tuner",             "tuner",        "Tuner",               nullptr,
            [] { return std::make_unique<effects::Tuner>(); }},
    };
    return table;
}

inline const std::vector<DeviceDescriptor<instruments::Instrument>>&
instrumentDescriptors() {
    // Menu order (Set Instrument menus). Drawbar Organ and Electric
    // Piano are listed too, but the track menu gives them custom
    // entries (auto-insert Rotary / Phaser) — iterate and special-case.
    static const std::vector<DeviceDescriptor<instruments::Instrument>> table = {
        {"SubSynth",          "subsynth",     "Subtractive Synth",   nullptr,
            [] { return std::make_unique<instruments::SubtractiveSynth>(); }},
        {"FM Synth",          "fmsynth",      "FM Synth",            nullptr,
            [] { return std::make_unique<instruments::FMSynth>(); }},
        {"Sampler",           "sampler",      "Sampler",             nullptr,
            [] { return std::make_unique<instruments::Sampler>(); }},
        {"Drum Rack",         "drumrack",     "Drum Rack",           nullptr,
            [] { return std::make_unique<instruments::DrumRack>(); }},
        {"Drum Synth",        "drumsynth",    "Drum Synth",          nullptr,
            [] { return std::make_unique<instruments::DrumSynth>(); }},
        {"DrumSlop",          "drumslop",     "DrumSlop",            nullptr,
            [] { return std::make_unique<instruments::DrumSlop>(); }},
        {"Karplus-Strong",    "karplus",      "Karplus-Strong",      nullptr,
            [] { return std::make_unique<instruments::KarplusStrong>(); }},
        {"Wavetable Synth",   "wavetable",    "Wavetable Synth",     nullptr,
            [] { return std::make_unique<instruments::WavetableSynth>(); }},
        {"Granular Synth",    "granular",     "Granular Synth",      nullptr,
            [] { return std::make_unique<instruments::GranularSynth>(); }},
        {"Vocoder",           "vocoder",      "Vocoder",             nullptr,
            [] { return std::make_unique<instruments::Vocoder>(); }},
        {"Multisampler",      "multisampler", "Multisampler",        nullptr,
            [] { return std::make_unique<instruments::Multisampler>(); }},
        {"Instrument Rack",   "instrack",     "Instrument Rack",     nullptr,
            [] { return std::make_unique<instruments::InstrumentRack>(); }},
        {"String Machine",    "stringmachine","String Machine",      nullptr,
            [] { return std::make_unique<instruments::StringMachine>(); }},
        {"Drawbar Organ",     "drawbarorgan", "Drawbar Organ",       nullptr,
            [] { return std::make_unique<instruments::DrawbarOrgan>(); }},
        {"Electric Piano",    "electricpiano","Electric Piano",      nullptr,
            [] { return std::make_unique<instruments::ElectricPiano>(); }},
    };
    return table;
}

inline const std::vector<DeviceDescriptor<midi::MidiEffect>>&
midiEffectDescriptors() {
    static const std::vector<DeviceDescriptor<midi::MidiEffect>> table = {
        {"Arpeggiator",       "arp",          "Arpeggiator",         nullptr,
            [] { return std::make_unique<midi::Arpeggiator>(); }},
        {"Chord",             "chord",        "Chord",               nullptr,
            [] { return std::make_unique<midi::Chord>(); }},
        {"Scale",             "scale",        "Scale",               nullptr,
            [] { return std::make_unique<midi::Scale>(); }},
        {"Note Length",       "notelength",   "Note Length",         nullptr,
            [] { return std::make_unique<midi::NoteLength>(); }},
        {"Velocity",          "velocity",     "Velocity",            nullptr,
            [] { return std::make_unique<midi::VelocityEffect>(); }},
        {"Random",            "random",       "Random",              "MIDI Random",
            [] { return std::make_unique<midi::MidiRandom>(); }},
        {"Pitch",             "pitch",        "Pitch",               "MIDI Pitch",
            [] { return std::make_unique<midi::MidiPitch>(); }},
        {"LFO",               "lfo",          "LFO",                 nullptr,
            [] { return std::make_unique<midi::LFO>(); }},
    };
    return table;
}

// ── Factories ─────────────────────────────────────────────────────
//
// createX(id)   — serializer path: internal id ("subsynth").
// createXByName — undo path: accepts displayName, className (what
//                 name() returns), id, or the legacy alias.
// Both are thin loops over the descriptor tables above.

inline std::unique_ptr<instruments::Instrument> createInstrument(const std::string& id) {
    for (const auto& d : instrumentDescriptors())
        if (id == d.id) return d.make();
    return nullptr;
}

inline std::unique_ptr<effects::AudioEffect> createAudioEffect(const std::string& id) {
    for (const auto& d : audioEffectDescriptors())
        if (id == d.id) return d.make();
    return nullptr;
}

#ifdef YAWN_HAS_VST3
inline std::unique_ptr<instruments::Instrument> createVST3Instrument(
    const std::string& modulePath, const std::string& classIDString) {
    return std::make_unique<vst3::VST3Instrument>(modulePath, classIDString);
}

inline std::unique_ptr<effects::AudioEffect> createVST3Effect(
    const std::string& modulePath, const std::string& classIDString) {
    return std::make_unique<vst3::VST3Effect>(modulePath, classIDString);
}

inline bool isVST3Id(const std::string& id) {
    return id.size() > 5 && id.substr(0, 5) == "vst3:";
}

inline std::string vst3ClassIDFromId(const std::string& id) {
    return id.substr(5);
}
#endif

inline std::unique_ptr<midi::MidiEffect> createMidiEffect(const std::string& id) {
    for (const auto& d : midiEffectDescriptors())
        if (id == d.id) return d.make();
    return nullptr;
}

inline std::unique_ptr<instruments::Instrument> createInstrumentByName(const std::string& n) {
    for (const auto& d : instrumentDescriptors())
        if (n == d.displayName || n == d.className || n == d.id ||
            (d.alias && n == d.alias))
            return d.make();
    return nullptr;
}

inline std::unique_ptr<effects::AudioEffect> createAudioEffectByName(const std::string& n) {
    for (const auto& d : audioEffectDescriptors())
        if (n == d.displayName || n == d.className || n == d.id ||
            (d.alias && n == d.alias))
            return d.make();
    return nullptr;
}

inline std::unique_ptr<midi::MidiEffect> createMidiEffectByName(const std::string& n) {
    for (const auto& d : midiEffectDescriptors())
        if (n == d.displayName || n == d.className || n == d.id ||
            (d.alias && n == d.alias))
            return d.make();
    return nullptr;
}

} // namespace yawn
