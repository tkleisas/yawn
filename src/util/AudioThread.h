#pragma once

// Realtime-thread marker.
//
// AudioEngine sets onAudioThread for the duration of every
// processAudio() block (the PortAudio callback, offline export
// renders via renderBuffer, and test pumps all count). It is written
// only by the thread itself, so reads need no synchronization.
//
// Code reached from many layers — effects, VST3 wrappers — uses this
// to pick a lock-free handoff when it is (or isn't) on the realtime
// thread, e.g. VST3PluginInstance::setParameterNormalized routes
// through an SPSC ring from UI threads but a plain list (same thread
// as the consumer) when already on the audio thread.

namespace yawn {
namespace rt {

inline thread_local bool onAudioThread = false;

// RAII guard: marks the thread for the enclosing scope's lifetime.
struct AudioThreadScope {
    AudioThreadScope()  { onAudioThread = true; }
    ~AudioThreadScope() { onAudioThread = false; }
    AudioThreadScope(const AudioThreadScope&) = delete;
    AudioThreadScope& operator=(const AudioThreadScope&) = delete;
};

} // namespace rt
} // namespace yawn
