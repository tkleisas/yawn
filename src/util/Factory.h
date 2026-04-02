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

#include "effects/AudioEffect.h"
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"

#include "midi/MidiEffect.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"

namespace yawn {

// Factory functions that map id strings to make_unique calls.
// Used by the project serializer to reconstruct objects from saved state.

inline std::unique_ptr<instruments::Instrument> createInstrument(const std::string& id) {
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
        {"filter",      [] { return std::make_unique<effects::Filter>(); }},
        {"chorus",      [] { return std::make_unique<effects::Chorus>(); }},
        {"distortion",  [] { return std::make_unique<effects::Distortion>(); }},
        {"tape",        [] { return std::make_unique<effects::TapeEmulation>(); }},
        {"amp",         [] { return std::make_unique<effects::AmpSimulator>(); }},
        {"oscilloscope",[] { return std::make_unique<effects::Oscilloscope>(); }},
        {"spectrum",    [] { return std::make_unique<effects::SpectrumAnalyzer>(); }},
    };
    auto it = registry.find(id);
    return (it != registry.end()) ? it->second() : nullptr;
}

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
    };
    auto it = registry.find(id);
    return (it != registry.end()) ? it->second() : nullptr;
}

} // namespace yawn
