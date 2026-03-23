#include "audio/AudioEngine.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace audio {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(const AudioEngineConfig& config) {
    m_config = config;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
        return false;
    }
    m_paInitialized = true;

    m_transport.setSampleRate(config.sampleRate);
    m_transport.reset();

    m_clipEngine.setTransport(&m_transport);
    m_clipEngine.setSampleRate(config.sampleRate);

    m_midiClipEngine.setTransport(&m_transport);
    m_midiClipEngine.setSampleRate(config.sampleRate);

    m_metronome.init(config.sampleRate, config.framesPerBuffer);

    // Preallocate per-track MIDI buffers on heap
    m_trackMidiBuffers.resize(kMaxTracks);

    for (int t = 0; t < kMaxMidiTracks; ++t)
        m_midiEffectChains[t].init(config.sampleRate);

    // Allocate per-track scratch buffers on heap
    const int bufferStride = kMaxFramesPerBuffer * config.outputChannels;
    m_trackBufferHeap.resize(kMaxTracks * bufferStride, 0.0f);
    for (int t = 0; t < kMaxTracks; ++t) {
        m_trackBufferPtrs[t] = m_trackBufferHeap.data() + t * bufferStride;
    }

    // Pre-reserve recording buffers to minimise audio-thread allocations
    for (int t = 0; t < kMaxTracks; ++t) {
        m_trackRecordStates[t].pendingNotes.reserve(128);
        m_trackRecordStates[t].recordedNotes.reserve(4096);
        m_trackRecordStates[t].recordedCCs.reserve(1024);
    }

    // Open default output stream
    PaStreamParameters outputParams;
    outputParams.device = Pa_GetDefaultOutputDevice();
    if (outputParams.device == paNoDevice) {
        std::fprintf(stderr, "No default audio output device found\n");
        return false;
    }

    outputParams.channelCount = config.outputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency =
        Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(
        &m_stream,
        nullptr,         // no input
        &outputParams,
        config.sampleRate,
        config.framesPerBuffer,
        paClipOff,
        &AudioEngine::paCallback,
        this
    );

    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio open stream failed: %s\n", Pa_GetErrorText(err));
        return false;
    }

    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(outputParams.device);
    std::printf("Audio device: %s\n", devInfo->name);
    std::printf("Sample rate: %.0f Hz, Buffer: %d frames, Channels: %d\n",
        config.sampleRate, config.framesPerBuffer, config.outputChannels);

    return true;
}

bool AudioEngine::start() {
    if (!m_stream) return false;

    PaError err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio start stream failed: %s\n", Pa_GetErrorText(err));
        return false;
    }

    m_running.store(true, std::memory_order_release);
    std::printf("Audio engine started\n");
    return true;
}

void AudioEngine::stop() {
    if (m_stream && m_running.load(std::memory_order_acquire)) {
        Pa_StopStream(m_stream);
        m_running.store(false, std::memory_order_release);
        std::printf("Audio engine stopped\n");
    }
}

void AudioEngine::shutdown() {
    stop();

    if (m_stream) {
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }

    if (m_paInitialized) {
        Pa_Terminate();
        m_paInitialized = false;
    }
}

void AudioEngine::sendCommand(const AudioCommand& cmd) {
    m_commandQueue.push(cmd);
}

bool AudioEngine::pollEvent(AudioEvent& event) {
    return m_eventQueue.pop(event);
}

// --- PortAudio callback (audio thread) ---

int AudioEngine::paCallback(
    const void* /*inputBuffer*/,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* /*timeInfo*/,
    PaStreamCallbackFlags /*statusFlags*/,
    void* userData)
{
    auto* engine = static_cast<AudioEngine*>(userData);
    auto* output = static_cast<float*>(outputBuffer);

    engine->processAudio(output, framesPerBuffer);
    return paContinue;
}

