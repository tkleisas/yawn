#pragma once

// MidiPort — cross-platform MIDI I/O wrapper.
// Uses RtMidi for MIDI 1.0 transport; messages are converted to/from
// our internal high-res format (MidiTypes.h). Designed so the transport
// layer can be swapped to MIDI 2.0 UMP when driver support matures.

#include "midi/MidiTypes.h"
#include "util/RingBuffer.h"
#include "util/Logger.h"
#include <RtMidi.h>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace midi {

// Lock-free ring buffer for raw MIDI messages from callback → audio thread
using MidiRingBuffer = util::RingBuffer<MidiMessage, 4096>;

// ALSA client name we give our own RtMidi instances. Every input we
// open registers a writable port under this client, which would then
// show up in the OUTPUT port list (and our outputs in the input
// list) — our own ports reflected back at us, offering a feedback
// loop that was even enabled by default. Naming the client lets the
// enumerators below filter our own ports out of the lists.
inline constexpr const char kOwnClientName[] = "YAWN";

// True when an enumerated port name belongs to our own client
// (ALSA names ports "client:port clientNum:portNum", e.g.
// "YAWN:YAWN 130:0"). Kept as a free function so tests can cover it
// without touching RtMidi.
inline bool isOwnPortName(const std::string& name) {
    return name.compare(0, sizeof(kOwnClientName), std::string(kOwnClientName) + ':') == 0;
}

class MidiPort {
public:
    enum class Direction { Input, Output };

    explicit MidiPort(Direction dir) : m_direction(dir) {}
    ~MidiPort() { close(); }

    MidiPort(const MidiPort&) = delete;
    MidiPort& operator=(const MidiPort&) = delete;

    // ---- Port enumeration (static helpers) ----

    static int countInputPorts() {
        try { RtMidiIn in; return static_cast<int>(in.getPortCount()); }
        catch (...) { return 0; }
    }

    static int countOutputPorts() {
        try { RtMidiOut out; return static_cast<int>(out.getPortCount()); }
        catch (...) { return 0; }
    }

    static std::string inputPortName(int index) {
        try { RtMidiIn in; return in.getPortName(index); }
        catch (...) { return ""; }
    }

    static std::string outputPortName(int index) {
        try { RtMidiOut out; return out.getPortName(index); }
        catch (...) { return ""; }
    }

    // One enumerated port: raw RtMidi index + display name, with our
    // own client's ports already filtered out (see kOwnClientName).
    // The UI lists ports in THIS filtered space, so open() must map
    // back to the raw index — otherwise filtering would shift every
    // port after the removed one off by one.
    struct EnumeratedPort {
        int         rawIndex;
        std::string name;
    };

    static std::vector<EnumeratedPort> enumerateInputPortsDetailed() {
        std::vector<EnumeratedPort> out;
        try {
            RtMidiIn in;
            int n = static_cast<int>(in.getPortCount());
            out.reserve(n);
            for (int i = 0; i < n; ++i) {
                std::string name = in.getPortName(i);
                if (!isOwnPortName(name))
                    out.push_back({i, std::move(name)});
            }
        } catch (...) {}
        return out;
    }

    static std::vector<EnumeratedPort> enumerateOutputPortsDetailed() {
        std::vector<EnumeratedPort> out;
        try {
            RtMidiOut out_;
            int n = static_cast<int>(out_.getPortCount());
            out.reserve(n);
            for (int i = 0; i < n; ++i) {
                std::string name = out_.getPortName(i);
                if (!isOwnPortName(name))
                    out.push_back({i, std::move(name)});
            }
        } catch (...) {}
        return out;
    }

    // Enumerate all input port names atomically using a single RtMidiIn instance,
    // avoiding TOCTOU crashes when devices disconnect between count and name queries.
    // Ports belonging to our own client are skipped — see kOwnClientName.
    static std::vector<std::string> enumerateInputPorts() {
        auto detailed = enumerateInputPortsDetailed();
        std::vector<std::string> names;
        names.reserve(detailed.size());
        for (auto& p : detailed) names.push_back(std::move(p.name));
        return names;
    }

