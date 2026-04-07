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

    // Enumerate all input port names atomically using a single RtMidiIn instance,
    // avoiding TOCTOU crashes when devices disconnect between count and name queries.
    static std::vector<std::string> enumerateInputPorts() {
        std::vector<std::string> names;
        try {
            RtMidiIn in;
            int n = static_cast<int>(in.getPortCount());
            names.reserve(n);
            for (int i = 0; i < n; ++i)
                names.push_back(in.getPortName(i));
        } catch (...) {}
        return names;
    }

    static std::vector<std::string> enumerateOutputPorts() {
        std::vector<std::string> names;
        try {
            RtMidiOut out;
            int n = static_cast<int>(out.getPortCount());
            names.reserve(n);
            for (int i = 0; i < n; ++i)
                names.push_back(out.getPortName(i));
        } catch (...) {}
        return names;
    }

    // ---- Open / Close ----

    bool open(int portIndex) {
        close();
        try {
            if (m_direction == Direction::Input) {
                m_midiIn = std::make_unique<RtMidiIn>();
                m_midiIn->openPort(portIndex);
                m_midiIn->setCallback(midiInCallback, this);
                m_midiIn->ignoreTypes(false, false, false); // receive sysex, timing, active sensing
            } else {
                m_midiOut = std::make_unique<RtMidiOut>();
                m_midiOut->openPort(portIndex);
            }
            m_open = true;
            m_portName = (m_direction == Direction::Input)
                ? inputPortName(portIndex) : outputPortName(portIndex);
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
                m_midiIn = std::make_unique<RtMidiIn>();
                m_midiIn->openVirtualPort(name);
                m_midiIn->setCallback(midiInCallback, this);
                m_midiIn->ignoreTypes(false, false, false);
            } else {
                m_midiOut = std::make_unique<RtMidiOut>();
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
