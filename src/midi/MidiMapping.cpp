#include "midi/MidiMapping.h"

namespace yawn {
namespace midi {

void MidiLearnManager::startLearn(const automation::AutomationTarget& target,
                                   float minVal, float maxVal) {
    m_learning = true;
    m_learnTarget = target;
    m_learnMin = minVal;
    m_learnMax = maxVal;
}

bool MidiLearnManager::handleLearnCC(int channel, int cc) {
    if (!m_learning) return false;
    removeByTarget(m_learnTarget);

    MidiMapping m;
    m.source      = MappingSource::CC;
    m.midiChannel = channel;
    m.ccNumber    = cc;
    m.target      = m_learnTarget;
    m.paramMin    = m_learnMin;
    m.paramMax    = m_learnMax;
    m_mappings.push_back(m);

    m_learning = false;
    return true;
}

bool MidiLearnManager::handleLearnNote(int channel, int noteNum) {
    if (!m_learning) return false;
    removeByTarget(m_learnTarget);

    MidiMapping m;
    m.source      = MappingSource::Note;
    m.midiChannel = channel;
    m.noteNumber  = noteNum;
    m.target      = m_learnTarget;
    m.paramMin    = m_learnMin;
    m.paramMax    = m_learnMax;
    m_mappings.push_back(m);

    m_learning = false;
    return true;
}

void MidiLearnManager::removeTrackMappings(int trackIndex) {
    m_mappings.erase(
        std::remove_if(m_mappings.begin(), m_mappings.end(),
                       [&](const MidiMapping& m) {
                           return m.target.trackIndex == trackIndex &&
                                  m.target.type != automation::TargetType::Transport;
                       }),
        m_mappings.end());
    // Shift higher track indices down
    for (auto& m : m_mappings) {
        if (m.target.type != automation::TargetType::Transport &&
            m.target.trackIndex > trackIndex) {
            m.target.trackIndex--;
        }
    }
}

nlohmann::json MidiLearnManager::toJson() const {
    auto arr = nlohmann::json::array();
    for (auto& m : m_mappings) {
        arr.push_back({
            {"src",   static_cast<int>(m.source)},
            {"ch",    m.midiChannel},
            {"cc",    m.ccNumber},
            {"note",  m.noteNumber},
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

void MidiLearnManager::fromJson(const nlohmann::json& j) {
    m_mappings.clear();
    if (!j.is_array()) return;
    for (auto& item : j) {
        MidiMapping m;
        m.source      = static_cast<MappingSource>(item.value("src", 0));
        m.midiChannel = item.value("ch", -1);
        m.ccNumber    = item.value("cc", 0);
        m.noteNumber  = item.value("note", 0);
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

} // namespace midi
} // namespace yawn
