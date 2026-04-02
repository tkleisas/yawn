#include "audio/AudioEngine.h"
#include "util/Logger.h"
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

std::vector<AudioDevice> AudioEngine::enumerateDevices() {
    std::vector<AudioDevice> devices;
    PaError err = Pa_Initialize();
    if (err != paNoError) return devices;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) continue;
        AudioDevice d;
        d.id = i;
        d.name = info->name ? info->name : "";
        d.maxInputChannels = info->maxInputChannels;
        d.maxOutputChannels = info->maxOutputChannels;
        d.defaultSampleRate = info->defaultSampleRate;
        devices.push_back(d);
    }
    return devices;
}

int AudioEngine::defaultOutputDevice() {
    Pa_Initialize();
    int dev = Pa_GetDefaultOutputDevice();
    return dev;
}

int AudioEngine::defaultInputDevice() {
    Pa_Initialize();
    int dev = Pa_GetDefaultInputDevice();
    return dev;
}

bool AudioEngine::init(const AudioEngineConfig& config) {
    m_config = config;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        LOG_ERROR("Audio", "PortAudio init failed: %s", Pa_GetErrorText(err));
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
    m_liveInputMidi.resize(kMaxTracks);

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

    // Allocate input buffer for deinterleaving
    m_inputBufferHeap.resize(config.inputChannels * kMaxFramesPerBuffer, 0.0f);

    // Open audio stream (full-duplex if input device available, output-only otherwise)
    PaStreamParameters outputParams;
    outputParams.device = (config.outputDevice >= 0)
        ? static_cast<PaDeviceIndex>(config.outputDevice)
        : Pa_GetDefaultOutputDevice();
    if (outputParams.device == paNoDevice) {
        LOG_ERROR("Audio", "No default audio output device found");
        return false;
    }

    outputParams.channelCount = config.outputChannels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency =
        Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    // Try to open input device for full-duplex recording
    PaStreamParameters inputParams;
    PaStreamParameters* inputParamsPtr = nullptr;
    PaDeviceIndex inputDev = (config.inputDevice >= 0)
        ? static_cast<PaDeviceIndex>(config.inputDevice)
        : Pa_GetDefaultInputDevice();

    if (inputDev != paNoDevice) {
        const PaDeviceInfo* inInfo = Pa_GetDeviceInfo(inputDev);
        if (inInfo && inInfo->maxInputChannels >= config.inputChannels) {
            inputParams.device = inputDev;
            inputParams.channelCount = config.inputChannels;
            inputParams.sampleFormat = paFloat32;
            inputParams.suggestedLatency = inInfo->defaultLowInputLatency;
            inputParams.hostApiSpecificStreamInfo = nullptr;
            inputParamsPtr = &inputParams;
        }
    }

    err = Pa_OpenStream(
        &m_stream,
        inputParamsPtr,  // nullptr if no input device
        &outputParams,
        config.sampleRate,
        config.framesPerBuffer,
        paClipOff,
        &AudioEngine::paCallback,
        this
    );

    if (err != paNoError && inputParamsPtr) {
        // Full-duplex failed, fall back to output-only
        LOG_WARN("Audio", "Full-duplex failed (%s), falling back to output-only",
                     Pa_GetErrorText(err));
        inputParamsPtr = nullptr;
        err = Pa_OpenStream(
            &m_stream,
            nullptr,
            &outputParams,
            config.sampleRate,
            config.framesPerBuffer,
            paClipOff,
            &AudioEngine::paCallback,
            this
        );
    }

    if (err != paNoError) {
        LOG_ERROR("Audio", "PortAudio open stream failed: %s", Pa_GetErrorText(err));
        return false;
    }

    m_hasInputDevice = (inputParamsPtr != nullptr);

    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(outputParams.device);
    LOG_INFO("Audio", "Output device: %s", devInfo->name);
    if (m_hasInputDevice) {
        const PaDeviceInfo* inDevInfo = Pa_GetDeviceInfo(inputParams.device);
        LOG_INFO("Audio", "Input device: %s (%d ch)", inDevInfo->name, config.inputChannels);
    } else {
        LOG_INFO("Audio", "No audio input device (recording disabled)");
    }
    LOG_INFO("Audio", "Sample rate: %.0f Hz, Buffer: %d frames, Channels: %d",
        config.sampleRate, config.framesPerBuffer, config.outputChannels);

    return true;
}

