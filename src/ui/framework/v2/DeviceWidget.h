#pragma once
// fw2::DeviceWidget — composite device strip in the detail panel.
//
// Migrated from v1 fw::DeviceWidget. Owns a fw2::DeviceHeaderWidget +
// optional fw2::VisualizerWidget + fw2::FwGrid (as a layout-coord
// helper) + a vector of ParamSlot (each wrapping a fw2 knob / toggle /
// step selector) + an optional fw2::CustomDeviceBody (for instruments
// that need grouped knob layouts, like Subtractive Synth).
//
// The custom-panel slot is still a v1 `fw::Widget*` because the small
// auxiliary display widgets (LFODisplayWidget etc.) live in the
// still-v1 InstrumentDisplayWidget.h. The hosting panel (DetailPanel
// for now) drives its v1 lifecycle with a cached v1 UIContext; fw2
// DeviceWidget just tracks the rect + delegates through toFw1Mouse.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/DeviceHeaderWidget.h"
#include "ui/framework/v2/VisualizerWidget.h"
#include "ui/framework/v2/FwGrid.h"
#include "ui/framework/v2/GroupedKnobBody.h"  // fw2 CustomDeviceBody
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/StepSelector.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/V1EventBridge.h"
#include "ui/Theme.h"
#include "util/Logger.h"
#include "WidgetHint.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class DeviceWidget : public Widget {
public:
    using RemoveCallback           = std::function<void()>;
    using ParamChangeCallback      = std::function<void(int paramIndex, float value)>;
    using ParamTouchCallback       = std::function<void(int paramIndex, float value, bool touching)>;
    using ParamRightClickCallback  = std::function<void(int paramIndex, float x, float y)>;
    using CCLabelCallback          = std::function<std::string(int paramIndex)>;

    struct ParamInfo {
        int         index;
        std::string name;
        std::string unit;
        float       minVal, maxVal, defaultVal;
        bool        isBoolean;
        ::yawn::WidgetHint widgetHint = ::yawn::WidgetHint::Knob;
        std::vector<std::string> valueLabels{};
        void (*formatFn)(float, char*, int) = nullptr;
    };

    struct ParamSlot {
        enum Type { TKnob, TDentedKnob, TStepSelector, TToggle, TKnob360 };
        FwKnob*         knob    = nullptr;
        FwToggle*       toggle  = nullptr;
        FwStepSelector* stepSel = nullptr;
        Type type = TKnob;
        int  paramIndex = -1;

        bool isKnob() const {
            return type == TKnob || type == TDentedKnob || type == TKnob360;
        }

        Widget* w2() const {
            if (isKnob())              return knob;
            if (type == TToggle)       return toggle;
            if (type == TStepSelector) return stepSel;
            return nullptr;
        }

        void setValue(float v) {
            if (isKnob())              { knob->setValue(v); return; }
            if (type == TToggle)       { toggle->setState(v > 0.5f); return; }
            if (type == TStepSelector) { stepSel->setValue(static_cast<int>(v + 0.5f)); return; }
        }

        float getValue() const {
            if (isKnob())              return knob->value();
            if (type == TToggle)       return toggle->state() ? 1.0f : 0.0f;
            if (type == TStepSelector) return static_cast<float>(stepSel->value());
            return 0.0f;
        }

        const Rect& bounds() const {
            if (auto* w = w2()) return w->bounds();
            static const Rect kEmpty{};
            return kEmpty;
        }

        bool isEditing() const {
            return isKnob() && knob->isEditing();
        }

        bool forwardKeyDown(int key) const {
            if (!isEditing()) return false;
            KeyEvent ke;
            switch (key) {
                case 27 /*SDLK_ESCAPE*/:    ke.key = Key::Escape;    break;
                case 13 /*SDLK_RETURN*/:    ke.key = Key::Enter;     break;
                case 8  /*SDLK_BACKSPACE*/: ke.key = Key::Backspace; break;
                default:                    return false;
            }
            return knob->dispatchKeyDown(ke);
        }

        bool forwardTextInput(const char* text) const {
            if (!isEditing()) return false;
            knob->takeTextInput(text ? text : "");
            return true;
        }
    };

    // Layout constants (shared with v1 wrapper via constexpr re-exports).
    // Cell size (kKnobSize + kKnobSpacing wide, kKnobSize + 22 tall =
    // 60×66) is tuned smaller than v1's {64,70} so generic-device knobs
    // render at the same visible size as the GroupedKnobBody knobs in
    // Subtractive Synth (cell 62×68 there; paintKnob picks disc-diameter
    // from min(cell.w, cell.h - labels)).
    static constexpr float kKnobSize       = 44.0f;
    static constexpr float kKnobSpacing    = 16.0f;
    static constexpr int   kMaxKnobRows    = 2;
    static constexpr float kCollapsedW     = 60.0f;
    static constexpr float kMinExpandedW   = 180.0f;
    static constexpr float kVisualizerMinW = 300.0f;
    static constexpr float kDeviceHeaderH  = 24.0f;

    // Collapsed-state letter stack font size. v1 used 10/26 scale against
    // the 48-px bake ≈ 18.5 px; tune down per the font-scale lesson.
    static constexpr float kFsCollapsed = 16.0f;
    // CC-label font size beneath knobs. v1 used 7/26 ≈ 12.9 px.
    static constexpr float kFsCCLabel   = 12.0f;
    // Knob label measuring for cell-width sizing (v1: 9/26 ≈ 16.6 px).
    static constexpr float kFsKnobLabel = 14.0f;

    // ─── Construction / destruction ─────────────────────────────────

    DeviceWidget() {
        setName("DeviceWidget");

        m_knobGrid.setCellSize(kKnobSize + kKnobSpacing, kKnobSize + 22.0f);
        m_knobGrid.setMaxRows(kMaxKnobRows);
        m_knobGrid.setGridPadding(8.0f);

        // Header → DeviceWidget wiring
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
        delete m_customPanel;
        delete m_customBody;
    }

    DeviceWidget(const DeviceWidget&) = delete;
    DeviceWidget& operator=(const DeviceWidget&) = delete;

    // ─── Configuration ──────────────────────────────────────────────

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
            auto mode = VisualizerWidget::Mode::Oscilloscope;
            if (vizType) {
                std::string vt(vizType);
                if      (vt == "spectrum") mode = VisualizerWidget::Mode::Spectrum;
                else if (vt == "tuner")    mode = VisualizerWidget::Mode::Tuner;
            }
            m_visualizer  = new VisualizerWidget(mode);
            m_vizKnobGrid = new FwGrid();
            m_vizKnobGrid->setCellSize(kKnobSize + kKnobSpacing, kKnobSize + 22.0f);
            m_vizKnobGrid->setMaxRows(1);
            m_vizKnobGrid->setGridPadding(8.0f);
        } else {
            delete m_visualizer;  m_visualizer  = nullptr;
            delete m_vizKnobGrid; m_vizKnobGrid = nullptr;
        }
    }

    // ─── v1-crossover slots (owned) ──────────────────────────────────
    // These point at v1 widgets from the still-v1 InstrumentDisplayWidget
    // cluster (CustomDeviceBody + auxiliary display panels). DeviceWidget
    // takes ownership — replacing or destroying the DeviceWidget deletes
    // the previous instance. Inline v1 Widget methods are reached
    // through the v1 lifecycle bridge driven by the panel hosting this
    // DeviceWidget.

    void setCustomPanel(Widget* panel, float h, float minW = 0.0f) {
        if (m_customPanel != panel) delete m_customPanel;
        m_customPanel  = panel;
        m_customPanelH = h;
        m_customMinW   = minW;
    }
    Widget* customPanel() const { return m_customPanel; }

    void setCustomBody(CustomDeviceBody* body) {
        if (m_customBody != body) delete m_customBody;
        m_customBody = body;
    }
    CustomDeviceBody* customBody() const { return m_customBody; }

    Rect customPanelLayoutRect() const { return m_customPanelRect; }
    Rect customBodyLayoutRect() const  { return m_customBodyRect; }

    // ─── State ──────────────────────────────────────────────────────

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool e) {
        m_expanded = e;
        m_header.setExpanded(e);
    }

    bool isBypassed() const { return m_header.isBypassed(); }
    void setBypassed(bool b) { m_header.setBypassed(b); }

    // ─── Parameters ─────────────────────────────────────────────────

    void setParameters(const std::vector<ParamInfo>& params) {
        clearKnobs();
        if (m_isVisualizer && m_vizKnobGrid)
            buildKnobs(params, m_vizKnobs, *m_vizKnobGrid, 1);
        else
            buildKnobs(params, m_knobs, m_knobGrid, kMaxKnobRows);

        // Estimate cell width from label lengths (pre-font-metrics seed).
        size_t maxLen = 0;
        for (auto& p : params)
            maxLen = std::max(maxLen, p.name.size());
        const float estW    = maxLen * 10.0f + 8.0f;
        const float minCell = kKnobSize + kKnobSpacing;
        m_cachedCellW = std::max(minCell, estW);
        m_knobGrid.setCellSize(m_cachedCellW, kKnobSize + 22.0f);
        if (m_vizKnobGrid) m_vizKnobGrid->setCellSize(m_cachedCellW, kKnobSize + 22.0f);
    }

    void updateParamValue(int index, float value) {
        if (m_customBody) { m_customBody->updateParamValue(index, value); return; }
        auto shouldSkip = [](const ParamSlot& s) {
            return s.isKnob() && (s.knob->isDragging() || s.knob->isEditing());
        };
        for (auto& s : m_knobs)
            if (s.paramIndex == index) { if (!shouldSkip(s)) s.setValue(value); return; }
        for (auto& s : m_vizKnobs)
            if (s.paramIndex == index) { if (!shouldSkip(s)) s.setValue(value); return; }
    }

    void setVisualizerData(const float* data, int size) {
        if (m_visualizer) m_visualizer->setData(data, size);
    }

    // ─── Callbacks ──────────────────────────────────────────────────

    void setOnRemove(RemoveCallback cb)                   { m_onRemove = std::move(cb); }
    void setOnParamChange(ParamChangeCallback cb)          { m_onParamChange = std::move(cb); }
    void setOnParamTouch(ParamTouchCallback cb)            { m_onParamTouch = std::move(cb); }
    void setOnParamRightClick(ParamRightClickCallback cb)  { m_onParamRightClick = std::move(cb); }
    void setCCLabelCallback(CCLabelCallback cb)            { m_ccLabelCb = std::move(cb); }
    void setOnBypassToggle(DeviceHeaderWidget::ToggleCallback cb) { m_onBypassToggle = std::move(cb); }
    void setOnExpandToggle(DeviceHeaderWidget::ToggleCallback cb) { m_onExpandToggle = std::move(cb); }
    void setOnDragStart(DeviceHeaderWidget::ActionCallback cb) {
        m_onDragStart = std::move(cb);
        m_header.setOnDragStart([this]() { if (m_onDragStart) m_onDragStart(); });
    }
    void setOnPresetClick(DeviceHeaderWidget::PresetClickCallback cb) {
        m_onPresetClick = std::move(cb);
        m_header.setOnPresetClick([this](float x, float y) {
            if (m_onPresetClick) m_onPresetClick(x, y);
        });
    }
    void setPresetName(const std::string& name) { m_header.setPresetName(name); }

    // ─── Knob edit forwarding (customBody is handled by wrapper) ────

    bool hasEditingKnob() const {
        for (auto& s : m_knobs)    if (s.isEditing()) return true;
        for (auto& s : m_vizKnobs) if (s.isEditing()) return true;
        return false;
    }

    bool forwardKeyDown(int key) {
        for (auto& s : m_knobs)    if (s.isEditing()) return s.forwardKeyDown(key);
        for (auto& s : m_vizKnobs) if (s.isEditing()) return s.forwardKeyDown(key);
        return false;
    }

    bool forwardTextInput(const char* text) {
        for (auto& s : m_knobs)    if (s.isEditing()) return s.forwardTextInput(text);
        for (auto& s : m_vizKnobs) if (s.isEditing()) return s.forwardTextInput(text);
        return false;
    }

    void cancelEditingKnobs() {
        for (auto& s : m_knobs)    if (s.isEditing()) s.knob->endEdit(/*commit*/false);
        for (auto& s : m_vizKnobs) if (s.isEditing()) s.knob->endEdit(/*commit*/false);
    }

    // ─── Width calculation (matches v1) ─────────────────────────────

    // Rough header width needed to fit expand/bypass/name/preset/remove
    // without clipping. Chars at ~12 px/col at the 25-px device-name
    // font size, plus fixed gutters. Used as a floor for preferredWidth.
    float headerMinWidth() const {
        constexpr float kExpand = 16.0f, kBypass = 24.0f, kRemove = 24.0f;
        constexpr float kPresetW = 72.0f;  // "Preset ▾" at kFsSmall
        constexpr float kGutters = 4 + 2 + 6 + 8 + 4;  // small paddings
        const float nameW = static_cast<float>(m_deviceName.size()) * 12.0f;
        return kExpand + kBypass + nameW + kPresetW + kRemove + kGutters;
    }

    float preferredWidth() const {
        if (!m_expanded) return kCollapsedW;
        if (m_customBody) {
            // Let the v1 CustomDeviceBody dictate its preferred width —
            // otherwise the device strip is sized only by the header
            // and the rightmost knob columns fall outside customBody's
            // bounds (which are clamped to our own w-8) so clicks never
            // reach them.
            const float bodyW = m_customBody->preferredBodyWidth() + 16.0f;
            return std::max({kMinExpandedW, headerMinWidth(), bodyW});
        }
        const float cellW = std::max(kKnobSize + kKnobSpacing, m_cachedCellW);
        const int paramCount = static_cast<int>(m_knobs.size() + m_vizKnobs.size());
        if (m_isVisualizer) {
            int cols = paramCount == 0 ? 0
                      : static_cast<int>(std::ceil(static_cast<float>(paramCount) / 1.0f));
            const float knobW = cols * cellW + 16.0f;
            return std::max({kVisualizerMinW, knobW, headerMinWidth()});
        }
        int cols = paramCount == 0 ? 1
                  : static_cast<int>(std::ceil(static_cast<float>(paramCount)
                                               / static_cast<float>(kMaxKnobRows)));
        float w = cols * cellW + 24.0f;
        if (m_customPanel && m_customMinW > 0) w = std::max(w, m_customMinW);
        return std::max({kMinExpandedW, w, headerMinWidth()});
    }

    // ─── fw2 lifecycle ──────────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext& ctx) override {
        updateCellWidth(ctx);
        float w = std::min(preferredWidth(), c.maxW);
        w = std::max(w, c.minW);
        const float h = std::min(c.maxH, std::max(c.minH, c.maxH));
        return {w, h};
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float x = bounds.x, y = bounds.y, w = bounds.w, h = bounds.h;

        m_header.measure(Constraints::tight(w, kDeviceHeaderH), ctx);
        m_header.layout({x, y, w, kDeviceHeaderH}, ctx);

        m_customPanelRect = {};
        m_customBodyRect  = {};

        if (!m_expanded) return;

        float bodyY = y + kDeviceHeaderH;
        float bodyH = h - kDeviceHeaderH;

        if (m_isVisualizer && m_visualizer) {
            float vizH = bodyH - 68.0f;
            if (vizH < 0) vizH = 0;
            m_visualizer->measure(Constraints::tight(w - 8, vizH - 4), ctx);
            m_visualizer->layout({x + 4, bodyY + 2, w - 8, vizH - 4}, ctx);
            if (m_vizKnobGrid) {
                m_vizKnobGrid->measure(Constraints::tight(w - 16, 64), ctx);
                m_vizKnobGrid->layout({x + 8, bodyY + vizH + 2, w - 16, 64}, ctx);
                layoutKnobsInGrid(m_vizKnobs, *m_vizKnobGrid, /*maxRows*/1, ctx);
            }
        } else if (m_customBody) {
            m_customBodyRect = {x + 4, bodyY + 2, w - 8, bodyH - 4};
            m_customBody->measure(
                Constraints::tight(m_customBodyRect.w, m_customBodyRect.h), ctx);
            m_customBody->layout(m_customBodyRect, ctx);
        } else {
            if (m_customPanel) {
                m_customPanelRect = {x + 4, bodyY + 2, w - 8, m_customPanelH};
                m_customPanel->measure(
                    Constraints::tight(m_customPanelRect.w, m_customPanelRect.h), ctx);
                m_customPanel->layout(m_customPanelRect, ctx);
                const float used = m_customPanelH + 4;
                bodyY += used;
                bodyH -= used;
            }
            m_knobGrid.measure(Constraints::tight(w - 16, bodyH - 8), ctx);
            m_knobGrid.layout({x + 8, bodyY + 4, w - 16, bodyH - 8}, ctx);
            layoutKnobsInGrid(m_knobs, m_knobGrid, kMaxKnobRows, ctx);
        }
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        // Device background
        const Color bg = isBypassed() ? Color{30, 30, 34, 255}
                                       : Color{36, 36, 42, 255};
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, bg);

        m_header.render(ctx);

        if (!m_expanded) {
            // Render device name vertically in collapsed body
            float ty = m_bounds.y + kDeviceHeaderH + 4;
            const char* nm = m_deviceName.c_str();
            char buf[2] = {0, 0};
            auto* tm = ctx.textMetrics;
            for (int ci = 0; nm[ci] && ty < m_bounds.y + m_bounds.h - 10; ++ci) {
                buf[0] = nm[ci];
                if (tm) tm->drawText(r, buf,
                                      m_bounds.x + m_bounds.w * 0.5f - 3, ty,
                                      kFsCollapsed, ::yawn::ui::Theme::textDim);
                ty += 12;
            }
            return;
        }

        auto renderV2Knobs = [&](std::vector<ParamSlot>& slots) {
            for (auto& s : slots)
                if (auto* v2w = s.w2()) v2w->render(ctx);
        };

        if (m_isVisualizer && m_visualizer) {
            m_visualizer->render(ctx);
            if (m_vizKnobGrid) m_vizKnobGrid->render(ctx);
            renderV2Knobs(m_vizKnobs);
        } else if (m_customBody) {
            m_customBody->render(ctx);
        } else {
            if (m_customPanel) m_customPanel->render(ctx);
            m_knobGrid.render(ctx);
            renderV2Knobs(m_knobs);
        }

        // CC labels on mapped knobs
        if (m_ccLabelCb && ctx.textMetrics) {
            auto* tm = ctx.textMetrics;
            const Color ccCol{100, 180, 255, 255};
            auto drawLabels = [&](const std::vector<ParamSlot>& slots) {
                for (auto& s : slots) {
                    auto lbl = m_ccLabelCb(s.paramIndex);
                    if (!lbl.empty()) {
                        const auto& b = s.bounds();
                        const float tw = tm->textWidth(lbl, kFsCCLabel);
                        tm->drawText(r, lbl,
                                      b.x + (b.w - tw) * 0.5f, b.y + b.h - 2,
                                      kFsCCLabel, ccCol);
                    }
                }
            };
            drawLabels(m_knobs);
            if (m_isVisualizer && m_vizKnobGrid) drawLabels(m_vizKnobs);
        }
    }
