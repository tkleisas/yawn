#pragma once

// Central configuration constants for Y.A.W.N
// All subsystems reference these instead of defining their own limits.

namespace yawn {

// --- Channel limits ---
static constexpr int kMaxAudioTracks   = 64;
static constexpr int kMaxMidiTracks    = 64;
static constexpr int kMaxTracks        = kMaxAudioTracks;  // unified track limit for audio engine
static constexpr int kMaxReturnBuses   = 8;
static constexpr int kMaxSendsPerTrack = kMaxReturnBuses;
static constexpr int kMaxScenes        = 256;

// --- Audio buffer limits ---
static constexpr int kMaxFramesPerBuffer = 4096;
static constexpr int kMaxOutputChannels  = 2;

// --- MIDI buffer limits ---
static constexpr int kMaxMidiMessagesPerBuffer = 1024;
static constexpr int kMaxMidiPorts             = 16;

// --- Defaults ---
// 5 tracks = 2 Audio + 2 MIDI + 1 Visual (track types assigned by the
// host after init — Project::init creates them all as Audio and then
// App's setupDefaultTrackTypes() mutates the MIDI / Visual slots).
static constexpr int kDefaultNumTracks = 5;
static constexpr int kDefaultNumScenes = 4;

// Default audio sample rate — referenced by all audio/midi/instrument
// classes as their initial m_sampleRate value (the engine re-queries the
// device's actual rate on open and re-inits, so this is only the pre-open
// fallback). 48 kHz matches modern hardware: PipeWire/CoreAudio graphs,
// USB interfaces, and NAM captures are 48 k native, so defaulting to 44.1
// forced an OS-level 44.1↔48 resampler (extra latency + CPU). Transport.h
// defines kDefaultSampleRate in its own namespace too — keep them in sync.
static constexpr double kDefaultSampleRate = 48000.0;

} // namespace yawn