bool AudioEngine::start() {
    if (!m_stream) return false;

    PaError err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        LOG_ERROR("Audio", "PortAudio start stream failed: %s", Pa_GetErrorText(err));
        return false;
    }

    m_running.store(true, std::memory_order_release);
    LOG_INFO("Audio", "Audio engine started");
    return true;
}

void AudioEngine::stop() {
    if (m_stream && m_running.load(std::memory_order_acquire)) {
        Pa_StopStream(m_stream);
        m_running.store(false, std::memory_order_release);
        LOG_INFO("Audio", "Audio engine stopped");
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
    const void* inputBuffer,
    void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* /*timeInfo*/,
    PaStreamCallbackFlags /*statusFlags*/,
    void* userData)
{
    auto* engine = static_cast<AudioEngine*>(userData);
    auto* output = static_cast<float*>(outputBuffer);
    auto* input = static_cast<const float*>(inputBuffer);

    engine->processAudio(input, output, framesPerBuffer);
    return paContinue;
}

void AudioEngine::processAudio(const float* input, float* output, unsigned long numFrames) {
    // Process any pending commands from the UI thread
    // (also populates m_liveInputMidi for virtual keyboard MIDI)
    for (int t = 0; t < kMaxTracks; ++t) m_liveInputMidi[t].clear();
    processCommands();

    int nc = m_config.outputChannels;
    int nf = static_cast<int>(numFrames);
    int inCh = m_config.inputChannels;

    // Clear per-track buffers (pointers already set up in init)
    for (int t = 0; t < kMaxTracks; ++t) {
        std::memset(m_trackBufferPtrs[t], 0, nf * nc * sizeof(float));
    }

    // Route audio input to monitored tracks (add to track buffer for monitoring)
    // MonitorMode: 0=Auto (only when armed), 1=In (always), 2=Off (never)
    if (input && m_hasInputDevice) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] != 0) continue;
            bool shouldMonitor = (m_trackMonitorMode[t] == 1) ||
                                 (m_trackMonitorMode[t] == 0 && m_trackArmed[t]);
            if (!shouldMonitor) continue;
            int inputCh = m_trackAudioInputCh[t];
            if (inputCh <= 0) continue;
            float* dst = m_trackBufferPtrs[t];
            for (int f = 0; f < nf; ++f) {
                if (m_trackMono[t]) {
                    int srcIdx = ((inputCh - 1) / 2) * 2;
                    if (srcIdx >= inCh) srcIdx = 0;
                    float mono = (input[f * inCh + srcIdx] +
                                  (srcIdx + 1 < inCh ? input[f * inCh + srcIdx + 1] : input[f * inCh + srcIdx])) * 0.5f;
                    dst[f * nc + 0] += mono;
                    dst[f * nc + 1] += mono;
                } else if (inputCh % 2 == 1 && inputCh >= 3) {
                    int ch0 = inputCh - 2;
                    int ch1 = inputCh - 1;
                    if (ch0 < inCh) dst[f * nc + 0] += input[f * inCh + ch0];
                    if (ch1 < inCh) dst[f * nc + (nc > 1 ? 1 : 0)] += input[f * inCh + ch1];
                } else {
                    int ch0 = (inputCh - 1) < inCh ? (inputCh - 1) : 0;
                    int ch1 = inputCh < inCh ? inputCh : ch0;
                    dst[f * nc + 0] += input[f * inCh + ch0];
                    if (nc > 1) dst[f * nc + 1] += input[f * inCh + ch1];
                }
            }
        }
    }

    // Capture audio for recording (before any effects/mixing modify the buffer)
    if (m_transport.isRecording() && input && m_hasInputDevice) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] != 0) continue;
            auto& ars = m_audioRecordStates[t];
            if (!ars.recording || !m_trackArmed[t]) continue;

            int64_t remaining = ars.maxFrames - ars.recordedFrames;
            if (remaining <= 0) {
                // Buffer full — auto-finalize and notify UI
                RecordBufferFullEvent bfe;
                bfe.trackIndex = t;
                bfe.sceneIndex = ars.targetScene;
                bfe.frameCount = ars.recordedFrames;
                m_eventQueue.push(bfe);
                finalizeAudioRecord(t);
                continue;
            }
            int64_t framesToCopy = std::min(static_cast<int64_t>(nf), remaining);

            // Deinterleave input into non-interleaved record buffer
            for (int ch = 0; ch < ars.channels; ++ch) {
                int srcCh = (ch < inCh) ? ch : (inCh - 1);
                float* dst = ars.buffer.data() + ch * ars.maxFrames + ars.recordedFrames;
                for (int64_t f = 0; f < framesToCopy; ++f) {
                    dst[f] = input[f * inCh + srcCh];
                }
            }
            ars.recordedFrames += framesToCopy;
        }
    }

    // Render each track's clip into its own buffer
    m_clipEngine.checkAndFirePending();
    m_midiClipEngine.checkAndFirePending();
    checkPendingRecordStops(nf);
    for (int t = 0; t < kMaxTracks; ++t) {
        m_clipEngine.processTrackToBuffer(t, m_trackBufferPtrs[t], nf, nc);
    }
    // Generate MIDI from MIDI clips into track MIDI buffers
    m_midiClipEngine.process(m_trackMidiBuffers.data(), nf);

    // For recording tracks, clear clip MIDI so only live input goes through
    // effects and gets recorded (standard DAW behavior: clip playback is
    // suspended on the recording track).
    if (m_transport.isRecording()) {
        for (int t = 0; t < kMaxTracks; ++t) {
            if (m_trackType[t] == 1 && m_trackRecordStates[t].recording
                && m_trackArmed[t])
                m_trackMidiBuffers[t].clear();
        }
    }

    // Merge live MIDI input into track MIDI buffers
    // (hardware MIDI from MidiEngine + virtual keyboard from m_liveInputMidi)
    if (m_midiEngine) {
        m_midiEngine->process(nf);
        for (int t = 0; t < kMaxTracks; ++t) {
            const auto& liveBuf = m_midiEngine->trackBuffer(t);
            for (int i = 0; i < liveBuf.count(); ++i)
                m_trackMidiBuffers[t].addMessage(liveBuf[i]);
        }
    }
    for (int t = 0; t < kMaxTracks; ++t) {
        const auto& vkBuf = m_liveInputMidi[t];
        for (int i = 0; i < vkBuf.count(); ++i)
            m_trackMidiBuffers[t].addMessage(vkBuf[i]);
    }

    // Process instruments: MIDI effects → record post-effect → instrument
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

        // Capture post-effect MIDI for recording (arpeggiator output, etc.)
        if (m_transport.isRecording() && m_trackType[t] == 1) {
            auto& rs = m_trackRecordStates[t];
            if (rs.recording && m_trackArmed[t]) {
                double currentBeat = m_transport.positionInBeats();
                const auto& buf = m_trackMidiBuffers[t];
                for (int i = 0; i < buf.count(); ++i) {
                    const auto& msg = buf[i];
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
                        m_transport.isPlaying(),
                        m_transport.isRecording());

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
                // Finalize all active recordings before stopping transport
                for (int t = 0; t < kMaxTracks; ++t) {
                    if (m_trackRecordStates[t].recording)
                        finalizeMidiRecord(t);
                    if (m_audioRecordStates[t].recording)
                        finalizeAudioRecord(t);
                }
                m_transport.stop();
                // Stop all playing clips (audio and MIDI)
                for (int t = 0; t < kMaxTracks; ++t) {
                    m_clipEngine.scheduleStop(t, QuantizeMode::None);
                    if (m_midiClipEngine.isTrackPlaying(t)) {
                        m_midiClipEngine.scheduleStop(t, QuantizeMode::None);
                        for (uint8_t n = 0; n < 128; ++n) {
                            m_trackMidiBuffers[t].addMessage(
                                midi::MidiMessage::noteOff(0, n, 0, 0));
                        }
                    }
                }
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
                m_clipEngine.scheduleClip(msg.trackIndex, msg.sceneIndex, msg.clip, msg.quantize);
                if (!m_transport.isPlaying())
                    m_transport.play();
            }
            else if constexpr (std::is_same_v<T, StopClipMsg>) {
                m_clipEngine.scheduleStop(msg.trackIndex, QuantizeMode::NextBar);
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
            else if constexpr (std::is_same_v<T, MetronomeSetModeMsg>) {
                m_metronome.setMode(static_cast<MetronomeMode>(msg.mode));
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
                    // Store in live input buffer (merged into track buffer later)
                    m_liveInputMidi[msg.trackIndex].addMessage(m);
                    if (m.isNoteOn())
                        LOG_DEBUG("MIDI", "NoteOn track=%d note=%d vel=%d inst=%s",
                                    msg.trackIndex, msg.note, msg.velocity,
                                    m_instruments[msg.trackIndex] ? m_instruments[msg.trackIndex]->name() : "NONE");
                }
            }
            else if constexpr (std::is_same_v<T, LaunchMidiClipMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    LOG_DEBUG("MIDI", "LaunchMidiClip track=%d scene=%d clip=%p len=%.2f",
                                msg.trackIndex, msg.sceneIndex, (const void*)msg.clip,
                                msg.clip ? msg.clip->lengthBeats() : 0.0);
                    m_midiClipEngine.scheduleClip(msg.trackIndex, msg.sceneIndex, msg.clip, msg.quantize);
                    // Auto-start transport if not playing
                    if (!m_transport.isPlaying())
                        m_transport.play();
                }
            }
            else if constexpr (std::is_same_v<T, StopMidiClipMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_midiClipEngine.scheduleStop(msg.trackIndex, QuantizeMode::NextBar);
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
                    // Auto-start per-track recording for all armed MIDI tracks
                    for (int t = 0; t < kMaxTracks; ++t) {
                        if (m_trackArmed[t] && m_trackType[t] == 1 &&
                            !m_trackRecordStates[t].recording) {
                            auto& rs = m_trackRecordStates[t];
                            rs.reset();
                            rs.recording = true;
                            rs.targetScene = msg.sceneIndex;
                            rs.overdub = false;
                            rs.recordStartBeat = m_transport.positionInBeats();
                        }
                    }
                } else {
                    // Gracefully stop all active recordings respecting per-track quantize
                    for (int t = 0; t < kMaxTracks; ++t) {
                        if (m_trackRecordStates[t].recording) {
                            if (m_trackRecordStates[t].pendingStopQuantize == QuantizeMode::None) {
                                // No pending stop yet — finalize immediately
                                finalizeMidiRecord(t);
                            }
                            // else: already has a pending quantized stop, let it complete naturally
                        }
                        if (m_audioRecordStates[t].recording) {
                            if (m_audioRecordStates[t].pendingStopQuantize == QuantizeMode::None) {
                                finalizeAudioRecord(t);
                            }
                        }
                    }
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
            else if constexpr (std::is_same_v<T, SetTrackMonitorMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackMonitorMode[msg.trackIndex] = msg.mode;
                }
            }
            else if constexpr (std::is_same_v<T, StartMidiRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks
                    && m_trackType[msg.trackIndex] == 1) { // Midi only
                    auto& rs = m_trackRecordStates[msg.trackIndex];
                    rs.reset();
                    rs.recording = true;
                    rs.targetScene = msg.sceneIndex;
                    rs.overdub = msg.overdub;
                    rs.recordStartBeat = m_transport.positionInBeats();

                    // Auto-arm transport recording and start playback
                    if (!m_transport.isRecording()) {
                        m_transport.startRecording();
                        if (!m_transport.isPlaying())
                            m_transport.play();
                    }
                }
            }
            else if constexpr (std::is_same_v<T, StopMidiRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    auto& rs = m_trackRecordStates[msg.trackIndex];
                    if (rs.recording) {
                        if (msg.quantize == QuantizeMode::None) {
                            finalizeMidiRecord(msg.trackIndex);
                        } else {
                            rs.pendingStopQuantize = msg.quantize;
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, StartAudioRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks
                    && m_trackType[msg.trackIndex] == 0) { // Audio only
                    auto& ars = m_audioRecordStates[msg.trackIndex];
                    ars.reset();
                    ars.recording = true;
                    ars.targetScene = msg.sceneIndex;
                    ars.overdub = msg.overdub;
                    ars.channels = m_config.inputChannels;
                    // Pre-allocate for 5 minutes of recording
                    static constexpr int64_t kMaxRecordSec = 300;
                    ars.maxFrames = static_cast<int64_t>(m_config.sampleRate * kMaxRecordSec);
                    ars.buffer.resize(ars.channels * ars.maxFrames, 0.0f);
                    ars.recordedFrames = 0;

                    // Auto-arm transport recording and start playback
                    if (!m_transport.isRecording()) {
                        m_transport.startRecording();
                        if (!m_transport.isPlaying())
                            m_transport.play();
                    }
                }
            }
            else if constexpr (std::is_same_v<T, StopAudioRecordMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    auto& ars = m_audioRecordStates[msg.trackIndex];
                    if (ars.recording) {
                        if (msg.quantize == QuantizeMode::None) {
                            finalizeAudioRecord(msg.trackIndex);
                        } else {
                            ars.pendingStopQuantize = msg.quantize;
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackTypeMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    int t = msg.trackIndex;
                    // Auto-stop any active recording when type changes
                    if (m_audioRecordStates[t].recording)
                        finalizeAudioRecord(t);
                    if (m_trackRecordStates[t].recording)
                        finalizeMidiRecord(t);
                    m_trackType[t] = msg.type;
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackAudioInputChMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackAudioInputCh[msg.trackIndex] = msg.channel;
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackMonoMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackMono[msg.trackIndex] = msg.mono;
                }
            }
            else if constexpr (std::is_same_v<T, SetTrackMidiOutputMsg>) {
                if (msg.trackIndex >= 0 && msg.trackIndex < kMaxTracks) {
                    m_trackMidiOutPort[msg.trackIndex] = msg.portIndex;
                    m_trackMidiOutCh[msg.trackIndex] = msg.channel;
                }
            }
        }, cmd);
    }
}