void AudioEngine::processAudio(float* output, unsigned long numFrames) {
    // Process any pending commands from the UI thread
    processCommands();

    int nc = m_config.outputChannels;
    int nf = static_cast<int>(numFrames);

    // Clear per-track buffers (pointers already set up in init)
    for (int t = 0; t < kMaxTracks; ++t) {
        std::memset(m_trackBufferPtrs[t], 0, nf * nc * sizeof(float));
    }

    // Render each track's clip into its own buffer
    m_clipEngine.checkAndFirePending();
    m_midiClipEngine.checkAndFirePending();
    for (int t = 0; t < kMaxTracks; ++t) {
        m_clipEngine.processTrackToBuffer(t, m_trackBufferPtrs[t], nf, nc);
    }
    // Generate MIDI from MIDI clips into track MIDI buffers
    m_midiClipEngine.process(m_trackMidiBuffers.data(), nf);

    // Merge live MIDI input from MidiEngine into track MIDI buffers
    if (m_midiEngine) {
        m_midiEngine->process(nf);
        for (int t = 0; t < kMaxTracks; ++t) {
            const auto& liveBuf = m_midiEngine->trackBuffer(t);
            for (int i = 0; i < liveBuf.count(); ++i) {
                m_trackMidiBuffers[t].addMessage(liveBuf[i]);
            }
        }
    }

    // Capture MIDI for recording
    if (m_transport.isRecording()) {
        double currentBeat = m_transport.positionInBeats();
        for (int t = 0; t < kMaxTracks; ++t) {
            auto& rs = m_trackRecordStates[t];
            if (!rs.recording || !m_trackArmed[t]) continue;

            if (m_midiEngine) {
                const auto& liveBuf = m_midiEngine->trackBuffer(t);
                for (int i = 0; i < liveBuf.count(); ++i) {
                    const auto& msg = liveBuf[i];
                    double msgBeat = currentBeat +
                        (static_cast<double>(msg.frameOffset) / m_config.sampleRate) *
                        (m_transport.bpm() / 60.0);
                    double relBeat = msgBeat - rs.recordStartBeat;
                    if (relBeat < 0) relBeat = 0;

                    if (msg.type == midi::MidiMessage::Type::NoteOn && msg.velocity > 0) {
                        TrackRecordState::PendingNote pn;
                        pn.pitch = msg.note;
                        pn.channel = msg.channel;
                        pn.velocity = msg.velocity;
                        pn.startBeat = relBeat;
                        rs.pendingNotes.push_back(pn);
                    }
                    else if (msg.type == midi::MidiMessage::Type::NoteOff ||
                             (msg.type == midi::MidiMessage::Type::NoteOn && msg.velocity == 0)) {
                        for (auto it = rs.pendingNotes.begin(); it != rs.pendingNotes.end(); ++it) {
                            if (it->pitch == msg.note && it->channel == msg.channel) {
                                midi::MidiNote note;
                                note.startBeat = it->startBeat;
                                note.duration = std::max(0.01, relBeat - it->startBeat);
                                note.pitch = it->pitch;
                                note.channel = it->channel;
                                note.velocity = static_cast<uint16_t>(it->velocity * 512);
                                if (note.velocity == 0) note.velocity = 1;
                                rs.recordedNotes.push_back(note);
                                rs.pendingNotes.erase(it);
                                break;
                            }
                        }
                    }
                    else if (msg.type == midi::MidiMessage::Type::ControlChange) {
                        midi::MidiCCEvent cc;
                        cc.beat = relBeat;
                        cc.ccNumber = msg.ccNumber;
                        cc.value = static_cast<uint32_t>(msg.value) << 25;
                        cc.channel = msg.channel;
                        rs.recordedCCs.push_back(cc);
                    }
                }
            }
        }
    }

    // Process instruments: MIDI effects → instrument → add to track buffer
    for (int t = 0; t < kMaxTracks; ++t) {
        if (!m_instruments[t]) {
            m_trackMidiBuffers[t].clear();
            continue;
        }
        // Run MIDI effect chain on this track's MIDI buffer
        if (m_midiEffectChains[t].count() > 0) {
            midi::TransportInfo ti;
            ti.bpm = m_transport.bpm();
            ti.positionInBeats = m_transport.positionInBeats();
            ti.positionInSamples = m_transport.positionInSamples();
            ti.sampleRate = m_config.sampleRate;
            ti.samplesPerBeat = ti.sampleRate * 60.0 / ti.bpm;
            ti.playing = m_transport.isPlaying();
            ti.beatsPerBar = m_transport.beatsPerBar();
            ti.beatDenominator = m_transport.denominator();
            m_midiEffectChains[t].process(m_trackMidiBuffers[t], nf, ti);
        }
        // Render instrument into track buffer (adds to existing audio)
        m_instruments[t]->process(m_trackBufferPtrs[t], nf, nc,
                                  m_trackMidiBuffers[t]);
        m_trackMidiBuffers[t].clear();
    }

    // Run mixer: per-track fader/pan/mute/solo, sends→returns, master
    // Mixer clears output before writing
    m_mixer.process(m_trackBufferPtrs, kMaxTracks, output, nf, nc);

    // Metronome: add click directly to master output (bypasses mixer routing)
    m_metronome.process(output, nf, nc,
                        m_transport.bpm(),
                        m_transport.positionInSamples(),
                        m_transport.isPlaying());

    // Add test tone on top of mixer output (bypasses mixer routing)
    if (m_testTone.enabled) {
        double phaseInc = 2.0 * M_PI * m_testTone.frequency / m_config.sampleRate;
        for (int i = 0; i < nf; ++i) {
            float sample = static_cast<float>(std::sin(m_testTone.phase)) * 0.3f;
            for (int ch = 0; ch < nc; ++ch) {
                output[i * nc + ch] += sample;
            }
            m_testTone.phase += phaseInc;
            if (m_testTone.phase >= 2.0 * M_PI) {
                m_testTone.phase -= 2.0 * M_PI;
            }
        }
    }

    // Advance transport
    m_transport.advance(nf);

    // Periodically send position updates to the UI (~30 Hz)
    m_posUpdateCounter += nf;
    int updateInterval = static_cast<int>(m_config.sampleRate / 30.0);
    if (m_posUpdateCounter >= updateInterval) {
        m_posUpdateCounter -= updateInterval;
        emitPositionUpdate();
        emitClipStates();
        emitMeterUpdates();
    }
}

