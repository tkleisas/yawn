#pragma once

// MIDI message types and buffer — internal representation at MIDI 2.0 resolution.
// All MIDI 1.0 input is upscaled to high-res internally. When MIDI 2.0 UMP transport
// is available, it maps directly to these types without loss of precision.

#include <algorithm>
#include <array>
#include <cstdint>

namespace yawn {
namespace midi {

// ---------------------------------------------------------------------------
// Resolution conversion helpers (MIDI 1.0 ↔ internal high-res)
// ---------------------------------------------------------------------------
namespace Convert {
    // 7-bit (0-127) → 16-bit (0-65535)
    inline constexpr uint16_t vel7to16(uint8_t v) {
        return static_cast<uint16_t>((static_cast<uint32_t>(v) << 9) |
                                     (static_cast<uint32_t>(v) << 2) |
                                     (static_cast<uint32_t>(v) >> 5));
    }
    // 16-bit → 7-bit
    inline constexpr uint8_t vel16to7(uint16_t v) {
        return static_cast<uint8_t>(v >> 9);
    }
    // 7-bit CC (0-127) → 32-bit (0-4294967295)
    inline constexpr uint32_t cc7to32(uint8_t v) {
        uint32_t w = static_cast<uint32_t>(v);
        return (w << 25) | (w << 18) | (w << 11) | (w << 4) | (w >> 3);
    }
    // 32-bit → 7-bit CC
    inline constexpr uint8_t cc32to7(uint32_t v) {
        return static_cast<uint8_t>(v >> 25);
    }
    // 14-bit pitch bend (0-16383, center 8192) → 32-bit (center 0x80000000)
    inline constexpr uint32_t pb14to32(uint16_t v) {
        return static_cast<uint32_t>(v) << 18;
    }
    // 32-bit → 14-bit pitch bend
    inline constexpr uint16_t pb32to14(uint32_t v) {
        return static_cast<uint16_t>(v >> 18);
    }
    // 32-bit pitch bend → normalized float (-1.0 to 1.0)
    inline float pb32toFloat(uint32_t v) {
        return (static_cast<double>(v) / 2147483648.0) - 1.0;
    }
    // Normalized float → 32-bit pitch bend
    inline uint32_t floatToPb32(float f) {
        double d = (static_cast<double>(f) + 1.0) * 2147483648.0;
        if (d < 0.0) d = 0.0;
        if (d > 4294967295.0) d = 4294967295.0;
        return static_cast<uint32_t>(d);
    }
} // namespace Convert

// ---------------------------------------------------------------------------
// MidiMessage — 16-byte, cache-friendly, high-resolution
// ---------------------------------------------------------------------------
struct MidiMessage {
    enum class Type : uint8_t {
        None = 0,
        NoteOn,
        NoteOff,
        PolyPressure,           // Per-note aftertouch
        ControlChange,
        ProgramChange,
        ChannelPressure,        // Channel aftertouch
        PitchBend,
        // Per-note controllers (MIDI 2.0 / MPE ready)
        PerNoteCC,
        PerNotePitchBend,
        PerNoteManagement,
        // System real-time
        Clock,
        Start,
        Stop,
        Continue,
        // Other
        SysEx
    };

    Type     type        = Type::None;
    uint8_t  channel     = 0;       // 0-15
    uint8_t  note        = 0;       // 0-127
    uint8_t  _pad        = 0;
    uint16_t velocity    = 0;       // 16-bit (0-65535)
    uint16_t ccNumber    = 0;       // CC number / per-note CC index
    uint32_t value       = 0;       // 32-bit: CC value, pitch bend, pressure
    int32_t  frameOffset = 0;       // Sample offset within current audio buffer

    // ---- MIDI 1.0 factory methods (auto-upscale) ----

