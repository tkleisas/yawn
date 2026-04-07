#pragma once

// MidiEngine — central MIDI coordinator.
// Manages MIDI ports, routes input to tracks, handles MPE zone management,
// and provides per-track MIDI buffers for the audio thread.

#include "core/Constants.h"
#include "midi/MidiTypes.h"
#include "midi/MidiPort.h"
#include "midi/MidiMonitorBuffer.h"
#include "midi/MidiMapping.h"
#include "util/MessageQueue.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace midi {

static constexpr int kMaxMidiPorts = 16;

// Track MIDI input routing
struct TrackMidiInput {
    enum class Source : uint8_t {
        None,       // No MIDI input
        AllPorts,   // Receive from all open input ports
        Port,       // Receive from a specific port
    };

    Source   source    = Source::AllPorts;
    int      portIndex = -1;     // Which port (when Source::Port)
    int      channel   = -1;     // -1 = all channels, 0-15 = specific channel
    bool     armed     = false;  // Track must be armed to receive live input
};

class MidiEngine {
public:
    MidiEngine() = default;
    ~MidiEngine() { shutdown(); }

    MidiEngine(const MidiEngine&) = delete;
    MidiEngine& operator=(const MidiEngine&) = delete;

    // ---- Lifecycle ----

    void init() {
        refreshPorts();
        for (auto& buf : m_trackBuffers) buf.clear();
        m_mergeBuffer.clear();
    }

    void shutdown() {
        for (auto& p : m_inputPorts) {
            if (p) p->close();
        }
        for (auto& p : m_outputPorts) {
            if (p) p->close();
        }
        m_inputPorts.clear();
        m_outputPorts.clear();
    }

    // ---- Port management ----

    void refreshPorts() {
        m_availableInputs  = MidiPort::enumerateInputPorts();
        m_availableOutputs = MidiPort::enumerateOutputPorts();
    }

    int availableInputCount() const { return static_cast<int>(m_availableInputs.size()); }
    int availableOutputCount() const { return static_cast<int>(m_availableOutputs.size()); }
    const std::string& availableInputName(int i) const { return m_availableInputs[i]; }
    const std::string& availableOutputName(int i) const { return m_availableOutputs[i]; }

    bool openInputPort(int portIndex) {
        if (portIndex < 0 || portIndex >= availableInputCount()) return false;
        auto port = std::make_unique<MidiPort>(MidiPort::Direction::Input);
        if (!port->open(portIndex)) return false;
        m_inputPorts.push_back(std::move(port));
        return true;
    }

    bool openOutputPort(int portIndex) {
        if (portIndex < 0 || portIndex >= availableOutputCount()) return false;
        auto port = std::make_unique<MidiPort>(MidiPort::Direction::Output);
        if (!port->open(portIndex)) return false;
        m_outputPorts.push_back(std::move(port));
        return true;
    }

    int openInputPortCount() const { return static_cast<int>(m_inputPorts.size()); }
    int openOutputPortCount() const { return static_cast<int>(m_outputPorts.size()); }

    // ---- Track routing ----

    void setTrackInput(int trackIndex, const TrackMidiInput& input) {
        if (trackIndex >= 0 && trackIndex < kMaxMidiTracks)
            m_trackInputs[trackIndex] = input;
    }

    const TrackMidiInput& trackInput(int trackIndex) const {
        static const TrackMidiInput kDefault{};
        if (trackIndex < 0 || trackIndex >= kMaxMidiTracks) return kDefault;
        return m_trackInputs[trackIndex];
    }

    void setTrackArmed(int trackIndex, bool armed) {
        if (trackIndex >= 0 && trackIndex < kMaxMidiTracks)
            m_trackInputs[trackIndex].armed = armed;
    }

    // ---- MPE Configuration ----

    MpeConfig& mpeConfig() { return m_mpeConfig; }
    const MpeConfig& mpeConfig() const { return m_mpeConfig; }

    // ---- Monitor ----

    void setMonitorBuffer(MidiMonitorBuffer* buf) { m_monitor = buf; }
    MidiMonitorBuffer* monitorBuffer() const { return m_monitor; }

    // ---- Audio-thread processing ----

