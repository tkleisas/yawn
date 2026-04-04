#pragma once
// DeviceWidget — Composite widget for a single audio device in the detail panel.
//
// Composes DeviceHeaderWidget + FwGrid + FwKnob + VisualizerWidget into a
// self-contained unit. Normal devices show a header bar and a knob grid;
// visualizer-type effects show a header, an oscilloscope/spectrum display,
// and a single-row knob strip underneath.

#include "DeviceHeaderWidget.h"
#include "FwGrid.h"
#include "InstrumentDisplayWidget.h"
#include "Primitives.h"
#include "VisualizerWidget.h"
#include "../../WidgetHint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#ifndef YAWN_TEST_BUILD
#include "../Renderer.h"
#include "../Font.h"
#endif

namespace yawn {
namespace ui {
namespace fw {

class DeviceWidget : public Widget {
public:
    using RemoveCallback     = std::function<void()>;
    using ParamChangeCallback = std::function<void(int paramIndex, float value)>;
    using ParamTouchCallback  = std::function<void(int paramIndex, float value, bool touching)>;

    // Per-parameter descriptor used to (re)build the knob grid.
    struct ParamInfo {
        int         index;
        std::string name;
        std::string unit;
        float       minVal, maxVal, defaultVal;
        bool        isBoolean;
        WidgetHint  widgetHint = WidgetHint::Knob;
        std::vector<std::string> valueLabels;  // for StepSelector display
    };

    // Type-erased wrapper around different parameter widget types.
    struct ParamSlot {
        enum Type { TKnob, TDentedKnob, TStepSelector, TToggle, TKnob360 };
        Widget* widget = nullptr;
        Type type = TKnob;

        void setValue(float v) {
            switch (type) {
            case TKnob:         static_cast<FwKnob*>(widget)->setValue(v); break;
            case TDentedKnob:   static_cast<FwDentedKnob*>(widget)->setValue(v); break;
            case TStepSelector: static_cast<FwStepSelector*>(widget)->setValue(static_cast<int>(v + 0.5f)); break;
            case TToggle:       static_cast<FwToggleSwitch*>(widget)->setState(v > 0.5f); break;
            case TKnob360:      static_cast<FwKnob360*>(widget)->setValue(v); break;
            }
        }

        float getValue() const {
            switch (type) {
            case TKnob:         return static_cast<FwKnob*>(widget)->value();
            case TDentedKnob:   return static_cast<FwDentedKnob*>(widget)->value();
            case TStepSelector: return static_cast<float>(static_cast<FwStepSelector*>(widget)->value());
            case TToggle:       return static_cast<FwToggleSwitch*>(widget)->state() ? 1.0f : 0.0f;
            case TKnob360:      return static_cast<FwKnob360*>(widget)->value();
            }
            return 0.0f;
        }

        bool isEditing() const {
            return (type == TKnob) ? static_cast<FwKnob*>(widget)->isEditing() : false;
        }

        bool forwardKeyDown(KeyEvent& ke) const {
            return (type == TKnob) ? static_cast<FwKnob*>(widget)->onKeyDown(ke) : false;
        }

        bool forwardTextInput(TextInputEvent& te) const {
            return (type == TKnob) ? static_cast<FwKnob*>(widget)->onTextInput(te) : false;
        }
    };

    // ─── Constants (matching DetailPanelWidget) ─────────────────────────

    static constexpr float kKnobSize       = 48.0f;
    static constexpr float kKnobSpacing    = 16.0f;
    static constexpr int   kMaxKnobRows    = 2;
    static constexpr float kCollapsedW     = 60.0f;
    static constexpr float kMinExpandedW   = 180.0f;
    static constexpr float kVisualizerMinW = 300.0f;
    static constexpr float kDeviceHeaderH  = 24.0f;

    // ─── Construction / destruction ─────────────────────────────────────