#endif

    // ─── Event handling ─────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        auto& hb = m_header.bounds();
        LOG_INFO("UI",
                 "DeviceWidget::onMouseDown click=(%g,%g) headerBounds=(%g,%g,%g,%g) contains=%d",
                 e.x, e.y, hb.x, hb.y, hb.w, hb.h,
                 static_cast<int>(rectContains(hb, e.x, e.y)));
        if (rectContains(hb, e.x, e.y))
            return m_header.dispatchMouseDown(e);

        if (!m_expanded) return false;

        auto hitSlot = [&](const ParamSlot& s) {
            const auto& b = s.bounds();
            return e.x >= b.x && e.x < b.x + b.w
                && e.y >= b.y && e.y < b.y + b.h;
        };

        // Right-click → MIDI Learn menu
        if (e.button == MouseButton::Right && m_onParamRightClick) {
            auto tryRightClick = [&](std::vector<ParamSlot>& slots) -> bool {
                for (auto& s : slots)
                    if (hitSlot(s)) {
                        m_onParamRightClick(s.paramIndex, e.x, e.y);
                        return true;
                    }
                return false;
            };
            if (m_isVisualizer && m_vizKnobGrid) {
                if (rectContains(m_vizKnobGrid->bounds(), e.x, e.y) && tryRightClick(m_vizKnobs))
                    return true;
            } else if (!m_customBody && rectContains(m_knobGrid.bounds(), e.x, e.y)) {
                if (tryRightClick(m_knobs)) return true;
            }
        }

        auto dispatchSlot = [&](ParamSlot& s) -> bool {
            auto* v2w = s.w2();
            if (!v2w) return false;
            MouseEvent ev = e;
            ev.lx = e.x - v2w->bounds().x;
            ev.ly = e.y - v2w->bounds().y;
            v2w->dispatchMouseDown(ev);
            m_v2Dragging = v2w;
            captureMouse();
            return true;
        };

        if (m_isVisualizer && m_vizKnobGrid) {
            if (rectContains(m_vizKnobGrid->bounds(), e.x, e.y)) {
                for (auto& s : m_vizKnobs) if (hitSlot(s)) return dispatchSlot(s);
            }
        } else if (m_customBody) {
            // fw2 custom body owns the click area below the header.
            const auto& cb = m_customBody->bounds();
            if (e.x >= cb.x && e.x < cb.x + cb.w &&
                e.y >= cb.y && e.y < cb.y + cb.h) {
                if (m_customBody->dispatchMouseDown(e)) return true;
            }
        } else {
            if (rectContains(m_knobGrid.bounds(), e.x, e.y)) {
                for (auto& s : m_knobs) if (hitSlot(s)) return dispatchSlot(s);
            }
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (rectContains(m_header.bounds(), e.x, e.y))
            return m_header.dispatchMouseUp(e);

        if (!m_expanded) return false;

        if (m_v2Dragging) {
            MouseEvent ev = e;
            ev.lx = e.x - m_v2Dragging->bounds().x;
            ev.ly = e.y - m_v2Dragging->bounds().y;
            m_v2Dragging->dispatchMouseUp(ev);
            m_v2Dragging = nullptr;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_v2Dragging) {
            MouseMoveEvent ev = e;
            ev.lx = e.x - m_v2Dragging->bounds().x;
            ev.ly = e.y - m_v2Dragging->bounds().y;
            m_v2Dragging->dispatchMouseMove(ev);
            return true;
        }
        return false;
    }

    // ─── Value formatting (matches v1) ──────────────────────────────

    static std::string formatValue(float value, const std::string& unit, bool isBoolean) {
        if (isBoolean) return value > 0.5f ? "On" : "Off";
        char buf[32];
        if (unit == "Hz") {
            if (value >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.1fk", value / 1000.0f);
            else                   std::snprintf(buf, sizeof(buf), "%.0f",  value);
        } else if (unit == "dB") std::snprintf(buf, sizeof(buf), "%.1f", value);
        else if (unit == "ms")   std::snprintf(buf, sizeof(buf), "%.0f", value);
        else if (unit == "%")    std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
        else                      std::snprintf(buf, sizeof(buf), "%.2f", value);
        return buf;
    }

    // Expose param slots for the v1 wrapper (iteration only — no ownership)
    const std::vector<ParamSlot>& knobs() const    { return m_knobs; }
    const std::vector<ParamSlot>& vizKnobs() const { return m_vizKnobs; }
    FwGrid& knobGrid()                              { return m_knobGrid; }
    const FwGrid& knobGrid() const                  { return m_knobGrid; }
    FwGrid* vizKnobGrid()                           { return m_vizKnobGrid; }

private:
    DeviceHeaderWidget     m_header;
    FwGrid                 m_knobGrid;
    std::vector<ParamSlot> m_knobs;
    VisualizerWidget*      m_visualizer  = nullptr;
    FwGrid*                m_vizKnobGrid = nullptr;
    std::vector<ParamSlot> m_vizKnobs;

    bool        m_isVisualizer = false;
    bool        m_expanded     = true;
    std::string m_deviceName;

    // v1 cross-framework slots (non-owning)
    Widget*                           m_customPanel = nullptr;
    float                              m_customPanelH = 0;
    float                              m_customMinW   = 0;
    CustomDeviceBody* m_customBody = nullptr;

    // Layout rects for v1 children (filled by onLayout; read by wrapper)
    Rect m_customPanelRect{};
    Rect m_customBodyRect{};

    float m_cachedCellW = 0;
    Widget* m_v2Dragging = nullptr;

    RemoveCallback                          m_onRemove;
    ParamChangeCallback                     m_onParamChange;
    ParamTouchCallback                      m_onParamTouch;
    ParamRightClickCallback                 m_onParamRightClick;
    CCLabelCallback                         m_ccLabelCb;
    DeviceHeaderWidget::ToggleCallback      m_onBypassToggle;
    DeviceHeaderWidget::ToggleCallback      m_onExpandToggle;
    DeviceHeaderWidget::ActionCallback      m_onDragStart;
    DeviceHeaderWidget::PresetClickCallback m_onPresetClick;

    // ─── Internals ──────────────────────────────────────────────────

    static bool rectContains(const Rect& r, float px, float py) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    }

    void updateCellWidth(UIContext& ctx) {
        if (!ctx.textMetrics) return;
        float maxLabelW = 0;
        auto measureSlots = [&](const std::vector<ParamSlot>& slots) {
            for (auto& s : slots) {
                std::string lbl;
                if (s.isKnob())                              lbl = s.knob->label();
                else if (s.type == ParamSlot::TToggle)       lbl = s.toggle->label();
                else if (s.type == ParamSlot::TStepSelector) lbl = s.stepSel->label();
                if (!lbl.empty()) {
                    const float tw = ctx.textMetrics->textWidth(lbl, kFsKnobLabel);
                    if (tw > maxLabelW) maxLabelW = tw;
                }
            }
        };
        measureSlots(m_knobs);
        measureSlots(m_vizKnobs);

        const float minCell = kKnobSize + kKnobSpacing;
        const float needed  = maxLabelW + 8.0f;
        m_cachedCellW = std::max(minCell, needed);

        m_knobGrid.setCellSize(m_cachedCellW, kKnobSize + 22.0f);
        if (m_vizKnobGrid) m_vizKnobGrid->setCellSize(m_cachedCellW, kKnobSize + 22.0f);
    }

    void layoutKnobsInGrid(std::vector<ParamSlot>& slots,
                            const FwGrid& grid, int maxRows, UIContext& ctx) {
        const int n = static_cast<int>(slots.size());
        if (n == 0) return;
        const int cols = static_cast<int>(std::ceil(static_cast<float>(n) / maxRows));

        const auto& gb    = grid.bounds();
        const float pad   = grid.gridPadding();
        const float cellW = grid.cellWidth();
        const float cellH = grid.cellHeight();
        const float gapX  = grid.gapX();
        const float gapY  = grid.gapY();

        for (int i = 0; i < n; ++i) {
            auto* v2w = slots[i].w2();
            if (!v2w) continue;
            const int row = i / cols;
            const int col = i % cols;
            const float cellX = gb.x + pad + col * (cellW + gapX);
            const float cellY = gb.y + pad + row * (cellH + gapY);
            v2w->measure(Constraints::tight(cellW, cellH), ctx);
            v2w->layout({cellX, cellY, cellW, cellH}, ctx);
        }
    }

    void clearKnobs() {
        auto freeSlot = [](ParamSlot& s) {
            delete s.knob;    s.knob    = nullptr;
            delete s.toggle;  s.toggle  = nullptr;
            delete s.stepSel; s.stepSel = nullptr;
        };
        for (auto& s : m_knobs)    freeSlot(s);
        m_knobs.clear();
        for (auto& s : m_vizKnobs) freeSlot(s);
        m_vizKnobs.clear();
        m_v2Dragging = nullptr;
    }

    void buildKnobs(const std::vector<ParamInfo>& params,
                    std::vector<ParamSlot>& slots, FwGrid& grid, int maxRows) {
        grid.setMaxRows(maxRows);

        auto makeKnob = [this](const ParamInfo& p) {
            auto* k = new FwKnob();
            k->setRange(p.minVal, p.maxVal);
            k->setDefaultValue(p.defaultVal);
            k->setValue(p.defaultVal);
            k->setLabel(p.name);
            // Integer-range detection (mirrors GroupedKnobBody): small
            // unit-less integer params (Semitones, Octave, Algorithm)
            // snap to 1.0 and format as plain integers instead of the
            // default %.2f. Covers Arpeggiator / Pitch / similar.
            const float range = p.maxVal - p.minVal;
            const bool isInteger = (range >= 2.0f && range <= 128.0f &&
                                     p.minVal == std::floor(p.minVal) &&
                                     p.maxVal == std::floor(p.maxVal) &&
                                     !p.isBoolean);
            if (p.formatFn) {
                // Param-supplied custom formatter (e.g. log-mapped Hz).
                k->setValueFormatter([fn = p.formatFn](float v) {
                    char buf[24];
                    fn(v, buf, static_cast<int>(sizeof(buf)));
                    return std::string(buf);
                });
            } else if (isInteger) {
                k->setStep(1.0f);
                // ~2 px per integer step — snap stays clean across large
                // ranges (Semitones is -48..+48 → 96 steps). A fixed 48
                // gave 0.5 px/step and dragged in 2-unit jumps.
                k->setPixelsPerFullRange(std::max(48.0f, range * 2.0f));
                k->setValueFormatter([unit = p.unit](float v) {
                    char buf[16];
                    const int iv = static_cast<int>(std::round(v));
                    if (unit.empty())
                        std::snprintf(buf, sizeof(buf), "%d", iv);
                    else
                        std::snprintf(buf, sizeof(buf), "%d %s", iv, unit.c_str());
                    return std::string(buf);
                });
            } else {
                k->setValueFormatter([unit = p.unit, isBool = p.isBoolean](float v) {
                    return formatValue(v, unit, isBool);
                });
            }
            const int idx = p.index;
            k->setOnChange([this, idx](float v) {
                if (m_onParamChange) m_onParamChange(idx, v);
            });
            k->setOnDragEnd([this, idx](float startV, float endV) {
                if (!m_onParamTouch) return;
                m_onParamTouch(idx, startV, /*touching*/true);
                m_onParamTouch(idx, endV,   /*touching*/false);
            });
            return k;
        };

        for (const auto& p : params) {
            ParamSlot slot;
            slot.paramIndex = p.index;
            auto hint = p.widgetHint;
            if (hint == ::yawn::WidgetHint::Knob && p.isBoolean)
                hint = ::yawn::WidgetHint::Toggle;

            switch (hint) {
            case ::yawn::WidgetHint::DentedKnob: {
                auto* dk = makeKnob(p);
                dk->addDetent(p.defaultVal, 0.04f);
                slot.knob = dk;
                slot.type = ParamSlot::TDentedKnob;
                break;
            }
            case ::yawn::WidgetHint::StepSelector: {
                auto* ss = new FwStepSelector();
                ss->setRange(static_cast<int>(p.minVal + 0.5f),
                              static_cast<int>(p.maxVal + 0.5f));
                ss->setValue(static_cast<int>(p.defaultVal + 0.5f));
                ss->setLabel(p.name);
                if (!p.valueLabels.empty()) {
                    auto labels = p.valueLabels;
                    const int mn = static_cast<int>(p.minVal + 0.5f);
                    ss->setFormatter([labels, mn](int v) -> std::string {
                        const int i = v - mn;
                        if (i >= 0 && i < static_cast<int>(labels.size()))
                            return labels[i];
                        return std::to_string(v);
                    });
                }
                ss->setOnChange([this, idx = p.index](int v) {
                    const float fv = static_cast<float>(v);
                    if (m_onParamChange) m_onParamChange(idx, fv);
                    if (m_onParamTouch) {
                        m_onParamTouch(idx, fv, /*touching*/true);
                        m_onParamTouch(idx, fv, /*touching*/false);
                    }
                });
                slot.stepSel = ss;
                slot.type    = ParamSlot::TStepSelector;
                break;
            }
            case ::yawn::WidgetHint::Toggle: {
                auto* ts = new FwToggle();
                ts->setVariant(ToggleVariant::Switch);
                ts->setState(p.defaultVal > 0.5f);
                ts->setLabel(p.name);
                ts->setOnChange([this, idx = p.index](bool state) {
                    const float v = state ? 1.0f : 0.0f;
                    if (m_onParamChange) m_onParamChange(idx, v);
                    if (m_onParamTouch) {
                        m_onParamTouch(idx, v, /*touching*/true);
                        m_onParamTouch(idx, v, /*touching*/false);
                    }
                });
                slot.toggle = ts;
                slot.type   = ParamSlot::TToggle;
                break;
            }
            case ::yawn::WidgetHint::Knob360: {
                auto* k360 = makeKnob(p);
                k360->setWrapMode(true);
                slot.knob = k360;
                slot.type = ParamSlot::TKnob360;
                break;
            }
            default: {
                auto* k = makeKnob(p);
                slot.knob = k;
                slot.type = ParamSlot::TKnob;
                break;
            }
            }
            slots.push_back(slot);
        }
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
