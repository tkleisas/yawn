#include "util/MidiFileIO.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace yawn {
namespace util {

namespace {

// ── Big-endian read helpers ─────────────────────────────────────────────────
uint16_t readBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t readBE32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) <<  8) |
            static_cast<uint32_t>(p[3]);
}
// Variable-length quantity (VLQ): big-endian, MSB = continuation.
// Returns the parsed value; advances `off`.
uint32_t readVLQ(const uint8_t* p, size_t& off, size_t end) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && off < end; ++i) {
        uint8_t b = p[off++];
        v = (v << 7) | (b & 0x7Fu);
        if (!(b & 0x80u)) return v;
    }
    return v;
}

// ── Big-endian write helpers ────────────────────────────────────────────────
void writeBE16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(static_cast<uint8_t>(v >> 8));
    o.push_back(static_cast<uint8_t>(v & 0xFF));
}
void writeBE32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    o.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    o.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
    o.push_back(static_cast<uint8_t>( v        & 0xFF));
}
void writeVLQ(std::vector<uint8_t>& o, uint32_t v) {
    uint8_t buf[5];
    int n = 0;
    buf[n++] = v & 0x7F;
    v >>= 7;
    while (v) {
        buf[n++] = (v & 0x7F) | 0x80;
        v >>= 7;
    }
    for (int i = n - 1; i >= 0; --i) o.push_back(buf[i]);
}

// ── Tag matching ────────────────────────────────────────────────────────────
bool tagMatches(const uint8_t* p, const char* tag) {
    return p[0] == static_cast<uint8_t>(tag[0]) &&
           p[1] == static_cast<uint8_t>(tag[1]) &&
           p[2] == static_cast<uint8_t>(tag[2]) &&
           p[3] == static_cast<uint8_t>(tag[3]);
}

// Open notes keyed by (channel, pitch) so a NoteOff closes the right one.
struct OpenKey {
    uint8_t ch, pitch;
    bool operator==(const OpenKey& o) const { return ch == o.ch && pitch == o.pitch; }
};
struct OpenKeyHash {
    size_t operator()(const OpenKey& k) const noexcept {
        return (static_cast<size_t>(k.ch) << 8) | k.pitch;
    }
};
struct OpenNote {
    uint32_t startTicks = 0;
    uint8_t  vel7       = 0;
};

} // namespace