    DeviceWidget() {
        setName("DeviceWidget");
        addChild(&m_header);
        addChild(&m_knobGrid);

        m_knobGrid.setCellSize(kKnobSize + kKnobSpacing, kKnobSize + 22.0f);
        m_knobGrid.setMaxRows(kMaxKnobRows);
        m_knobGrid.setPadding(8);

        // Wire header → DeviceWidget forwarding
        m_header.setOnExpandToggle([this](bool expanded) {
            m_expanded = expanded;
            if (m_onExpandToggle) m_onExpandToggle(expanded);
        });
        m_header.setOnBypassToggle([this](bool bypassed) {
            if (m_onBypassToggle) m_onBypassToggle(bypassed);
        });
        m_header.setOnRemove([this]() {
            if (m_onRemove) m_onRemove();
        });
    }

    ~DeviceWidget() {
        clearKnobs();
        delete m_visualizer;
        delete m_vizKnobGrid;
        if (m_customPanel) { removeChild(m_customPanel); delete m_customPanel; }
        delete m_customBody;
    }

    // Non-copyable (owns heap-allocated children)
    DeviceWidget(const DeviceWidget&) = delete;
    DeviceWidget& operator=(const DeviceWidget&) = delete;

    // ─── Configuration ──────────────────────────────────────────────────

    void setDeviceName(const std::string& name) {
        m_deviceName = name;
        m_header.setDeviceName(name);
    }

    void setDeviceType(DeviceHeaderWidget::DeviceType type) {
        m_header.setDeviceType(type);
    }

    void setRemovable(bool r) { m_header.setRemovable(r); }

    void setVisualizer(bool isViz, const char* vizType = "oscilloscope") {
        if (isViz == m_isVisualizer) return;
        m_isVisualizer = isViz;

        if (isViz) {
            // Create visualizer and its single-row knob grid
            auto mode = VisualizerWidget::Mode::Oscilloscope;
            if (vizType) {
                std::string vt(vizType);
                if (vt == "spectrum") mode = VisualizerWidget::Mode::Spectrum;
                else if (vt == "tuner") mode = VisualizerWidget::Mode::Tuner;
            }
            m_visualizer = new VisualizerWidget(mode);
            m_vizKnobGrid = new FwGrid();
            m_vizKnobGrid->setCellSize(kKnobSize + kKnobSpacing, kKnobSize + 22.0f);
            m_vizKnobGrid->setMaxRows(1);
            m_vizKnobGrid->setPadding(8);
            addChild(m_visualizer);
            addChild(m_vizKnobGrid);
        } else {
            // Tear down visualizer children
            if (m_visualizer)  { removeChild(m_visualizer);  delete m_visualizer;  m_visualizer = nullptr; }
            if (m_vizKnobGrid) { removeChild(m_vizKnobGrid); delete m_vizKnobGrid; m_vizKnobGrid = nullptr; }
        }
    }

    // ─── Custom instrument display panel ────────────────────────────────

    void setCustomPanel(Widget* panel, float height, float minWidth = 0) {
        if (m_customPanel) { removeChild(m_customPanel); delete m_customPanel; }
        m_customPanel = panel;
        m_customPanelH = height;
        m_customMinW = minWidth;
        if (panel) addChild(panel);
    }

    Widget* customPanel() const { return m_customPanel; }

    // ─── Custom body (replaces knob grid entirely for instruments) ───────

    void setCustomBody(CustomDeviceBody* body) {
        delete m_customBody;
        m_customBody = body;
    }
    CustomDeviceBody* customBody() const { return m_customBody; }

    // ─── State ──────────────────────────────────────────────────────────

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool e) {
        m_expanded = e;
        m_header.setExpanded(e);
    }

    bool isBypassed() const { return m_header.isBypassed(); }
    void setBypassed(bool b) { m_header.setBypassed(b); }

    // ─── Parameters ─────────────────────────────────────────────────────

    void setParameters(const std::vector<ParamInfo>& params) {
        clearKnobs();
        if (m_isVisualizer && m_vizKnobGrid) {
            buildKnobs(params, m_vizKnobs, *m_vizKnobGrid, 1);
        } else {
            buildKnobs(params, m_knobs, m_knobGrid, kMaxKnobRows);
        }
    }

