#pragma once

#include "automation/AutomationTypes.h"
#include "automation/AutomationEnvelope.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace yawn {
namespace automation {

// A single automation lane — binds an envelope to a specific parameter target.
// Used for both clip-level and track-level (arrangement) automation.
struct AutomationLane {
    AutomationTarget target;
    AutomationEnvelope envelope;
    bool armed = false;  // for Touch/Latch recording
};

// --- JSON helpers (inline, header-only) ---

inline nlohmann::json targetToJson(const AutomationTarget& t) {
    nlohmann::json j;
    j["type"]       = static_cast<int>(t.type);
    j["trackIndex"] = t.trackIndex;
    j["chainIndex"] = t.chainIndex;
    j["paramIndex"] = t.paramIndex;
    return j;
}

inline AutomationTarget targetFromJson(const nlohmann::json& j) {
    AutomationTarget t;
    t.type       = static_cast<TargetType>(j.value("type", 0));
    t.trackIndex = j.value("trackIndex", 0);
    t.chainIndex = j.value("chainIndex", 0);
    t.paramIndex = j.value("paramIndex", 0);
    return t;
}

inline nlohmann::json laneToJson(const AutomationLane& lane) {
    nlohmann::json j;
    j["target"]   = targetToJson(lane.target);
    j["envelope"] = lane.envelope.toJson();
    j["armed"]    = lane.armed;
    return j;
}

inline AutomationLane laneFromJson(const nlohmann::json& j) {
    AutomationLane lane;
    if (j.contains("target"))
        lane.target = targetFromJson(j["target"]);
    if (j.contains("envelope"))
        lane.envelope = AutomationEnvelope::fromJson(j["envelope"]);
    lane.armed = j.value("armed", false);
    return lane;
}

inline nlohmann::json lanesToJson(const std::vector<AutomationLane>& lanes) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& l : lanes) arr.push_back(laneToJson(l));
    return arr;
}

inline std::vector<AutomationLane> lanesFromJson(const nlohmann::json& j) {
    std::vector<AutomationLane> lanes;
    if (j.is_array()) {
        for (const auto& item : j)
            lanes.push_back(laneFromJson(item));
    }
    return lanes;
}

} // namespace automation
} // namespace yawn