    static MidiMessage noteOn(uint8_t ch, uint8_t n, uint8_t vel7, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::NoteOn;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.velocity = Convert::vel7to16(vel7);
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage noteOff(uint8_t ch, uint8_t n, uint8_t vel7 = 0, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::NoteOff;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.velocity = Convert::vel7to16(vel7);
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage cc(uint8_t ch, uint8_t ccNum, uint8_t val7, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::ControlChange;
        m.channel = ch & 0x0F;
        m.ccNumber = ccNum;
        m.value = Convert::cc7to32(val7);
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage pitchBend(uint8_t ch, uint16_t val14, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::PitchBend;
        m.channel = ch & 0x0F;
        m.value = Convert::pb14to32(val14);
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage channelPressure(uint8_t ch, uint8_t pressure7, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::ChannelPressure;
        m.channel = ch & 0x0F;
        m.value = Convert::cc7to32(pressure7);
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage polyPressure(uint8_t ch, uint8_t n, uint8_t pressure7, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::PolyPressure;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.value = Convert::cc7to32(pressure7);
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage programChange(uint8_t ch, uint8_t program, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::ProgramChange;
        m.channel = ch & 0x0F;
        m.value = static_cast<uint32_t>(program);
        m.frameOffset = frame;
        return m;
    }

    // ---- MIDI 2.0 high-res factory methods (native resolution) ----

    static MidiMessage noteOn16(uint8_t ch, uint8_t n, uint16_t vel16, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::NoteOn;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.velocity = vel16;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage noteOff16(uint8_t ch, uint8_t n, uint16_t vel16 = 0, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::NoteOff;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.velocity = vel16;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage cc32(uint8_t ch, uint16_t ccNum, uint32_t val32, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::ControlChange;
        m.channel = ch & 0x0F;
        m.ccNumber = ccNum;
        m.value = val32;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage pitchBend32(uint8_t ch, uint32_t val32, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::PitchBend;
        m.channel = ch & 0x0F;
        m.value = val32;
        m.frameOffset = frame;
        return m;
    }

    // ---- Per-note controllers (MIDI 2.0) ----

    static MidiMessage perNoteCC(uint8_t ch, uint8_t n, uint16_t ccNum, uint32_t val32, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::PerNoteCC;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.ccNumber = ccNum;
        m.value = val32;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage perNotePitchBend(uint8_t ch, uint8_t n, uint32_t val32, int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::PerNotePitchBend;
        m.channel = ch & 0x0F;
        m.note = n & 0x7F;
        m.value = val32;
        m.frameOffset = frame;
        return m;
    }

    // ---- System messages ----

    static MidiMessage clock(int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::Clock;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage start(int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::Start;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage stop(int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::Stop;
        m.frameOffset = frame;
        return m;
    }

    static MidiMessage cont(int32_t frame = 0) {
        MidiMessage m;
        m.type = Type::Continue;
        m.frameOffset = frame;
        return m;
    }

    // ---- Query helpers ----

    bool isNote()       const { return type == Type::NoteOn || type == Type::NoteOff; }
    bool isNoteOn()     const { return type == Type::NoteOn && velocity > 0; }
    bool isNoteOff()    const { return type == Type::NoteOff || (type == Type::NoteOn && velocity == 0); }
    bool isCC()         const { return type == Type::ControlChange; }
    bool isPerNoteMsg() const { return type == Type::PerNoteCC || type == Type::PerNotePitchBend || type == Type::PerNoteManagement; }

    // MIDI 1.0 convenience getters
    uint8_t velocity7()   const { return Convert::vel16to7(velocity); }
    uint8_t ccValue7()    const { return Convert::cc32to7(value); }
    uint16_t pitchBend14() const { return Convert::pb32to14(value); }
};

static_assert(sizeof(MidiMessage) == 16, "MidiMessage must be 16 bytes for cache efficiency");

// ---------------------------------------------------------------------------
// MPE Zone Configuration
// ---------------------------------------------------------------------------
struct MpeZone {
    bool     enabled            = false;
    uint8_t  masterChannel      = 0;    // 0 for lower zone, 15 for upper zone
    uint8_t  memberChannelCount = 0;    // Number of member channels (1-15)
    float    pitchBendRange     = 48.0f; // Per-note pitch bend range in semitones
    float    masterPitchBendRange = 2.0f; // Master channel pitch bend range

    // Returns true if the given channel is a member of this zone
    bool isMemberChannel(uint8_t ch) const {
        if (!enabled || memberChannelCount == 0) return false;
        uint8_t firstMember = masterChannel + 1;
        return ch >= firstMember && ch < firstMember + memberChannelCount;
    }

    bool isMasterChannel(uint8_t ch) const {
        return enabled && ch == masterChannel;
    }
};

struct MpeConfig {
    MpeZone lowerZone;  // Master = ch 0,  members = ch 1..N
    MpeZone upperZone;  // Master = ch 15, members = ch 15-N..14

    MpeConfig() {
        lowerZone.masterChannel = 0;
        upperZone.masterChannel = 15;
    }

    void enableLowerZone(int memberChannels, float pbRange = 48.0f) {
        lowerZone.enabled = true;
        lowerZone.memberChannelCount = static_cast<uint8_t>(memberChannels);
        lowerZone.pitchBendRange = pbRange;
    }

    void enableUpperZone(int memberChannels, float pbRange = 48.0f) {
        upperZone.enabled = true;
        upperZone.memberChannelCount = static_cast<uint8_t>(memberChannels);
        upperZone.pitchBendRange = pbRange;
    }

    void disableLowerZone() { lowerZone.enabled = false; lowerZone.memberChannelCount = 0; }
    void disableUpperZone() { upperZone.enabled = false; upperZone.memberChannelCount = 0; }
};

// ---------------------------------------------------------------------------
// MidiBuffer — fixed-capacity message buffer for audio-thread use
// ---------------------------------------------------------------------------
static constexpr int kMaxMidiMessagesPerBuffer = 1024;

class MidiBuffer {
public:
    void clear() { m_count = 0; }

    bool addMessage(const MidiMessage& msg) {
        if (m_count >= kMaxMidiMessagesPerBuffer) return false;
        m_messages[m_count++] = msg;
        return true;
    }

    void sortByFrame() {
        std::sort(m_messages.begin(), m_messages.begin() + m_count,
                  [](const MidiMessage& a, const MidiMessage& b) {
                      return a.frameOffset < b.frameOffset;
                  });
    }

    int count() const { return m_count; }
    bool empty() const { return m_count == 0; }

    const MidiMessage& operator[](int i) const { return m_messages[i]; }
    MidiMessage& operator[](int i) { return m_messages[i]; }

    const MidiMessage* begin() const { return m_messages.data(); }
    const MidiMessage* end()   const { return m_messages.data() + m_count; }
    MidiMessage*       begin()       { return m_messages.data(); }
    MidiMessage*       end()         { return m_messages.data() + m_count; }

    // Merge another buffer into this one (used for combining inputs)
    void merge(const MidiBuffer& other) {
        for (int i = 0; i < other.count() && m_count < kMaxMidiMessagesPerBuffer; ++i) {
            m_messages[m_count++] = other[i];
        }
    }

private:
    std::array<MidiMessage, kMaxMidiMessagesPerBuffer> m_messages;
    int m_count = 0;
};

// ---------------------------------------------------------------------------
// MIDI 1.0 raw byte parsing (for RtMidi callback)
// ---------------------------------------------------------------------------
namespace Parse {
    // Parse a raw MIDI 1.0 byte sequence into a MidiMessage.
    // Returns MidiMessage::Type::None if unparseable.
    inline MidiMessage fromBytes(const uint8_t* data, int length, int32_t frameOffset = 0) {
        if (length < 1) return {};

        uint8_t status = data[0];

        // System real-time (single byte)
        if (status >= 0xF8) {
            switch (status) {
                case 0xF8: return MidiMessage::clock(frameOffset);
                case 0xFA: return MidiMessage::start(frameOffset);
                case 0xFB: return MidiMessage::cont(frameOffset);
                case 0xFC: return MidiMessage::stop(frameOffset);
                default: return {};
            }
        }

        // Channel messages
        uint8_t type = status & 0xF0;
        uint8_t ch   = status & 0x0F;

        switch (type) {
            case 0x90: // Note On
                if (length < 3) return {};
                if (data[2] == 0)
                    return MidiMessage::noteOff(ch, data[1], 0, frameOffset);
                return MidiMessage::noteOn(ch, data[1], data[2], frameOffset);

            case 0x80: // Note Off
                if (length < 3) return {};
                return MidiMessage::noteOff(ch, data[1], data[2], frameOffset);

            case 0xA0: // Poly Pressure
                if (length < 3) return {};
                return MidiMessage::polyPressure(ch, data[1], data[2], frameOffset);

            case 0xB0: // Control Change
                if (length < 3) return {};
                return MidiMessage::cc(ch, data[1], data[2], frameOffset);

            case 0xC0: // Program Change
                if (length < 2) return {};
                return MidiMessage::programChange(ch, data[1], frameOffset);

            case 0xD0: // Channel Pressure
                if (length < 2) return {};
                return MidiMessage::channelPressure(ch, data[1], frameOffset);

            case 0xE0: // Pitch Bend
                if (length < 3) return {};
                {
                    uint16_t pb = static_cast<uint16_t>(data[1]) |
                                  (static_cast<uint16_t>(data[2]) << 7);
                    return MidiMessage::pitchBend(ch, pb, frameOffset);
                }
            default:
                return {};
        }
    }

    // Serialize a MidiMessage to raw MIDI 1.0 bytes.
    // Returns number of bytes written (0 if not serializable).
    inline int toBytes(const MidiMessage& msg, uint8_t* out, int maxLen) {
        switch (msg.type) {
            case MidiMessage::Type::NoteOn:
                if (maxLen < 3) return 0;
                out[0] = 0x90 | msg.channel;
                out[1] = msg.note;
                out[2] = msg.velocity7();
                return 3;
            case MidiMessage::Type::NoteOff:
                if (maxLen < 3) return 0;
                out[0] = 0x80 | msg.channel;
                out[1] = msg.note;
                out[2] = msg.velocity7();
                return 3;
            case MidiMessage::Type::ControlChange:
                if (maxLen < 3) return 0;
                out[0] = 0xB0 | msg.channel;
                out[1] = static_cast<uint8_t>(msg.ccNumber & 0x7F);
                out[2] = msg.ccValue7();
                return 3;
            case MidiMessage::Type::PitchBend: {
                if (maxLen < 3) return 0;
                uint16_t pb14 = msg.pitchBend14();
                out[0] = 0xE0 | msg.channel;
                out[1] = pb14 & 0x7F;
                out[2] = (pb14 >> 7) & 0x7F;
                return 3;
            }
            case MidiMessage::Type::ChannelPressure:
                if (maxLen < 2) return 0;
                out[0] = 0xD0 | msg.channel;
                out[1] = msg.ccValue7();
                return 2;
            case MidiMessage::Type::PolyPressure:
                if (maxLen < 3) return 0;
                out[0] = 0xA0 | msg.channel;
                out[1] = msg.note;
                out[2] = msg.ccValue7();
                return 3;
            case MidiMessage::Type::ProgramChange:
                if (maxLen < 2) return 0;
                out[0] = 0xC0 | msg.channel;
                out[1] = static_cast<uint8_t>(msg.value & 0x7F);
                return 2;
            case MidiMessage::Type::Clock:
                if (maxLen < 1) return 0;
                out[0] = 0xF8; return 1;
            case MidiMessage::Type::Start:
                if (maxLen < 1) return 0;
                out[0] = 0xFA; return 1;
            case MidiMessage::Type::Stop:
                if (maxLen < 1) return 0;
                out[0] = 0xFC; return 1;
            case MidiMessage::Type::Continue:
                if (maxLen < 1) return 0;
                out[0] = 0xFB; return 1;
            default:
                return 0;
        }
    }
} // namespace Parse

} // namespace midi
} // namespace yawn