    void updateParamValue(int index, float value) {
        if (m_customBody) { m_customBody->updateParamValue(index, value); return; }
        std::string key = std::to_string(index);
        for (auto& s : m_knobs)    if (s.widget->name() == key) { s.setValue(value); return; }
        for (auto& s : m_vizKnobs) if (s.widget->name() == key) { s.setValue(value); return; }
    }

    // ─── Visualizer data feed ───────────────────────────────────────────

    void setVisualizerData(const float* data, int size) {
        if (m_visualizer) m_visualizer->setData(data, size);
    }

    // ─── Callbacks ──────────────────────────────────────────────────────

    void setOnRemove(RemoveCallback cb) { m_onRemove = std::move(cb); }
    void setOnParamChange(ParamChangeCallback cb) { m_onParamChange = std::move(cb); }
    void setOnParamTouch(ParamTouchCallback cb) { m_onParamTouch = std::move(cb); }
    void setOnBypassToggle(DeviceHeaderWidget::ToggleCallback cb) { m_onBypassToggle = std::move(cb); }
    void setOnExpandToggle(DeviceHeaderWidget::ToggleCallback cb) { m_onExpandToggle = std::move(cb); }
    void setOnDragStart(DeviceHeaderWidget::ActionCallback cb) {
        m_onDragStart = std::move(cb);
        m_header.setOnDragStart([this]() {
            if (m_onDragStart) m_onDragStart();
        });
    }

    // ─── Knob text-edit forwarding (for GroupedKnobBody) ────────────────

    bool hasEditingKnob() const {
        if (m_customBody) return m_customBody->hasEditingKnob();
        for (auto& s : m_knobs)    if (s.isEditing()) return true;
        for (auto& s : m_vizKnobs) if (s.isEditing()) return true;
        return false;
    }

    bool forwardKeyDown(int key) {
        if (m_customBody) return m_customBody->forwardKeyDown(key);
        KeyEvent ke; ke.keyCode = key;
        for (auto& s : m_knobs)    if (s.isEditing()) return s.forwardKeyDown(ke);
        for (auto& s : m_vizKnobs) if (s.isEditing()) return s.forwardKeyDown(ke);
        return false;
    }

    bool forwardTextInput(const char* text) {
        if (m_customBody) return m_customBody->forwardTextInput(text);
        TextInputEvent te;
        std::strncpy(te.text, text, sizeof(te.text) - 1);
        te.text[sizeof(te.text) - 1] = '\0';
        for (auto& s : m_knobs)    if (s.isEditing()) return s.forwardTextInput(te);
        for (auto& s : m_vizKnobs) if (s.isEditing()) return s.forwardTextInput(te);
        return false;
    }

    void cancelEditingKnobs() {
        if (m_customBody) { m_customBody->cancelEditingKnobs(); return; }
        KeyEvent ke; ke.keyCode = 27;
        for (auto& s : m_knobs)    if (s.isEditing()) s.forwardKeyDown(ke);
        for (auto& s : m_vizKnobs) if (s.isEditing()) s.forwardKeyDown(ke);
    }

    // ─── Width calculation (matches DetailPanelWidget::DevicePanel::deviceWidth) ─

    float preferredWidth() const {
        if (!m_expanded) return kCollapsedW;
        if (m_customBody) {
            float w = m_customBody->preferredBodyWidth() + 16.0f;
            return std::max(kMinExpandedW, w);
        }
        int paramCount = static_cast<int>(m_knobs.size() + m_vizKnobs.size());
        if (m_isVisualizer) {
            int cols = paramCount == 0 ? 0
                       : static_cast<int>(std::ceil(static_cast<float>(paramCount) / 1.0f));
            float knobW = cols * (kKnobSize + kKnobSpacing) + 16.0f;
            return std::max(kVisualizerMinW, knobW);
        }
        int cols = paramCount == 0 ? 1
                   : static_cast<int>(std::ceil(static_cast<float>(paramCount)
                                                / static_cast<float>(kMaxKnobRows)));
        float w = cols * (kKnobSize + kKnobSpacing) + 24.0f;
        if (m_customPanel && m_customMinW > 0)
            w = std::max(w, m_customMinW);
        return std::max(kMinExpandedW, w);
    }