    // Call once per audio buffer: collects all MIDI input and routes to per-track buffers
    void process(int /*numFrames*/) {
        // Clear all track buffers
        for (auto& buf : m_trackBuffers) buf.clear();

        // Collect input from all open ports into the merge buffer
        m_mergeBuffer.clear();
        for (int p = 0; p < static_cast<int>(m_inputPorts.size()); ++p) {
            auto& port = m_inputPorts[p];
            if (!port || !port->isOpen()) continue;

            int prevCount = m_mergeBuffer.count();
            port->readMessages(m_mergeBuffer);

            // Feed monitor buffer with port index
            if (m_monitor && m_monitor->enabled()) {
                uint32_t ts = m_monitor->elapsedMs();
                for (int i = prevCount; i < m_mergeBuffer.count(); ++i) {
                    m_monitor->push(makeMonitorEntry(m_mergeBuffer[i],
                                                      static_cast<uint8_t>(p), ts));
                }
            }
        }

        if (m_mergeBuffer.empty()) return;
        m_mergeBuffer.sortByFrame();

        // ── MIDI Learn: intercept CC and Note messages ──
        if (m_learnManager && m_sendCommand) {
            for (int i = 0; i < m_mergeBuffer.count(); ++i) {
                const auto& msg = m_mergeBuffer[i];

                if (msg.isCC()) {
                    int ch = msg.channel;
                    int cc = static_cast<int>(msg.ccNumber);
                    int val = static_cast<int>(Convert::cc32to7(msg.value));

                    if (m_learnManager->isLearning()) {
                        m_learnManager->handleLearnCC(ch, cc);
                        continue;
                    }

                    auto hits = m_learnManager->findByCC(ch, cc);
                    for (auto* mapping : hits) {
                        float paramVal = mapping->ccToParam(val);
                        resolveAndSend(mapping->target, paramVal);
                    }
                }
                else if (msg.isNoteOn() || msg.isNoteOff()) {
                    int ch = msg.channel;
                    int noteNum = msg.note;

                    if (m_learnManager->isLearning() && msg.isNoteOn()) {
                        m_learnManager->handleLearnNote(ch, noteNum);
                        continue;
                    }

                    auto hits = m_learnManager->findByNote(ch, noteNum);
                    for (auto* mapping : hits) {
                        float paramVal = mapping->noteToParam(msg.isNoteOn());
                        resolveAndSend(mapping->target, paramVal);
                    }
                }
            }
        }

        // Route messages to tracks based on track input configuration
        for (int t = 0; t < kMaxMidiTracks; ++t) {
            const auto& input = m_trackInputs[t];
            if (input.source == TrackMidiInput::Source::None) continue;
            if (!input.armed) continue;

            for (int i = 0; i < m_mergeBuffer.count(); ++i) {
                const auto& msg = m_mergeBuffer[i];
                if (!matchesRouting(msg, input)) continue;
                m_trackBuffers[t].addMessage(msg);
            }
        }
    }

    // Get the routed MIDI buffer for a track (valid after process())
    const MidiBuffer& trackBuffer(int trackIndex) const {
        if (trackIndex < 0 || trackIndex >= kMaxMidiTracks) {
            static const MidiBuffer kEmpty{};
            return kEmpty;
        }
        return m_trackBuffers[trackIndex];
    }

    // ---- Output ----

    void sendToAllOutputs(const MidiBuffer& buffer) {
        for (auto& port : m_outputPorts) {
            if (port && port->isOpen())
                port->sendBuffer(buffer);
        }
    }

    void sendToOutput(int portIndex, const MidiBuffer& buffer) {
        if (portIndex >= 0 && portIndex < static_cast<int>(m_outputPorts.size())) {
            if (m_outputPorts[portIndex] && m_outputPorts[portIndex]->isOpen())
                m_outputPorts[portIndex]->sendBuffer(buffer);
        }
    }

private:
    bool matchesRouting(const MidiMessage& msg, const TrackMidiInput& input) const {
        // Channel filter (-1 = all channels)
        if (input.channel >= 0 && msg.channel != input.channel) return false;

        // MPE awareness: if MPE is enabled, route member channel messages
        // to the track that owns the zone, regardless of per-channel filter
        if (m_mpeConfig.lowerZone.enabled && m_mpeConfig.lowerZone.isMemberChannel(msg.channel))
            return true;
        if (m_mpeConfig.upperZone.enabled && m_mpeConfig.upperZone.isMemberChannel(msg.channel))
            return true;

        return true; // Source matches (AllPorts or specific port already filtered upstream)
    }

