#pragma once
// MidiMapping — MIDI Learn mapping types and manager.
//
// MidiMapping links a MIDI CC (channel + CC#) to an AutomationTarget.
// MidiLearnManager stores all mappings, handles learn mode, and provides
// lookup for CC interception in the audio thread.

#include "automation/AutomationTypes.h"
#include <vector>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <nlohmann/json.hpp>

namespace yawn {
namespace midi {

// A single CC → parameter mapping
struct MidiMapping {
    int midiChannel = -1;       // -1 = any channel
    int ccNumber    = 0;        // 0-127
    automation::AutomationTarget target;
    float paramMin  = 0.0f;     // CC 0   → paramMin
    float paramMax  = 1.0f;     // CC 127 → paramMax
    bool  enabled   = true;

    // Convert CC value (0-127) to parameter value
    float ccToParam(int ccValue) const {
        float t = static_cast<float>(ccValue) / 127.0f;
        return paramMin + t * (paramMax - paramMin);
    }

    bool matchesCC(int ch, int cc) const {
        if (!enabled) return false;
        if (midiChannel != -1 && midiChannel != ch) return false;
        return ccNumber == cc;
    }
};

// Manages MIDI CC → parameter mappings and learn mode
class MidiLearnManager {
public:
    // ── Learn mode ──

    void startLearn(const automation::AutomationTarget& target,
                    float minVal = 0.0f, float maxVal = 1.0f) {
        m_learning = true;
        m_learnTarget = target;
        m_learnMin = minVal;
        m_learnMax = maxVal;
    }

    bool isLearning() const { return m_learning; }

    const automation::AutomationTarget& learnTarget() const { return m_learnTarget; }

    void cancelLearn() { m_learning = false; }

    // Called when a CC arrives while in learn mode.
    // Returns true if the CC was consumed (mapping created).
    bool handleLearnCC(int channel, int cc) {
        if (!m_learning) return false;

        // Remove any existing mapping for this target
        removeByTarget(m_learnTarget);

        MidiMapping m;
        m.midiChannel = channel;
        m.ccNumber    = cc;
        m.target      = m_learnTarget;
        m.paramMin    = m_learnMin;
        m.paramMax    = m_learnMax;
        m_mappings.push_back(m);

        m_learning = false;
        return true;
    }

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

    // Find all mappings for a given CC (may be multiple targets)
    std::vector<const MidiMapping*> findByCC(int channel, int cc) const {
        std::vector<const MidiMapping*> result;
        for (auto& m : m_mappings) {
            if (m.matchesCC(channel, cc))
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

    bool empty() const { return m_mappings.empty(); }
    size_t size() const { return m_mappings.size(); }

    const std::vector<MidiMapping>& mappings() const { return m_mappings; }

    // ── Serialization ──

    nlohmann::json toJson() const {
        auto arr = nlohmann::json::array();
        for (auto& m : m_mappings) {
            arr.push_back({
                {"ch",    m.midiChannel},
                {"cc",    m.ccNumber},
                {"tType", static_cast<int>(m.target.type)},
                {"tTrack", m.target.trackIndex},
                {"tChain", m.target.chainIndex},
                {"tParam", m.target.paramIndex},
                {"min",   m.paramMin},
                {"max",   m.paramMax},
                {"on",    m.enabled}
            });
        }
        return arr;
    }

    void fromJson(const nlohmann::json& j) {
        m_mappings.clear();
        if (!j.is_array()) return;
        for (auto& item : j) {
            MidiMapping m;
            m.midiChannel = item.value("ch", -1);
            m.ccNumber    = item.value("cc", 0);
            m.target.type       = static_cast<automation::TargetType>(item.value("tType", 0));
            m.target.trackIndex = item.value("tTrack", 0);
            m.target.chainIndex = item.value("tChain", 0);
            m.target.paramIndex = item.value("tParam", 0);
            m.paramMin    = item.value("min", 0.0f);
            m.paramMax    = item.value("max", 1.0f);
            m.enabled     = item.value("on", true);
            m_mappings.push_back(m);
        }
    }

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