    // ─── Layout lifecycle ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext& /*ctx*/) override {
        float w = std::min(preferredWidth(), c.maxW);
        w = std::max(w, c.minW);
        float h = std::min(c.maxH, std::max(c.minH, c.maxH));
        return {w, h};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        float x = bounds.x;
        float y = bounds.y;
        float w = bounds.w;
        float h = bounds.h;

        m_header.layout({x, y, w, kDeviceHeaderH}, ctx);

        if (!m_expanded) return;

        float bodyY = y + kDeviceHeaderH;
        float bodyH = h - kDeviceHeaderH;

        if (m_isVisualizer && m_visualizer) {
            float vizH = bodyH - 68.0f;
            if (vizH < 0) vizH = 0;
            m_visualizer->layout({x + 4, bodyY + 2, w - 8, vizH - 4}, ctx);
            if (m_vizKnobGrid)
                m_vizKnobGrid->layout({x + 8, bodyY + vizH + 2, w - 16, 64}, ctx);
        } else if (m_customBody) {
            m_customBody->layout({x + 4, bodyY + 2, w - 8, bodyH - 4}, ctx);
        } else {
            if (m_customPanel) {
                m_customPanel->layout({x + 4, bodyY + 2, w - 8, m_customPanelH}, ctx);
                float used = m_customPanelH + 4;
                bodyY += used;
                bodyH -= used;
            }
            m_knobGrid.layout({x + 8, bodyY + 4, w - 16, bodyH - 8}, ctx);
        }
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void render(UIContext& ctx) override {
        if (!m_visible) return;
        paint(ctx);
    }

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        if (!ctx.renderer || !ctx.font) return;

        // Device background
        Color bg = isBypassed() ? Color{30, 30, 34, 255} : Color{36, 36, 42, 255};
        ctx.renderer->drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);

        m_header.paint(ctx);

        if (!m_expanded) {
            // Render device name vertically in collapsed body
            float ty = m_bounds.y + kDeviceHeaderH + 4;
            float sml = 10.0f / Theme::kFontSize;
            const char* nm = m_deviceName.c_str();
            char buf[2] = {0, 0};
            for (int ci = 0; nm[ci] && ty < m_bounds.y + m_bounds.h - 10; ++ci) {
                buf[0] = nm[ci];
                ctx.font->drawText(*ctx.renderer, buf,
                                   m_bounds.x + m_bounds.w * 0.5f - 3, ty,
                                   sml, Theme::textDim);
                ty += 12;
            }
            return;
        }

        if (m_isVisualizer && m_visualizer) {
            m_visualizer->paint(ctx);
            if (m_vizKnobGrid) m_vizKnobGrid->paint(ctx);
        } else if (m_customBody) {
            m_customBody->paint(ctx);
        } else {
            if (m_customPanel) m_customPanel->paint(ctx);
            m_knobGrid.paint(ctx);
        }
#endif
    }

    // ─── Event handling ─────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        if (m_header.bounds().contains(e.x, e.y))
            return m_header.onMouseDown(e);

        if (!m_expanded) return false;

        if (m_isVisualizer && m_vizKnobGrid) {
            if (m_vizKnobGrid->bounds().contains(e.x, e.y)) {
                for (auto& s : m_vizKnobs) {
                    if (s.widget->bounds().contains(e.x, e.y))
                        return s.widget->onMouseDown(e);
                }
            }
        } else if (m_customBody) {
            return m_customBody->onMouseDown(e);
        } else {
            if (m_knobGrid.bounds().contains(e.x, e.y)) {
                for (auto& s : m_knobs) {
                    if (s.widget->bounds().contains(e.x, e.y))
                        return s.widget->onMouseDown(e);
                }
            }
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_header.bounds().contains(e.x, e.y))
            return m_header.onMouseUp(e);

        if (!m_expanded) return false;

