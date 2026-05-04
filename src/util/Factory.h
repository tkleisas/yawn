#pragma once

#include <memory>
#include <string>
#include <unordered_map>
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

// Factory functions that map id strings to make_unique calls.
// Used by the project serializer to reconstruct objects from saved state.

inline std::unique_ptr<instruments::Instrument> createInstrument(const std::string& id) {
    // Handle VST3 instruments: id = "vst3:<classID>", modulePath needed separately
    // For VST3, use createVST3Instrument() below instead
    static const std::unordered_map<std::string,
        std::function<std::unique_ptr<instruments::Instrument>()>> registry = {
        {"subsynth",   [] { return std::make_unique<instruments::SubtractiveSynth>(); }},
        {"fmsynth",    [] { return std::make_unique<instruments::FMSynth>(); }},
        {"sampler",    [] { return std::make_unique<instruments::Sampler>(); }},
        {"drumrack",   [] { return std::make_unique<instruments::DrumRack>(); }},
        {"drumslop",   [] { return std::make_unique<instruments::DrumSlop>(); }},
        {"karplus",    [] { return std::make_unique<instruments::KarplusStrong>(); }},
        {"wavetable",  [] { return std::make_unique<instruments::WavetableSynth>(); }},
        {"granular",   [] { return std::make_unique<instruments::GranularSynth>(); }},
        {"vocoder",    [] { return std::make_unique<instruments::Vocoder>(); }},
        {"multisampler",[] { return std::make_unique<instruments::Multisampler>(); }},
        {"instrack",   [] { return std::make_unique<instruments::InstrumentRack>(); }},
        {"drumsynth", [] { return std::make_unique<instruments::DrumSynth>(); }},
        {"stringmachine", [] { return std::make_unique<instruments::StringMachine>(); }},
        {"drawbarorgan", [] { return std::make_unique<instruments::DrawbarOrgan>(); }},
        {"electricpiano", [] { return std::make_unique<instruments::ElectricPiano>(); }},
    };
    auto it = registry.find(id);
    return (it != registry.end()) ? it->second() : nullptr;
}

inline std::unique_ptr<effects::AudioEffect> createAudioEffect(const std::string& id) {
    static const std::unordered_map<std::string,
        std::function<std::unique_ptr<effects::AudioEffect>()>> registry = {
        {"reverb",      [] { return std::make_unique<effects::Reverb>(); }},
        {"delay",       [] { return std::make_unique<effects::Delay>(); }},
        {"eq",          [] { return std::make_unique<effects::EQ>(); }},
        {"compressor",  [] { return std::make_unique<effects::Compressor>(); }},
        {"limiter",     [] { return std::make_unique<effects::Limiter>(); }},
        {"filter",      [] { return std::make_unique<effects::Filter>(); }},
        {"chorus",      [] { return std::make_unique<effects::Chorus>(); }},
        {"phaser",      [] { return std::make_unique<effects::Phaser>(); }},
        {"wah",         [] { return std::make_unique<effects::Wah>(); }},
        {"rotary",      [] { return std::make_unique<effects::Rotary>(); }},
        {"distortion",  [] { return std::make_unique<effects::Distortion>(); }},
        {"tape",        [] { return std::make_unique<effects::TapeEmulation>(); }},
        {"amp",         [] { return std::make_unique<effects::AmpSimulator>(); }},
        {"oscilloscope",[] { return std::make_unique<effects::Oscilloscope>(); }},
        {"spectrum",    [] { return std::make_unique<effects::SpectrumAnalyzer>(); }},
        {"tuner",       [] { return std::make_unique<effects::Tuner>(); }},
        {"bitcrusher",  [] { return std::make_unique<effects::Bitcrusher>(); }},
        {"noisegate",   [] { return std::make_unique<effects::NoiseGate>(); }},
        {"pingpongdelay",[] { return std::make_unique<effects::PingPongDelay>(); }},
        {"envfollower", [] { return std::make_unique<effects::EnvelopeFollower>(); }},
        {"splineeq",    [] { return std::make_unique<effects::SplineEQ>(); }},
        {"neuralamp",   [] { return std::make_unique<effects::NeuralAmp>(); }},
        {"convreverb",  [] { return std::make_unique<effects::ConvolutionReverb>(); }},
    };
    auto it = registry.find(id);
    return (it != registry.end()) ? it->second() : nullptr;
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
    static const std::unordered_map<std::string,
        std::function<std::unique_ptr<midi::MidiEffect>()>> registry = {
        {"arp",        [] { return std::make_unique<midi::Arpeggiator>(); }},
        {"chord",      [] { return std::make_unique<midi::Chord>(); }},
        {"scale",      [] { return std::make_unique<midi::Scale>(); }},
        {"notelength", [] { return std::make_unique<midi::NoteLength>(); }},
        {"velocity",   [] { return std::make_unique<midi::VelocityEffect>(); }},
        {"random",     [] { return std::make_unique<midi::MidiRandom>(); }},
        {"pitch",      [] { return std::make_unique<midi::MidiPitch>(); }},
        {"lfo",        [] { return std::make_unique<midi::LFO>(); }},
    };
    auto it = registry.find(id);
    return (it != registry.end()) ? it->second() : nullptr;
}

} // namespace yawn