void AudioEngine::processCommands() {
    AudioCommand cmd;
    while (m_commandQueue.pop(cmd)) {
        std::visit([this](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;

            if constexpr (std::is_same_v<T, TransportPlayMsg>) {
                m_transport.play();
            }
            else if constexpr (std::is_same_v<T, TransportStopMsg>) {
                m_transport.stop();
            }
            else if constexpr (std::is_same_v<T, TransportSetBPMMsg>) {
                m_transport.setBPM(msg.bpm);
            }
            else if constexpr (std::is_same_v<T, TransportSetTimeSignatureMsg>) {
                m_transport.setTimeSignature(msg.numerator, msg.denominator);
                m_metronome.setBeatsPerBar(msg.numerator);
            }
            else if constexpr (std::is_same_v<T, TransportSetPositionMsg>) {
                m_transport.setPositionInSamples(msg.positionInSamples);
            }
            else if constexpr (std::is_same_v<T, TestToneMsg>) {
                m_testTone.enabled = msg.enabled;
                m_testTone.frequency = msg.frequency;
                m_testTone.phase = 0.0;
            }
            else if constexpr (std::is_same_v<T, LaunchClipMsg>) {
                m_clipEngine.scheduleClip(msg.trackIndex, msg.clip);
            }
            else if constexpr (std::is_same_v<T, StopClipMsg>) {
                m_clipEngine.scheduleStop(msg.trackIndex);
            }
            else if constexpr (std::is_same_v<T, SetQuantizeMsg>) {
                m_clipEngine.setQuantizeMode(msg.mode);
            }
            else if constexpr (std::is_same_v<T, SetTrackVolumeMsg>) {
                m_mixer.setTrackVolume(msg.trackIndex, msg.volume);
            }
            else if constexpr (std::is_same_v<T, SetTrackPanMsg>) {
                m_mixer.setTrackPan(msg.trackIndex, msg.pan);
            }
            else if constexpr (std::is_same_v<T, SetTrackMuteMsg>) {
                m_mixer.setTrackMute(msg.trackIndex, msg.muted);
            }
            else if constexpr (std::is_same_v<T, SetTrackSoloMsg>) {
                m_mixer.setTrackSolo(msg.trackIndex, msg.soloed);
            }
            else if constexpr (std::is_same_v<T, SetMasterVolumeMsg>) {
                m_mixer.setMasterVolume(msg.volume);
            }
            else if constexpr (std::is_same_v<T, SetSendLevelMsg>) {
                m_mixer.setSendLevel(msg.trackIndex, msg.sendIndex, msg.level);
            }
            else if constexpr (std::is_same_v<T, SetSendModeMsg>) {
                m_mixer.setSendMode(msg.trackIndex, msg.sendIndex,
                    msg.mode == 0 ? SendMode::PreFader : SendMode::PostFader);
            }
            else if constexpr (std::is_same_v<T, SetSendEnabledMsg>) {
                m_mixer.setSendEnabled(msg.trackIndex, msg.sendIndex, msg.enabled);
            }
            else if constexpr (std::is_same_v<T, SetReturnVolumeMsg>) {
                m_mixer.setReturnVolume(msg.busIndex, msg.volume);
            }
            else if constexpr (std::is_same_v<T, SetReturnPanMsg>) {
                m_mixer.setReturnPan(msg.busIndex, msg.pan);
            }
            else if constexpr (std::is_same_v<T, SetReturnMuteMsg>) {
                m_mixer.setReturnMute(msg.busIndex, msg.muted);
            }
            else if constexpr (std::is_same_v<T, MetronomeToggleMsg>) {
                m_metronome.setEnabled(msg.enabled);
            }
            else if constexpr (std::is_same_v<T, MetronomeSetVolumeMsg>) {
                m_metronome.setVolume(msg.volume);
            }
            else if constexpr (std::is_same_v<T, MetronomeSetBeatsPerBarMsg>) {
                m_metronome.setBeatsPerBar(msg.beatsPerBar);
            }
            else if constexpr (std::is_same_v<T, SendMidiToTrackMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    midi::MidiMessage m{};
                    m.type = static_cast<midi::MidiMessage::Type>(msg.type);
                    m.channel = msg.channel;
                    m.note = msg.note;
                    m.velocity = msg.velocity;
                    m.value = msg.value;
                    m.ccNumber = msg.ccNumber;
                    m_trackMidiBuffers[msg.trackIndex].addMessage(m);
                }
            }
            else if constexpr (std::is_same_v<T, LaunchMidiClipMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_midiClipEngine.scheduleClip(msg.trackIndex, msg.clip);
                }
            }
            else if constexpr (std::is_same_v<T, StopMidiClipMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_midiClipEngine.scheduleStop(msg.trackIndex);
                    // Send all-notes-off to clean up any held notes
                    for (uint8_t n = 0; n < 128; ++n) {
                        m_trackMidiBuffers[msg.trackIndex].addMessage(
                            midi::MidiMessage::noteOff(0, n, 0, 0));
                    }
                }
            }
            else if constexpr (std::is_same_v<T, TransportRecordMsg>) {
                if (msg.arm) {
                    m_transport.startRecording();
                    if (m_transport.countInBars() > 0 && !m_transport.isPlaying()) {
                        m_transport.beginCountIn();
                        m_transport.play();
                    } else if (!m_transport.isPlaying()) {
                        m_transport.play();
                    }
                } else {
                    m_transport.stopRecording();
                }
            }
            else if constexpr (std::is_same_v<T, TransportSetCountInMsg>) {
                m_transport.setCountInBars(msg.bars);
            }
            else if constexpr (std::is_same_v<T, SetTrackArmedMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackArmed[msg.trackIndex] = msg.armed;
                    if (m_midiEngine) m_midiEngine->setTrackArmed(msg.trackIndex, msg.armed);
                }
            }
            else if constexpr (std::is_same_v<T, StartMidiRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    auto& rs = m_trackRecordStates[msg.trackIndex];
                    rs.reset();
                    rs.recording = true;
                    rs.targetScene = msg.sceneIndex;
                    rs.overdub = msg.overdub;
                    rs.recordStartBeat = m_transport.positionInBeats();
                }
            }
            else if constexpr (std::is_same_v<T, StopMidiRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    auto& rs = m_trackRecordStates[msg.trackIndex];
                    if (rs.recording) {
                        // Close any pending notes at current position
                        double endBeat = m_transport.positionInBeats() - rs.recordStartBeat;
                        for (auto& pn : rs.pendingNotes) {
                            midi::MidiNote note;
                            note.startBeat = pn.startBeat;
                            note.duration = std::max(0.01, endBeat - pn.startBeat);
                            note.pitch = pn.pitch;
                            note.channel = pn.channel;
                            note.velocity = static_cast<uint16_t>(pn.velocity * 512);
                            if (note.velocity == 0) note.velocity = 1;
                            rs.recordedNotes.push_back(note);
                        }
                        rs.pendingNotes.clear();

                        // Transfer to UI-readable buffer
                        m_recordedMidi.notes = rs.recordedNotes;
                        m_recordedMidi.ccs = rs.recordedCCs;
                        m_recordedMidi.trackIndex = msg.trackIndex;
                        m_recordedMidi.sceneIndex = rs.targetScene;
                        m_recordedMidi.overdub = rs.overdub;
                        double rawLen = m_transport.positionInBeats() - rs.recordStartBeat;
                        int bpb = m_transport.beatsPerBar();
                        double bars = std::ceil(rawLen / bpb);
                        m_recordedMidi.lengthBeats = bars * bpb;
                        m_recordedMidi.ready.store(true, std::memory_order_release);

                        // Emit completion event
                        MidiRecordCompleteEvent evt;
                        evt.trackIndex = msg.trackIndex;
                        evt.sceneIndex = rs.targetScene;
                        evt.noteCount = static_cast<int>(rs.recordedNotes.size());
                        m_eventQueue.push(evt);

                        rs.recording = false;
                    }
                }
            }
        }, cmd);
    }
}