        if (m_isVisualizer) {
            for (auto& s : m_vizKnobs)
                if (s.widget->bounds().contains(e.x, e.y)) return s.widget->onMouseUp(e);
        } else if (m_customBody) {
            return m_customBody->onMouseUp(e);
        } else {
            for (auto& s : m_knobs)
                if (s.widget->bounds().contains(e.x, e.y)) return s.widget->onMouseUp(e);
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        // Captured knobs receive moves regardless of position
        if (m_isVisualizer) {
            for (auto& s : m_vizKnobs)
                if (s.widget->onMouseMove(e)) return true;
        } else if (m_customBody) {
            return m_customBody->onMouseMove(e);
        } else {
            for (auto& s : m_knobs)
                if (s.widget->onMouseMove(e)) return true;
        }
        return false;
    }

private:
    DeviceHeaderWidget          m_header;
    FwGrid                      m_knobGrid;
    std::vector<ParamSlot>      m_knobs;        // owned
    VisualizerWidget*           m_visualizer   = nullptr;  // owned, null if not visualizer
    FwGrid*                     m_vizKnobGrid  = nullptr;  // owned, single-row grid for viz knobs
    std::vector<ParamSlot>      m_vizKnobs;     // owned

    bool        m_isVisualizer = false;
    bool        m_expanded     = true;
    std::string m_deviceName;

    Widget*     m_customPanel  = nullptr;  // owned, instrument display panel
    float       m_customPanelH = 0;
    float       m_customMinW   = 0;

    CustomDeviceBody* m_customBody = nullptr;  // owned, replaces knob grid for instruments

    RemoveCallback                    m_onRemove;
    ParamChangeCallback               m_onParamChange;
    ParamTouchCallback                m_onParamTouch;
    DeviceHeaderWidget::ToggleCallback m_onBypassToggle;
    DeviceHeaderWidget::ToggleCallback m_onExpandToggle;
    DeviceHeaderWidget::ActionCallback m_onDragStart;

    // ─── Knob management ────────────────────────────────────────────────

    void clearKnobs() {
        for (auto& s : m_knobs)    { m_knobGrid.removeChild(s.widget); delete s.widget; }
        m_knobs.clear();
        for (auto& s : m_vizKnobs) { if (m_vizKnobGrid) m_vizKnobGrid->removeChild(s.widget); delete s.widget; }
        m_vizKnobs.clear();
    }

