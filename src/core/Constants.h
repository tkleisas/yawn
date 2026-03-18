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
static constexpr int kDefaultNumTracks = 8;
static constexpr int kDefaultNumScenes = 8;

} // namespace yawn