// ── Reader ──────────────────────────────────────────────────────────────────
std::unique_ptr<midi::MidiClip> loadMidiFile(const std::filesystem::path& path,
                                              MidiFileInfo* info) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return nullptr;
    std::streamsize sz = f.tellg();
    if (sz < 14) return nullptr;
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (!f.read(reinterpret_cast<char*>(buf.data()), sz)) return nullptr;

    const uint8_t* p   = buf.data();
    const size_t   len = buf.size();

    // ── Header chunk ──
    if (len < 14 || !tagMatches(p, "MThd")) return nullptr;
    uint32_t hdrLen = readBE32(p + 4);
    if (hdrLen < 6 || 8 + hdrLen > len) return nullptr;
    uint16_t format  = readBE16(p + 8);
    uint16_t ntracks = readBE16(p + 10);
    int16_t  div     = static_cast<int16_t>(readBE16(p + 12));
    if (format > 1) return nullptr;          // type 2 (independent patterns) — not supported
    if (div <= 0)   return nullptr;          // SMPTE not supported
    const double division = static_cast<double>(div);

    auto clip = std::make_unique<midi::MidiClip>();
    clip->setName(path.stem().string());

    double tempoBPM = 120.0;
    int    tsNum    = 4;
    int    tsDen    = 4;
    uint32_t maxAbsTicks = 0;

    size_t off = 8 + hdrLen;
    for (int t = 0; t < ntracks; ++t) {
        if (off + 8 > len || !tagMatches(p + off, "MTrk")) return nullptr;
        uint32_t trkLen = readBE32(p + off + 4);
        off += 8;
        if (off + trkLen > len) return nullptr;
        const size_t trkEnd = off + trkLen;

        std::unordered_map<OpenKey, OpenNote, OpenKeyHash> open;
        uint32_t absTicks   = 0;
        uint8_t  runStatus  = 0;

        while (off < trkEnd) {
            uint32_t delta = readVLQ(p, off, trkEnd);
            absTicks += delta;
            if (off >= trkEnd) break;
            uint8_t status = p[off];
            if (status & 0x80u) {
                ++off;
                if (status < 0xF0u) runStatus = status;   // running status applies
            } else {
                status = runStatus;                       // running status
                if (!status) return nullptr;              // invalid
            }

            if (status == 0xFFu) {
                // Meta event.
                if (off >= trkEnd) return nullptr;
                uint8_t mtype = p[off++];
                uint32_t mlen = readVLQ(p, off, trkEnd);
                if (off + mlen > trkEnd) return nullptr;
                if (mtype == 0x51 && mlen == 3) {
                    // Tempo: microseconds per quarter note.
                    uint32_t us = (static_cast<uint32_t>(p[off]) << 16) |
                                  (static_cast<uint32_t>(p[off + 1]) << 8) |
                                   static_cast<uint32_t>(p[off + 2]);
                    if (us > 0) tempoBPM = 60000000.0 / static_cast<double>(us);
                } else if (mtype == 0x58 && mlen == 4) {
                    tsNum = p[off];
                    tsDen = 1 << p[off + 1];
                } else if (mtype == 0x2F) {
                    off += mlen;
                    break;  // End of Track
                }
                off += mlen;
            } else if (status == 0xF0u || status == 0xF7u) {
                // SysEx: just skip.
                uint32_t slen = readVLQ(p, off, trkEnd);
                if (off + slen > trkEnd) return nullptr;
                off += slen;
            } else {
                const uint8_t type = status & 0xF0u;
                const uint8_t ch   = status & 0x0Fu;
                switch (type) {
                    case 0x80: { // Note Off
                        if (off + 2 > trkEnd) return nullptr;
                        uint8_t note = p[off++], vel = p[off++];
                        (void)vel;
                        auto it = open.find({ch, note});
                        if (it != open.end()) {
                            midi::MidiNote n;
                            n.startBeat = it->second.startTicks / division;
                            n.duration  = std::max(0.01,
                                (absTicks - it->second.startTicks) / division);
                            n.pitch     = note;
                            n.channel   = ch;
                            // YAWN stores velocity at 16-bit; existing
                            // recording code uses vel7 << 9 (see
                            // AudioEngine MIDI capture).
                            uint16_t v16 = static_cast<uint16_t>(it->second.vel7) << 9;
                            n.velocity  = v16 ? v16 : 1;
                            clip->addNote(n);
                            open.erase(it);
                        }
                        break;
                    }
                    case 0x90: { // Note On (vel 0 == Note Off)
                        if (off + 2 > trkEnd) return nullptr;
                        uint8_t note = p[off++], vel = p[off++];
                        if (vel == 0) {
                            auto it = open.find({ch, note});
                            if (it != open.end()) {
                                midi::MidiNote n;
                                n.startBeat = it->second.startTicks / division;
                                n.duration  = std::max(0.01,
                                    (absTicks - it->second.startTicks) / division);
                                n.pitch     = note;
                                n.channel   = ch;
                                uint16_t v16 = static_cast<uint16_t>(it->second.vel7) << 9;
                                n.velocity  = v16 ? v16 : 1;
                                clip->addNote(n);
                                open.erase(it);
                            }
                        } else {
                            open[{ch, note}] = {absTicks, vel};
                        }
                        break;
                    }
                    case 0xB0: { // Control Change
                        if (off + 2 > trkEnd) return nullptr;
                        midi::MidiCCEvent cc;
                        cc.beat     = absTicks / division;
                        cc.ccNumber = p[off++];
                        // 7-bit value → 32-bit (MIDI 2.0 scale), shift
                        // into the top bits like the engine recorder does.
                        cc.value    = static_cast<uint32_t>(p[off++]) << 25;
                        cc.channel  = ch;
                        clip->addCC(cc);
                        break;
                    }
                    case 0xA0:        // Poly Aftertouch (2 bytes) — skip
                    case 0xE0:        // Pitch Bend       (2 bytes) — skip
                        if (off + 2 > trkEnd) return nullptr;
                        off += 2;
                        break;
                    case 0xC0:        // Program Change   (1 byte) — skip
                    case 0xD0:        // Channel Pressure (1 byte) — skip
                        if (off + 1 > trkEnd) return nullptr;
                        off += 1;
                        break;
                    default:
                        return nullptr;  // unknown status
                }
            }
        }
        if (absTicks > maxAbsTicks) maxAbsTicks = absTicks;
        // Close any notes left hanging at end-of-track (defensive).
        for (auto& kv : open) {
            midi::MidiNote n;
            n.startBeat = kv.second.startTicks / division;
            n.duration  = std::max(0.01,
                (absTicks - kv.second.startTicks) / division);
            n.pitch     = kv.first.pitch;
            n.channel   = kv.first.ch;
            uint16_t v16 = static_cast<uint16_t>(kv.second.vel7) << 9;
            n.velocity  = v16 ? v16 : 1;
            clip->addNote(n);
        }
        off = trkEnd;
    }

    // Loop length: the larger of the track's reported length and the
    // last note's end. Round up to a whole beat so loops don't have
    // awkward fractional tails from a stray meta-event delta.
    double lenBeats = maxAbsTicks / division;
    for (int i = 0; i < clip->noteCount(); ++i) {
        const auto& n = clip->note(i);
        lenBeats = std::max(lenBeats, n.startBeat + n.duration);
    }
    if (lenBeats < 0.25) lenBeats = 0.25;
    lenBeats = std::ceil(lenBeats - 1e-6);   // snap up to whole beats
    clip->setLengthBeats(lenBeats);
    clip->setLoop(true);

    if (info) {
        info->tempoBPM   = tempoBPM;
        info->timeSigNum = tsNum;
        info->timeSigDen = tsDen;
    }
    return clip;
}