// Name-based factory helpers — used by UI menus (display names → make_unique).
// These are separate from the id-based factories above, which are used by
// the project serializer (internal IDs like "subsynth" → make_unique).
inline std::unique_ptr<yawn::instruments::Instrument> createInstrumentByName(const std::string& n) {
    if (n == "Subtractive Synth") return std::make_unique<yawn::instruments::SubtractiveSynth>();
    if (n == "FM Synth")          return std::make_unique<yawn::instruments::FMSynth>();
    if (n == "Sampler")           return std::make_unique<yawn::instruments::Sampler>();
    if (n == "Drum Rack")         return std::make_unique<yawn::instruments::DrumRack>();
    if (n == "Drum Synth")        return std::make_unique<yawn::instruments::DrumSynth>();
    if (n == "DrumSlop")          return std::make_unique<yawn::instruments::DrumSlop>();
    if (n == "Karplus-Strong")    return std::make_unique<yawn::instruments::KarplusStrong>();
    if (n == "Wavetable Synth")   return std::make_unique<yawn::instruments::WavetableSynth>();
    if (n == "Granular Synth")    return std::make_unique<yawn::instruments::GranularSynth>();
    if (n == "Vocoder")           return std::make_unique<yawn::instruments::Vocoder>();
    if (n == "Multisampler")      return std::make_unique<yawn::instruments::Multisampler>();
    if (n == "Instrument Rack")   return std::make_unique<yawn::instruments::InstrumentRack>();
    if (n == "String Machine")    return std::make_unique<yawn::instruments::StringMachine>();
    if (n == "Drawbar Organ")     return std::make_unique<yawn::instruments::DrawbarOrgan>();
    if (n == "Electric Piano")    return std::make_unique<yawn::instruments::ElectricPiano>();
    return nullptr;
}

inline std::unique_ptr<yawn::effects::AudioEffect> createAudioEffectByName(const std::string& n) {
    if (n == "Reverb")            return std::make_unique<yawn::effects::Reverb>();
    if (n == "Delay")             return std::make_unique<yawn::effects::Delay>();
    if (n == "EQ")                return std::make_unique<yawn::effects::EQ>();
    if (n == "Compressor")        return std::make_unique<yawn::effects::Compressor>();
    if (n == "Limiter")           return std::make_unique<yawn::effects::Limiter>();
    if (n == "Filter")            return std::make_unique<yawn::effects::Filter>();
    if (n == "Chorus")            return std::make_unique<yawn::effects::Chorus>();
    if (n == "Phaser")            return std::make_unique<yawn::effects::Phaser>();
    if (n == "Wah")               return std::make_unique<yawn::effects::Wah>();
    if (n == "Rotary")            return std::make_unique<yawn::effects::Rotary>();
    if (n == "Distortion")        return std::make_unique<yawn::effects::Distortion>();
    if (n == "Tape Emulation")    return std::make_unique<yawn::effects::TapeEmulation>();
    if (n == "Amp Simulator")     return std::make_unique<yawn::effects::AmpSimulator>();
    if (n == "Oscilloscope")      return std::make_unique<yawn::effects::Oscilloscope>();
    if (n == "Spectrum Analyzer" || n == "Spectrum") return std::make_unique<yawn::effects::SpectrumAnalyzer>();
    if (n == "Tuner")             return std::make_unique<yawn::effects::Tuner>();
    if (n == "Bitcrusher")        return std::make_unique<yawn::effects::Bitcrusher>();
    if (n == "Noise Gate")        return std::make_unique<yawn::effects::NoiseGate>();
    if (n == "Ping-Pong Delay")   return std::make_unique<yawn::effects::PingPongDelay>();
    if (n == "Envelope Follower") return std::make_unique<yawn::effects::EnvelopeFollower>();
    if (n == "Spline EQ")         return std::make_unique<yawn::effects::SplineEQ>();
    if (n == "Neural Amp")        return std::make_unique<yawn::effects::NeuralAmp>();
    if (n == "Convolution Reverb")return std::make_unique<yawn::effects::ConvolutionReverb>();
    return nullptr;
}

inline std::unique_ptr<yawn::midi::MidiEffect> createMidiEffectByName(const std::string& n) {
    if (n == "Arpeggiator")    return std::make_unique<yawn::midi::Arpeggiator>();
    if (n == "Chord")          return std::make_unique<yawn::midi::Chord>();
    if (n == "Scale")          return std::make_unique<yawn::midi::Scale>();
    if (n == "Note Length")    return std::make_unique<yawn::midi::NoteLength>();
    if (n == "Velocity")       return std::make_unique<yawn::midi::VelocityEffect>();
    if (n == "Random" || n == "MIDI Random") return std::make_unique<yawn::midi::MidiRandom>();
    if (n == "Pitch" || n == "MIDI Pitch")   return std::make_unique<yawn::midi::MidiPitch>();
    if (n == "LFO")            return std::make_unique<yawn::midi::LFO>();
    return nullptr;
}