    // Resolve an AutomationTarget to a command and send it
    void resolveAndSend(const automation::AutomationTarget& target, float value) {
        if (!m_sendCommand) return;
        using namespace automation;

        switch (target.type) {
        case TargetType::Mixer: {
            auto mp = static_cast<MixerParam>(target.paramIndex);
            int ti = target.trackIndex;
            if (ti == -1) {
                // Master channel
                if (mp == MixerParam::Volume)
                    m_sendCommand(audio::SetMasterVolumeMsg{value * 2.0f});
            } else if (ti <= -2 && ti >= -9) {
                // Return buses: index = -(ti + 2)
                int busIdx = -(ti + 2);
                if (mp == MixerParam::Volume)
                    m_sendCommand(audio::SetReturnVolumeMsg{busIdx, value * 2.0f});
                else if (mp == MixerParam::Pan)
                    m_sendCommand(audio::SetReturnPanMsg{busIdx, value * 2.0f - 1.0f});
            } else {
                // Regular tracks
                switch (mp) {
                case MixerParam::Volume:
                    m_sendCommand(audio::SetTrackVolumeMsg{ti, value * 2.0f});
                    break;
                case MixerParam::Pan:
                    m_sendCommand(audio::SetTrackPanMsg{ti, value * 2.0f - 1.0f});
                    break;
                case MixerParam::Mute:
                    m_sendCommand(audio::SetTrackMuteMsg{ti, value >= 0.5f});
                    break;
                default:
                    if (mp >= MixerParam::SendLevel0 && mp <= MixerParam::SendLevel7) {
                        int sendIdx = static_cast<int>(mp) - static_cast<int>(MixerParam::SendLevel0);
                        m_sendCommand(audio::SetSendLevelMsg{ti, sendIdx, value});
                    }
                    break;
                }
            }
            break;
        }
        case TargetType::AudioEffect:
            m_sendCommand(audio::SetEffectParamMsg{
                target.trackIndex, target.chainIndex, target.paramIndex, value, true});
            break;
        case TargetType::MidiEffect:
            m_sendCommand(audio::SetEffectParamMsg{
                target.trackIndex, target.chainIndex, target.paramIndex, value, false});
            break;
        case TargetType::Instrument:
            m_sendCommand(audio::SetEffectParamMsg{
                target.trackIndex, 0, target.paramIndex, value, false});
            break;
        case TargetType::Transport: {
            auto tp = static_cast<TransportParam>(target.paramIndex);
            switch (tp) {
            case TransportParam::BPM: {
                // Map 0-1 to 20-300 BPM range
                double bpm = 20.0 + static_cast<double>(value) * 280.0;
                m_sendCommand(audio::TransportSetBPMMsg{bpm});
                break;
            }
            case TransportParam::Play:
                if (value >= 0.5f)
                    m_sendCommand(audio::TransportPlayMsg{});
                break;
            case TransportParam::Stop:
                if (value >= 0.5f)
                    m_sendCommand(audio::TransportStopMsg{});
                break;
            case TransportParam::Record:
                if (value >= 0.5f)
                    m_sendCommand(audio::TransportRecordMsg{true, 0});
                else
                    m_sendCommand(audio::TransportRecordMsg{false, 0});
                break;
            }
            break;
        }
        }
    }

    // Available port names (refreshed by refreshPorts())
    std::vector<std::string> m_availableInputs;
    std::vector<std::string> m_availableOutputs;

    // Open ports
    std::vector<std::unique_ptr<MidiPort>> m_inputPorts;
    std::vector<std::unique_ptr<MidiPort>> m_outputPorts;

    // Per-track input routing
    std::array<TrackMidiInput, kMaxMidiTracks> m_trackInputs;

    // Per-track MIDI buffers (filled by process(), read by audio engine)
    std::array<MidiBuffer, kMaxMidiTracks> m_trackBuffers;

    // Scratch buffer for merging all port input
    MidiBuffer m_mergeBuffer;

    // MPE configuration
    MpeConfig m_mpeConfig;

    // Monitor (optional, owned externally)
    MidiMonitorBuffer* m_monitor = nullptr;

    // MIDI Learn (optional, owned externally)
    MidiLearnManager* m_learnManager = nullptr;
    std::function<void(const audio::AudioCommand&)> m_sendCommand;

public:
    void setLearnManager(MidiLearnManager* mgr) { m_learnManager = mgr; }
    MidiLearnManager* learnManager() const { return m_learnManager; }

    // Set callback for sending commands to AudioEngine (for MIDI-mapped params)
    void setCommandSender(std::function<void(const audio::AudioCommand&)> fn) {
        m_sendCommand = std::move(fn);
    }
};

} // namespace midi
} // namespace yawn
