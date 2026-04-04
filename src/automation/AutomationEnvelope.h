#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace yawn {
namespace automation {

struct BreakPoint {
    double time  = 0.0;    // position in beats
    float  value = 0.0f;   // parameter value at this point
};

// Sorted breakpoint envelope with linear interpolation.
// Times are in beats — relative to clip start (clip automation)
// or absolute from project start (track/arrangement automation).
class AutomationEnvelope {
public:
    // Add a point, maintaining sorted order by time.
    // If a point already exists at this time (within epsilon), updates its value.
    void addPoint(double time, float value) {
        constexpr double kEpsilon = 1e-6;
        for (auto& bp : m_points) {
            if (std::abs(bp.time - time) < kEpsilon) {
                bp.value = value;
                return;
            }
        }
        BreakPoint bp{time, value};
        auto it = std::lower_bound(m_points.begin(), m_points.end(), bp,
            [](const BreakPoint& a, const BreakPoint& b) { return a.time < b.time; });
        m_points.insert(it, bp);
    }

    void removePoint(int index) {
        if (index >= 0 && index < static_cast<int>(m_points.size()))
            m_points.erase(m_points.begin() + index);
    }

    void movePoint(int index, double time, float value) {
        if (index < 0 || index >= static_cast<int>(m_points.size())) return;
        m_points.erase(m_points.begin() + index);
        addPoint(time, value);
    }

    // Linear interpolation at a given time.
    // Before first point: returns first value. After last: returns last.
    // Empty envelope: returns defaultValue.
    float valueAt(double time, float defaultValue = 0.0f) const {
        if (m_points.empty()) return defaultValue;
        if (m_points.size() == 1) return m_points[0].value;

        // Before first point
        if (time <= m_points.front().time) return m_points.front().value;
        // After last point
        if (time >= m_points.back().time) return m_points.back().value;

        // Binary search for the segment containing time
        auto it = std::lower_bound(m_points.begin(), m_points.end(), time,
            [](const BreakPoint& bp, double t) { return bp.time < t; });

        if (it == m_points.begin()) return it->value;
        auto prev = it - 1;

        double dt = it->time - prev->time;
        if (dt < 1e-12) return it->value;

        double t = (time - prev->time) / dt;
        return prev->value + static_cast<float>(t) * (it->value - prev->value);
    }

    int pointCount() const { return static_cast<int>(m_points.size()); }

    const BreakPoint& point(int index) const { return m_points[index]; }
    BreakPoint& point(int index) { return m_points[index]; }

    bool empty() const { return m_points.empty(); }
    void clear() { m_points.clear(); }

    // Time range of the envelope
    double startTime() const { return m_points.empty() ? 0.0 : m_points.front().time; }
    double endTime()   const { return m_points.empty() ? 0.0 : m_points.back().time; }

    // Raw access for iteration
    const std::vector<BreakPoint>& points() const { return m_points; }

    // Serialization
    nlohmann::json toJson() const {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& bp : m_points)
            arr.push_back({{"t", bp.time}, {"v", bp.value}});
        return arr;
    }

    static AutomationEnvelope fromJson(const nlohmann::json& j) {
        AutomationEnvelope env;
        if (j.is_array()) {
            for (const auto& item : j) {
                double t = item.value("t", 0.0);
                float v  = item.value("v", 0.0f);
                env.m_points.push_back({t, v});
            }
        }
        return env;
    }

private:
    std::vector<BreakPoint> m_points;
};

} // namespace automation
} // namespace yawn