void AudioEngine::emitPositionUpdate() {
    TransportPositionUpdate update;
    update.positionInSamples = m_transport.positionInSamples();
    update.positionInBeats = m_transport.positionInBeats();
    update.isPlaying = m_transport.isPlaying();
    m_eventQueue.push(update);

    // Also emit record state
    if (m_transport.isRecording() || m_transport.isCountingIn()) {
        TransportRecordStateUpdate rsu;
        rsu.recording = m_transport.isRecording();
        rsu.countingIn = m_transport.isCountingIn();
        rsu.countInProgress = m_transport.countInProgress();
        m_eventQueue.push(rsu);
    }
}

void AudioEngine::emitClipStates() {
    for (int t = 0; t < kMaxTracks; ++t) {
        const auto& state = m_clipEngine.trackState(t);
        if (state.clip) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = state.active;
            csu.playPosition = state.playPosition;
            m_eventQueue.push(csu);
        }
    }
}

void AudioEngine::emitMeterUpdates() {
    // Track meters (only emit for tracks with non-trivial peaks)
    for (int t = 0; t < kMaxTracks; ++t) {
        const auto& ch = m_mixer.trackChannel(t);
        if (ch.peakL > 0.001f || ch.peakR > 0.001f) {
            m_eventQueue.push(MeterUpdate{t, ch.peakL, ch.peakR});
        }
    }

    // Return bus meters: encoded as -2..-5
    for (int r = 0; r < kMaxReturnBuses; ++r) {
        const auto& rb = m_mixer.returnBus(r);
        if (rb.peakL > 0.001f || rb.peakR > 0.001f) {
            m_eventQueue.push(MeterUpdate{-(r + 2), rb.peakL, rb.peakR});
        }
    }

    // Master meter: encoded as -1
    const auto& master = m_mixer.master();
    if (master.peakL > 0.001f || master.peakR > 0.001f) {
        m_eventQueue.push(MeterUpdate{-1, master.peakL, master.peakR});
    }
}

} // namespace audio
} // namespace yawn
