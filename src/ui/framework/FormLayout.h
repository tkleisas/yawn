#pragma once
// FormLayout — Arranges label + widget pairs in rows for settings forms.
//
// Each row has a label on the left and a widget on the right.
// Supports optional section headers/separators.

#include "Widget.h"
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

class FormLayout : public Widget {
public:
    static constexpr float kRowHeight = 28.0f;
    static constexpr float kRowGap = 4.0f;
    static constexpr float kSectionGap = 12.0f;
    static constexpr float kLabelWidthRatio = 0.35f;
    static constexpr float kFieldPadding = 8.0f;

    FormLayout() = default;

    // ─── Row management ─────────────────────────────────────────────────

    void addRow(const std::string& label, Widget* widget) {
        m_rows.push_back({RowType::Field, label, widget});
        if (widget) addChild(widget);
    }

    void addSection(const std::string& title) {
        m_rows.push_back({RowType::Section, title, nullptr});
    }

    void addSeparator() {
        m_rows.push_back({RowType::Separator, "", nullptr});
    }

    int rowCount() const { return static_cast<int>(m_rows.size()); }

    Widget* widgetAt(int row) const {
        if (row < 0 || row >= rowCount()) return nullptr;
        return m_rows[row].widget;
    }

    void setLabelWidthRatio(float ratio) { m_labelRatio = ratio; }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        float totalH = 0;
        for (size_t i = 0; i < m_rows.size(); ++i) {
            totalH += rowHeight(i);
            if (i > 0) totalH += gapBefore(i);
        }
        // Measure each widget within its field width
        float fieldW = constraints.maxW * (1.0f - m_labelRatio) - kFieldPadding * 2;
        for (auto& row : m_rows) {
            if (row.widget) {
                row.widget->measure(
                    Constraints::tight(fieldW, kRowHeight), ctx);
            }
        }
        return constraints.constrain({constraints.maxW, totalH});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float labelW = bounds.w * m_labelRatio;
        float fieldX = bounds.x + labelW + kFieldPadding;
        float fieldW = bounds.w - labelW - kFieldPadding * 2;
        float y = bounds.y;

        for (size_t i = 0; i < m_rows.size(); ++i) {
            if (i > 0) y += gapBefore(i);
            float rh = rowHeight(i);

            if (m_rows[i].widget) {
                m_rows[i].widget->layout(
                    Rect{fieldX, y, fieldW, rh}, ctx);
            }
            m_rows[i].layoutY = y;
            y += rh;
        }
    }

    // ─── Geometry helpers ───────────────────────────────────────────────

    struct RowInfo {
        std::string label;
        float y = 0;
        float height = 0;
        float labelWidth = 0;
        bool isSection = false;
        bool isSeparator = false;
    };

    RowInfo rowInfo(int index) const {
        if (index < 0 || index >= rowCount()) return {};
        const auto& row = m_rows[index];
        return {
            row.label,
            row.layoutY,
            rowHeight(index),
            m_bounds.w * m_labelRatio,
            row.type == RowType::Section,
            row.type == RowType::Separator
        };
    }

    float labelWidth() const { return m_bounds.w * m_labelRatio; }

private:
    enum class RowType { Field, Section, Separator };

    struct Row {
        RowType type;
        std::string label;
        Widget* widget = nullptr;
        float layoutY = 0;
    };

    float rowHeight(size_t i) const {
        if (m_rows[i].type == RowType::Separator) return 1.0f;
        if (m_rows[i].type == RowType::Section) return kRowHeight;
        return kRowHeight;
    }

    float gapBefore(size_t i) const {
        if (m_rows[i].type == RowType::Section) return kSectionGap;
        if (m_rows[i].type == RowType::Separator) return kSectionGap * 0.5f;
        return kRowGap;
    }

    std::vector<Row> m_rows;
    float m_labelRatio = kLabelWidthRatio;
};

} // namespace fw
} // namespace ui
} // namespace yawn