    void buildKnobs(const std::vector<ParamInfo>& params,
                    std::vector<ParamSlot>& slots, FwGrid& grid, int maxRows) {
        grid.setMaxRows(maxRows);
        for (const auto& p : params) {
            ParamSlot slot;
            WidgetHint hint = p.widgetHint;

            // Auto-resolve: booleans become toggles
            if (hint == WidgetHint::Knob && p.isBoolean)
                hint = WidgetHint::Toggle;

            switch (hint) {
            case WidgetHint::DentedKnob: {
                auto* dk = new FwDentedKnob();
                dk->setName(std::to_string(p.index));
                dk->setRange(p.minVal, p.maxVal);
                dk->setDefault(p.defaultVal);
                dk->setValue(p.defaultVal);
                dk->setLabel(p.name);
                dk->addDetent(p.defaultVal, 0.04f);
                dk->setFormat([unit = p.unit](float v) {
                    return formatValue(v, unit, false);
                });
                dk->setOnChange([this, idx = p.index](float v) {
                    if (m_onParamChange) m_onParamChange(idx, v);
                });
                dk->setOnTouch([this, idx = p.index, dk](bool touching) {
                    if (m_onParamTouch) m_onParamTouch(idx, dk->value(), touching);
                });
                slot.widget = dk;
                slot.type = ParamSlot::TDentedKnob;
                break;
            }
            case WidgetHint::StepSelector: {
                auto* ss = new FwStepSelector();
                ss->setName(std::to_string(p.index));
                ss->setRange(static_cast<int>(p.minVal + 0.5f),
                             static_cast<int>(p.maxVal + 0.5f));
                ss->setValue(static_cast<int>(p.defaultVal + 0.5f));
                ss->setLabel(p.name);
                if (!p.valueLabels.empty()) {
                    auto labels = p.valueLabels;
                    int mn = static_cast<int>(p.minVal + 0.5f);
                    ss->setFormat([labels, mn](int v) -> std::string {
                        int idx = v - mn;
                        if (idx >= 0 && idx < static_cast<int>(labels.size()))
                            return labels[idx];
                        return std::to_string(v);
                    });
                } else {
                    ss->setFormat([unit = p.unit](int v) -> std::string {
                        if (unit == "st") return std::to_string(v);
                        return std::to_string(v);
                    });
                }
                ss->setOnChange([this, idx = p.index](int v) {
                    if (m_onParamChange) m_onParamChange(idx, static_cast<float>(v));
                });
                ss->setOnTouch([this, idx = p.index, ss](bool touching) {
                    if (m_onParamTouch)
                        m_onParamTouch(idx, static_cast<float>(ss->value()), touching);
                });
                slot.widget = ss;
                slot.type = ParamSlot::TStepSelector;
                break;
            }
            case WidgetHint::Toggle: {
                auto* ts = new FwToggleSwitch();
                ts->setName(std::to_string(p.index));
                ts->setState(p.defaultVal > 0.5f);
                ts->setLabel(p.name);
                if (p.valueLabels.size() >= 2)
                    ts->setLabels(p.valueLabels[0], p.valueLabels[1]);
                else
                    ts->setLabels("Off", "On");
                ts->setOnChange([this, idx = p.index](bool state) {
                    if (m_onParamChange) m_onParamChange(idx, state ? 1.0f : 0.0f);
                });
                ts->setOnTouch([this, idx = p.index, ts](bool touching) {
                    if (m_onParamTouch)
                        m_onParamTouch(idx, ts->state() ? 1.0f : 0.0f, touching);
                });
                slot.widget = ts;
                slot.type = ParamSlot::TToggle;
                break;
            }
            case WidgetHint::Knob360: {
                auto* k360 = new FwKnob360();
                k360->setName(std::to_string(p.index));
                k360->setRange(p.minVal, p.maxVal);
                k360->setDefault(p.defaultVal);
                k360->setValue(p.defaultVal);
                k360->setLabel(p.name);
                k360->setFormat([unit = p.unit](float v) {
                    return formatValue(v, unit, false);
                });
                k360->setOnChange([this, idx = p.index](float v) {
                    if (m_onParamChange) m_onParamChange(idx, v);
                });
                k360->setOnTouch([this, idx = p.index, k360](bool touching) {
                    if (m_onParamTouch) m_onParamTouch(idx, k360->value(), touching);
                });
                slot.widget = k360;
                slot.type = ParamSlot::TKnob360;
                break;
            }
            default: /* WidgetHint::Knob */ {
                auto* k = new FwKnob();
                k->setName(std::to_string(p.index));
                k->setRange(p.minVal, p.maxVal);
                k->setDefault(p.defaultVal);
                k->setValue(p.defaultVal);
                k->setLabel(p.name);
                k->setBoolean(p.isBoolean);
                k->setFormatCallback([unit = p.unit, isBool = p.isBoolean](float v) {
                    return formatValue(v, unit, isBool);
                });
                k->setOnChange([this, idx = p.index](float v) {
                    if (m_onParamChange) m_onParamChange(idx, v);
                });
                k->setOnTouch([this, idx = p.index, k](bool touching) {
                    if (m_onParamTouch) m_onParamTouch(idx, k->value(), touching);
                });
                slot.widget = k;
                slot.type = ParamSlot::TKnob;
                break;
            }
            }
            slots.push_back(slot);
            grid.addChild(slot.widget);
        }
    }

    // ─── Value formatting (matches DetailPanelWidget::formatValue) ──────
public:
    static std::string formatValue(float value, const std::string& unit, bool isBoolean) {
        if (isBoolean) return value > 0.5f ? "On" : "Off";
        char buf[32];
        if (unit == "Hz") {
            if (value >= 1000.0f)
                std::snprintf(buf, sizeof(buf), "%.1fk", value / 1000.0f);
            else
                std::snprintf(buf, sizeof(buf), "%.0f", value);
        } else if (unit == "dB") {
            std::snprintf(buf, sizeof(buf), "%.1f", value);
        } else if (unit == "ms") {
            std::snprintf(buf, sizeof(buf), "%.0f", value);
        } else if (unit == "%") {
            std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f", value);
        }
        return buf;
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