void AudioEngine::checkPendingRecordStops(int bufferSize) {
    if (!m_transport.isPlaying()) return;

    int64_t pos = m_transport.positionInSamples();
    int64_t prevPos = pos - bufferSize;
    double spb = m_transport.samplesPerBar();
    double spBeat = m_transport.samplesPerBeat();

    for (int t = 0; t < kMaxTracks; ++t) {
        auto& rs = m_trackRecordStates[t];
        if (rs.recording && rs.pendingStopQuantize != QuantizeMode::None) {
            double interval = (rs.pendingStopQuantize == QuantizeMode::NextBar) ? spb : spBeat;
            if (interval <= 0.0) continue;
            int64_t iInterval = static_cast<int64_t>(interval);
            if (iInterval <= 0) continue;
            if ((pos / iInterval) != (prevPos / iInterval)) {
                finalizeMidiRecord(t);
            }
        }

        auto& ars = m_audioRecordStates[t];
        if (ars.recording && ars.pendingStopQuantize != QuantizeMode::None) {
            double interval = (ars.pendingStopQuantize == QuantizeMode::NextBar) ? spb : spBeat;
            if (interval <= 0.0) continue;
            int64_t iInterval = static_cast<int64_t>(interval);
            if (iInterval <= 0) continue;
            if ((pos / iInterval) != (prevPos / iInterval)) {
                finalizeAudioRecord(t);
            }
        }
    }
}

