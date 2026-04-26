#pragma once
// MidiMapping — MIDI Learn mapping types and manager.
//
// MidiMapping links a MIDI source (CC or Note) to an AutomationTarget.
// MidiLearnManager stores all mappings, handles learn mode, and provides
// lookup for CC/Note interception in the audio thread.

#include "automation/AutomationTypes.h"
#include <vector>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <nlohmann/json.hpp>

namespace yawn {
namespace midi {

enum class MappingSource : uint8_t {
    CC   = 0,
    Note = 1
};

// A single MIDI → parameter mapping
struct MidiMapping {
    MappingSource source = MappingSource::CC;
    int midiChannel = -1;       // -1 = any channel
    int ccNumber    = 0;        // CC: 0-127 controller number
    int noteNumber  = 0;        // Note: 0-127 MIDI note number
    automation::AutomationTarget target;
    float paramMin  = 0.0f;     // CC 0 / NoteOff → paramMin
    float paramMax  = 1.0f;     // CC 127 / NoteOn → paramMax
    bool  enabled   = true;

    // Convert CC value (0-127) to parameter value
    float ccToParam(int ccValue) const {
        float t = static_cast<float>(ccValue) / 127.0f;
        return paramMin + t * (paramMax - paramMin);
    }

    // Convert note velocity (0=off, >0=on) to parameter value
    float noteToParam(bool noteOn) const {
        return noteOn ? paramMax : paramMin;
    }

    bool matchesCC(int ch, int cc) const {
        if (!enabled || source != MappingSource::CC) return false;
        if (midiChannel != -1 && midiChannel != ch) return false;
        return ccNumber == cc;
    }

    bool matchesNote(int ch, int noteNum) const {
        if (!enabled || source != MappingSource::Note) return false;
        if (midiChannel != -1 && midiChannel != ch) return false;
        return noteNumber == noteNum;
    }

    // Label for display: "CC 7" or "N 60"
    std::string label() const {
        char buf[16];
        if (source == MappingSource::Note)
            std::snprintf(buf, sizeof(buf), "N%d", noteNumber);
        else
            std::snprintf(buf, sizeof(buf), "CC%d", ccNumber);
        return buf;
    }
};

// Manages MIDI → parameter mappings and learn mode
class MidiLearnManager {
public:
    // ── Learn mode ──

    void startLearn(const automation::AutomationTarget& target,
                    float minVal = 0.0f, float maxVal = 1.0f);

    bool isLearning() const { return m_learning; }

    const automation::AutomationTarget& learnTarget() const { return m_learnTarget; }

    void cancelLearn() { m_learning = false; }

    // Called when a CC arrives while in learn mode.
    bool handleLearnCC(int channel, int cc);

    // Called when a NoteOn arrives while in learn mode.
    bool handleLearnNote(int channel, int noteNum);

    // ── Mapping management ──

    void addMapping(const MidiMapping& m) {
        m_mappings.push_back(m);
    }

    const MidiMapping* findByTarget(const automation::AutomationTarget& t) const {
        for (auto& m : m_mappings) {
            if (m.target == t) return &m;
        }
        return nullptr;
    }

    std::vector<const MidiMapping*> findByCC(int channel, int cc) const {
        std::vector<const MidiMapping*> result;
        for (auto& m : m_mappings) {
            if (m.matchesCC(channel, cc))
                result.push_back(&m);
        }
        return result;
    }

    std::vector<const MidiMapping*> findByNote(int channel, int noteNum) const {
        std::vector<const MidiMapping*> result;
        for (auto& m : m_mappings) {
            if (m.matchesNote(channel, noteNum))
                result.push_back(&m);
        }
        return result;
    }

    void removeByTarget(const automation::AutomationTarget& t) {
        m_mappings.erase(
            std::remove_if(m_mappings.begin(), m_mappings.end(),
                           [&](const MidiMapping& m) { return m.target == t; }),
            m_mappings.end());
    }

    void clearAll() {
        m_mappings.clear();
        m_learning = false;
    }

    // Remove all mappings targeting a specific track, and shift higher track indices down
    void removeTrackMappings(int trackIndex);

    bool empty() const { return m_mappings.empty(); }
    size_t size() const { return m_mappings.size(); }

    const std::vector<MidiMapping>& mappings() const { return m_mappings; }

    // ── Serialization ──

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::vector<MidiMapping> m_mappings;

    // Learn mode state
    bool m_learning = false;
    automation::AutomationTarget m_learnTarget;
    float m_learnMin = 0.0f;
    float m_learnMax = 1.0f;
};

} // namespace midi
} // namespace yawn