// ── Writer ──────────────────────────────────────────────────────────────────
bool saveMidiFile(const std::filesystem::path& path,
                  const midi::MidiClip& clip,
                  double tempoBPM,
                  int    timeSigNum,
                  int    timeSigDen) {
    constexpr int kPPQ = 480;

    // Build chronological event list (note-on/off pairs) in ticks.
    struct Ev {
        uint32_t tick;
        uint8_t  status, d1, d2;
        // Stable sort key — note-offs sort before note-ons at the same
        // tick so a "next-note touches end of previous" doesn't lose
        // either event.
        int order = 0;
    };
    std::vector<Ev> evs;
    evs.reserve(static_cast<size_t>(clip.noteCount()) * 2);

    for (int i = 0; i < clip.noteCount(); ++i) {
        const auto& n = clip.note(i);
        uint32_t tOn  = static_cast<uint32_t>(std::lround(n.startBeat * kPPQ));
        uint32_t tOff = static_cast<uint32_t>(std::lround((n.startBeat + n.duration) * kPPQ));
        if (tOff <= tOn) tOff = tOn + 1;
        // 16-bit → 7-bit velocity (the inverse of vel7 << 9). Floor + clamp.
        uint8_t vel7 = static_cast<uint8_t>(std::min<uint16_t>(n.velocity >> 9, 127));
        if (vel7 == 0) vel7 = 1;
        uint8_t ch   = n.channel & 0x0Fu;
        evs.push_back({tOn,  static_cast<uint8_t>(0x90u | ch), n.pitch, vel7, 1});
        evs.push_back({tOff, static_cast<uint8_t>(0x80u | ch), n.pitch, 0,    0});
    }

    std::stable_sort(evs.begin(), evs.end(),
        [](const Ev& a, const Ev& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.order < b.order;
        });

    // Build the track body.
    std::vector<uint8_t> track;
    track.reserve(evs.size() * 4 + 32);

    // Tempo meta event (μs per quarter).
    {
        uint32_t us = static_cast<uint32_t>(60000000.0 / std::max(1.0, tempoBPM));
        writeVLQ(track, 0);
        track.push_back(0xFF); track.push_back(0x51); track.push_back(0x03);
        track.push_back(static_cast<uint8_t>((us >> 16) & 0xFF));
        track.push_back(static_cast<uint8_t>((us >>  8) & 0xFF));
        track.push_back(static_cast<uint8_t>( us        & 0xFF));
    }
    // Time-signature meta event.
    {
        uint8_t dp = 0;
        for (int d = timeSigDen; d > 1; d >>= 1) ++dp;  // log2(den)
        writeVLQ(track, 0);
        track.push_back(0xFF); track.push_back(0x58); track.push_back(0x04);
        track.push_back(static_cast<uint8_t>(timeSigNum));
        track.push_back(dp);
        track.push_back(24);   // MIDI clocks per metronome click
        track.push_back(8);    // 32nd notes per quarter
    }

    // Note events with VLQ deltas.
    uint32_t prevTick = 0;
    for (const Ev& e : evs) {
        writeVLQ(track, e.tick - prevTick);
        prevTick = e.tick;
        track.push_back(e.status);
        track.push_back(e.d1);
        track.push_back(e.d2);
    }
    // End of Track meta event.
    writeVLQ(track, 0);
    track.push_back(0xFF); track.push_back(0x2F); track.push_back(0x00);

    // Assemble file: MThd + MTrk.
    std::vector<uint8_t> out;
    out.reserve(14 + 8 + track.size());
    out.insert(out.end(), {'M','T','h','d'});
    writeBE32(out, 6);
    writeBE16(out, 0);            // format 0
    writeBE16(out, 1);            // one track
    writeBE16(out, static_cast<uint16_t>(kPPQ));
    out.insert(out.end(), {'M','T','r','k'});
    writeBE32(out, static_cast<uint32_t>(track.size()));
    out.insert(out.end(), track.begin(), track.end());

    std::ofstream of(path, std::ios::binary | std::ios::trunc);
    if (!of) return false;
    of.write(reinterpret_cast<const char*>(out.data()),
             static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(of);
}

} // namespace util
} // namespace yawn