void AudioEngine::finalizeMidiRecord(int trackIndex) {
    auto& rs = m_trackRecordStates[trackIndex];
    if (!rs.recording) return;

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

    // Reset MIDI effect chain to clear arpeggiator state and prevent stuck notes
    m_midiEffectChains[trackIndex].reset();

    // Send all-notes-off to the instrument to silence any lingering arp notes
    if (m_instruments[trackIndex]) {
        midi::MidiBuffer killBuf;
        midi::MidiMessage allOff{};
        allOff.type = midi::MidiMessage::Type::ControlChange;
        allOff.ccNumber = 123; // All Notes Off
        allOff.value = 0;
        allOff.channel = 0;
        killBuf.addMessage(allOff);
        float silence[512] = {};
        m_instruments[trackIndex]->process(silence, 1, 2, killBuf);
    }

    // Discard empty MIDI recordings (no notes and no CCs)
    if (rs.recordedNotes.empty() && rs.recordedCCs.empty()) {
        // Emit event with noteCount=0 so UI clears recording state
        auto& xfer = m_recordedMidi[trackIndex];
        xfer.trackIndex = trackIndex;
        xfer.sceneIndex = rs.targetScene;
        xfer.notes.clear();
        xfer.ccs.clear();
        xfer.lengthBeats = 0.0;
        xfer.ready.store(true, std::memory_order_release);

        MidiRecordCompleteEvent evt;
        evt.trackIndex = trackIndex;
        evt.sceneIndex = rs.targetScene;
        evt.noteCount = 0;
        m_eventQueue.push(evt);

        rs.recording = false;
        rs.pendingStopQuantize = QuantizeMode::None;
        maybeStopTransportRecording();
        return;
    }

    auto& xfer = m_recordedMidi[trackIndex];
    xfer.notes = rs.recordedNotes;
    xfer.ccs = rs.recordedCCs;
    xfer.trackIndex = trackIndex;
    xfer.sceneIndex = rs.targetScene;
    xfer.overdub = rs.overdub;
    double rawLen = m_transport.positionInBeats() - rs.recordStartBeat;
    int bpb = m_transport.beatsPerBar();
    double bars = std::ceil(rawLen / bpb);
    xfer.lengthBeats = bars * bpb;
    xfer.ready.store(true, std::memory_order_release);

    MidiRecordCompleteEvent evt;
    evt.trackIndex = trackIndex;
    evt.sceneIndex = rs.targetScene;
    evt.noteCount = static_cast<int>(rs.recordedNotes.size());
    m_eventQueue.push(evt);

    rs.recording = false;
    rs.pendingStopQuantize = QuantizeMode::None;
    maybeStopTransportRecording();
}

