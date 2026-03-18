#pragma once

#include "core/Constants.h"
#include "audio/Transport.h"
#include "audio/ClipEngine.h"
#include "audio/Mixer.h"
#include "util/MessageQueue.h"
#include <portaudio.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace yawn {
namespace audio {

struct AudioEngineConfig {
    double sampleRate = 44100.0;
    int framesPerBuffer = 256;
    int outputChannels = 2;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init(const AudioEngineConfig& config = {});
    bool start();
    void stop();
    void shutdown();

    void sendCommand(const AudioCommand& cmd);
    bool pollEvent(AudioEvent& event);

    Transport& transport() { return m_transport; }
    const Transport& transport() const { return m_transport; }
    ClipEngine& clipEngine() { return m_clipEngine; }
    Mixer& mixer() { return m_mixer; }
    const Mixer& mixer() const { return m_mixer; }

    double sampleRate() const { return m_config.sampleRate; }
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

private:
    static int paCallback(
        const void* inputBuffer,
        void* outputBuffer,
        unsigned long framesPerBuffer,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData
    );

    void processAudio(float* output, unsigned long numFrames);
    void processCommands();
    void emitPositionUpdate();
    void emitClipStates();
    void emitMeterUpdates();

    struct TestTone {
        bool enabled = false;
        float frequency = 440.0f;
        double phase = 0.0;
    };

    AudioEngineConfig m_config;
    Transport m_transport;
    ClipEngine m_clipEngine;
    Mixer m_mixer;

    CommandQueue m_commandQueue;
    EventQueue m_eventQueue;

    PaStream* m_stream = nullptr;
    std::atomic<bool> m_running{false};
    bool m_paInitialized = false;

    TestTone m_testTone;
    int m_posUpdateCounter = 0;

    // Per-track scratch buffers for mixer routing (heap-allocated, preallocated in init)
    std::vector<float> m_trackBufferHeap;     // kMaxTracks * kMaxFramesPerBuffer * 2
    float* m_trackBufferPtrs[kMaxTracks] = {};
};

} // namespace audio
} // namespace yawn