    static std::vector<std::string> enumerateOutputPorts() {
        auto detailed = enumerateOutputPortsDetailed();
        std::vector<std::string> names;
        names.reserve(detailed.size());
        for (auto& p : detailed) names.push_back(std::move(p.name));
        return names;
    }

    // ---- Open / Close ----

    bool open(int portIndex) {
        close();
        try {
            // portIndex is in the FILTERED enumeration space (what the
            // UI lists) — map to the raw RtMidi index before opening.
            const auto list = (m_direction == Direction::Input)
                ? enumerateInputPortsDetailed() : enumerateOutputPortsDetailed();
            if (portIndex < 0 || portIndex >= static_cast<int>(list.size()))
                return false;
            const int rawIndex = list[portIndex].rawIndex;
            if (m_direction == Direction::Input) {
                m_midiIn = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kOwnClientName);
                m_midiIn->openPort(rawIndex);
                m_midiIn->setCallback(midiInCallback, this);
                m_midiIn->ignoreTypes(false, false, false); // receive sysex, timing, active sensing
            } else {
                m_midiOut = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kOwnClientName);
                m_midiOut->openPort(rawIndex);
            }
            m_open = true;
            m_portName = list[portIndex].name;
            return true;
        } catch (const RtMidiError& e) {
            LOG_ERROR("MIDI", "MidiPort::open error: %s", e.getMessage().c_str());
            close();
            return false;
        }
    }

    bool openVirtual(const std::string& name) {
        close();
        try {
            if (m_direction == Direction::Input) {
                m_midiIn = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, kOwnClientName);
                m_midiIn->openVirtualPort(name);
                m_midiIn->setCallback(midiInCallback, this);
                m_midiIn->ignoreTypes(false, false, false);
            } else {
                m_midiOut = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, kOwnClientName);
                m_midiOut->openVirtualPort(name);
            }
            m_open = true;
            m_portName = name;
            return true;
        } catch (const RtMidiError& e) {
            LOG_ERROR("MIDI", "MidiPort::openVirtual error: %s", e.getMessage().c_str());
            close();
            return false;
        }
    }

    void close() {
        if (m_midiIn) {
            m_midiIn->cancelCallback();
            m_midiIn->closePort();
            m_midiIn.reset();
        }
        if (m_midiOut) {
            m_midiOut->closePort();
            m_midiOut.reset();
        }
        m_open = false;
    }

    bool isOpen() const { return m_open; }
    const std::string& portName() const { return m_portName; }
    Direction direction() const { return m_direction; }

    // ---- Input: read messages (lock-free, safe for audio thread) ----

    int readMessages(MidiBuffer& buffer) {
        int count = 0;
        MidiMessage msg;
        while (m_inputRing.pop(msg)) {
            buffer.addMessage(msg);
            ++count;
        }
        return count;
    }

    // ---- Output: send a message ----

    void sendMessage(const MidiMessage& msg) {
        if (!m_midiOut || !m_open) return;
        uint8_t bytes[3];
        int len = Parse::toBytes(msg, bytes, 3);
        if (len > 0) {
            try {
                m_midiOut->sendMessage(bytes, len);
            } catch (...) {}
        }
    }

    void sendBuffer(const MidiBuffer& buffer) {
        for (int i = 0; i < buffer.count(); ++i)
            sendMessage(buffer[i]);
    }

private:
    // RtMidi input callback — runs on system MIDI thread, must be lock-free
    static void midiInCallback(double /*deltaTime*/,
                                std::vector<unsigned char>* message,
                                void* userData) {
        auto* port = static_cast<MidiPort*>(userData);
        if (!message || message->empty()) return;

        MidiMessage msg = Parse::fromBytes(message->data(),
                                            static_cast<int>(message->size()));
        if (msg.type != MidiMessage::Type::None) {
            port->m_inputRing.push(msg); // lock-free
        }
    }

    Direction   m_direction;
    bool        m_open = false;
    std::string m_portName;

    std::unique_ptr<RtMidiIn>  m_midiIn;
    std::unique_ptr<RtMidiOut> m_midiOut;

    MidiRingBuffer m_inputRing; // System MIDI thread → audio thread
};

} // namespace midi
} // namespace yawn