void AudioEngine::finalizeAudioRecord(int trackIndex) {
    auto& ars = m_audioRecordStates[trackIndex];
    // Minimum ~50ms at any sample rate
    static constexpr int64_t kMinRecordFrames = 2048;
    if (!ars.recording || ars.recordedFrames < kMinRecordFrames) {
        // Emit event with frameCount=0 so UI clears recording state
        auto& xfer = m_recordedAudio[trackIndex];
        xfer.trackIndex = trackIndex;
        xfer.sceneIndex = ars.targetScene;
        xfer.frameCount = 0;
        xfer.buffer.clear();
        xfer.ready.store(true, std::memory_order_release);

        AudioRecordCompleteEvent evt;
        evt.trackIndex = trackIndex;
        evt.sceneIndex = ars.targetScene;
        evt.frameCount = 0;
        m_eventQueue.push(evt);

        ars.buffer.clear();
        ars.buffer.shrink_to_fit();
        ars.recording = false;
        ars.pendingStopQuantize = QuantizeMode::None;
        maybeStopTransportRecording();
        return;
    }

    auto& xfer = m_recordedAudio[trackIndex];
    xfer.channels = ars.channels;
    xfer.frameCount = ars.recordedFrames;
    xfer.trackIndex = trackIndex;
    xfer.sceneIndex = ars.targetScene;
    xfer.overdub = ars.overdub;
    xfer.buffer.resize(ars.channels * ars.recordedFrames);
    for (int ch = 0; ch < ars.channels; ++ch) {
        std::memcpy(
            xfer.buffer.data() + ch * ars.recordedFrames,
            ars.buffer.data() + ch * ars.maxFrames,
            ars.recordedFrames * sizeof(float));
    }
    xfer.ready.store(true, std::memory_order_release);

    AudioRecordCompleteEvent evt;
    evt.trackIndex = trackIndex;
    evt.sceneIndex = ars.targetScene;
    evt.frameCount = ars.recordedFrames;
    evt.overdub = ars.overdub;
    m_eventQueue.push(evt);

    ars.buffer.clear();
    ars.buffer.shrink_to_fit();
    ars.recording = false;
    ars.pendingStopQuantize = QuantizeMode::None;
    maybeStopTransportRecording();
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

void AudioEngine::maybeStopTransportRecording() {
    if (!m_transport.isRecording()) return;
    for (int t = 0; t < kMaxTracks; ++t) {
        if (m_trackRecordStates[t].recording || m_audioRecordStates[t].recording)
            return; // At least one track still recording
    }
    m_transport.stopRecording();
}

void AudioEngine::emitClipStates() {
    for (int t = 0; t < kMaxTracks; ++t) {
        bool midiRec = m_trackRecordStates[t].recording;
        bool audioRec = m_audioRecordStates[t].recording;

        // Safety: only one recording type should be active per track
        if (midiRec && audioRec) {
            LOG_ERROR("Audio", "Track %d has BOTH midi and audio recording!", t);
            if (m_trackType[t] == 1)
                { finalizeAudioRecord(t); audioRec = false; }
            else
                { finalizeMidiRecord(t); midiRec = false; }
        }

        int midiRecScene = midiRec ? m_trackRecordStates[t].targetScene : -1;
        int audioRecScene = audioRec ? m_audioRecordStates[t].targetScene : -1;

        // Audio clips
        const auto& state = m_clipEngine.trackState(t);
        if (state.clip || audioRec) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = state.active;
            csu.playPosition = state.playPosition;
            csu.playingScene = state.sceneIndex;
            csu.recording = audioRec;
            csu.recordingScene = audioRecScene;
            m_eventQueue.push(csu);
        }
        // MIDI clips
        const auto& mstate = m_midiClipEngine.trackState(t);
        if (mstate.clip || midiRec) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = mstate.active;
            csu.playPosition = static_cast<int64_t>(mstate.playPositionBeats * 1000000.0);
            csu.playingScene = mstate.sceneIndex;
            csu.isMidi = true;
            csu.clipLengthBeats = mstate.clip ? mstate.clip->lengthBeats() : 0.0;
            csu.recording = midiRec;
            csu.recordingScene = midiRecScene;
            m_eventQueue.push(csu);
        }

        // Emit recording state even when no clip is loaded
        if (!state.clip && !mstate.clip && (midiRec || audioRec)) {
            ClipStateUpdate csu;
            csu.trackIndex = t;
            csu.playing = false;
            csu.playPosition = 0;
            csu.isMidi = midiRec;
            csu.recording = true;
            csu.recordingScene = midiRec ? midiRecScene : audioRecScene;
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
