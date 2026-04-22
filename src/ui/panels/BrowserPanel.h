#pragma once
// BrowserPanel — Tabbed panel (top-right) for Files, Presets, Clip, MIDI.
//
// The "Files" tab provides an in-app file browser for audio samples.
// The "Presets" tab browses device presets with search and metadata.
// The "Clip" tab shows follow action controls for the currently selected
// clip slot, giving them dedicated screen real-estate.
// The "MIDI" tab shows a real-time MIDI monitor for debugging.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/InstrumentDisplayWidget.h"  // AutomationEnvelopeWidget
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/V1EventBridge.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/FollowAction.h"
#include "automation/AutomationLane.h"
#include "midi/MidiMonitorBuffer.h"
#include "BrowserFilesTab.h"
#include "BrowserPresetsTab.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

namespace yawn {
namespace ui {
namespace fw {

class BrowserPanel : public Widget {
public:
    enum class Tab { Files = 0, Presets, Clip, Midi, COUNT };

    BrowserPanel() {
        initFollowActionWidgets();
        initClipEnvelopeWidgets();
    }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ── Follow action API ──
    // Set which visual clip's automation lanes the "Clip" tab edits.
    // lanes may be null (no active visual slot — editor grays out).
    // lengthBeats is the clip's loop length in beats, used for the
    // envelope time axis. shaderParamNames is the list of @range
    // uniforms declared by the clip's shader — they show up in the
    // dropdown alongside A..H so the user can automate any of them.
    void setVisualClipAutomation(std::vector<automation::AutomationLane>* lanes,
                                   int trackIndex,
                                   double lengthBeats,
                                   std::vector<std::string> shaderParamNames = {}) {
        m_clipAutoLanes       = lanes;
        m_clipAutoTrackIdx    = trackIndex;
        m_clipAutoLength      = lengthBeats > 0.25 ? lengthBeats : 4.0;
        m_clipShaderParamNames = std::move(shaderParamNames);
        rebuildClipTargetList();
        syncClipEnvelope();
    }

    void setFollowAction(FollowAction* fa) {
        m_followActionPtr = fa;
        if (fa) {
            m_faEnableBtn.setLabel(fa->enabled ? "On" : "Off");
            m_faBarCountKnob.setValue(static_cast<float>(fa->barCount));
            m_faActionADropdown.setSelectedIndex(static_cast<int>(fa->actionA));
            m_faActionBDropdown.setSelectedIndex(static_cast<int>(fa->actionB));
            m_faCrossfader.setValue(static_cast<float>(fa->chanceA));
        }
    }

    FollowAction* followActionPtr() const { return m_followActionPtr; }

    // ── Files & Presets tabs ──
    BrowserFilesTab& filesTab() { return m_filesTab; }
    BrowserPresetsTab& presetsTab() { return m_presetsTab; }
    void setLibraryDatabase(library::LibraryDatabase* db) {
        m_filesTab.setDatabase(db);
        m_presetsTab.setDatabase(db);
    }
    void setLibraryScanner(library::LibraryScanner* sc) {
        m_filesTab.setScanner(sc);
    }

    // ── MIDI monitor ──
    void setMidiMonitor(midi::MidiMonitorBuffer* buf) { m_midiMonitor = buf; }

    // Port name lookup callback (set by App to resolve port index → name)
    using PortNameFn = std::function<std::string(int)>;
    void setPortNameFn(PortNameFn fn) { m_portNameFn = std::move(fn); }

    // ── Events ──
    bool onMouseDown(MouseEvent& e) override {
        float mx = e.x, my = e.y;
        float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w;

        // Tab bar click
        if (my >= y && my < y + kTabH) {
            float tx = x + 2.0f;
            for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i) {
                float tw = m_tabWidths[i];
                if (mx >= tx && mx < tx + tw) {
                    m_activeTab = static_cast<Tab>(i);
                    return true;
                }
                tx += tw;
            }
            return true;
        }

        // MIDI tab: toolbar buttons + filter checkboxes
        if (m_activeTab == Tab::Midi && m_midiMonitor) {
            float toolY = y + kTabH + 2.0f;
            float toolH = 18.0f;
            float btnW = 42.0f;
            float bx = x + w - btnW - 4.0f;
            // Auto-scroll button
            if (mx >= bx && mx < bx + btnW && my >= toolY && my < toolY + toolH) {
                m_midiAutoScroll = !m_midiAutoScroll;
                return true;
            }
            bx -= btnW + 4.0f;
            // Clear button
            if (mx >= bx && mx < bx + btnW && my >= toolY && my < toolY + toolH) {
                m_midiMonitor->clear();
                m_midiScrollOffset = 0;
                m_midiAutoScroll = true;
                return true;
            }
            // Filter checkboxes row
            float filterY = toolY + toolH + 2.0f;
            float filterH = 14.0f;
            float cbW = 52.0f;
            if (my >= filterY && my < filterY + filterH) {
                float fx = x + 4;
                if (mx >= fx && mx < fx + cbW) { m_midiShowClock = !m_midiShowClock; return true; }
                fx += cbW + 4;
                if (mx >= fx && mx < fx + cbW) { m_midiShowSysEx = !m_midiShowSysEx; return true; }
                fx += cbW + 4;
                cbW = 62.0f;
                if (mx >= fx && mx < fx + cbW) { m_midiShowRealtime = !m_midiShowRealtime; return true; }
            }
        }

        // Files tab
        if (m_activeTab == Tab::Files) {
            float bodyY = y + kTabH;
            float bodyH = m_bounds.h - kTabH;
            return m_filesTab.onMouseDown(e, x, bodyY, w, bodyH, m_lastCtx);
        }

        // Presets tab
        if (m_activeTab == Tab::Presets) {
            float bodyY = y + kTabH;
            float bodyH = m_bounds.h - kTabH;
            return m_presetsTab.onMouseDown(e, x, bodyY, w, bodyH, m_lastCtx);
        }

        // Clip tab: follow action widgets.
        // v2 dropdowns handle their open-state via LayerStack; when a
        // popup is open the panel doesn't see the click at all.
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            if (hitWidget(m_faEnableBtn, mx, my))
                return m_faEnableBtn.onMouseDown(e);
            // v2 knob — drag needs the gesture SM. Forward via v1→v2
            // event bridge and capture v1 mouse so subsequent moves
            // route back here for forwarding.
            {
                const auto& b = m_faBarCountKnob.bounds();
                if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
                    m_faBarCountKnob.dispatchMouseDown(ev);
                    m_v2Dragging = &m_faBarCountKnob;
                    captureMouse();
                    return true;
                }
            }
            {
                const auto& b = m_faActionADropdown.bounds();
                if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    if (e.button == MouseButton::Left) m_faActionADropdown.toggle();
                    return true;
                }
            }
            {
                const auto& b = m_faActionBDropdown.bounds();
                if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    if (e.button == MouseButton::Left) m_faActionBDropdown.toggle();
                    return true;
                }
            }
            if (hitWidget(m_faCrossfader, mx, my))
                return m_faCrossfader.onMouseDown(e);
        }
        // Clip envelope editor (visual clips only — presence of
        // m_clipAutoLanes gates it).
        if (m_activeTab == Tab::Clip && m_clipAutoLanes) {
            {
                const auto& b = m_clipTargetDropdown.bounds();
                if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    if (e.button == MouseButton::Left) m_clipTargetDropdown.toggle();
                    return true;
                }
            }
            if (hitWidget(m_clipEnvelope, mx, my))
                return m_clipEnvelope.onMouseDown(e);
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_activeTab == Tab::Files)
            return m_filesTab.onMouseMove(e);
        if (m_activeTab == Tab::Presets)
            return m_presetsTab.onMouseMove(e);
        // v2 knob drag in progress — forward translated events.
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2MouseMove(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseMove(ev);
            return true;
        }
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            // v2 dropdowns handle open-state mouseMove via LayerStack.
            MouseMoveEvent me = e;
            if (m_faCrossfader.onMouseMove(me)) return true;
        }
        // Clip envelope drag (point move): the envelope widget
        // captures on mouse-down, so we route moves to it while it
        // reports a drag in progress.
        if (m_activeTab == Tab::Clip && m_clipAutoLanes) {
            MouseMoveEvent me = e;
            if (m_clipEnvelope.dragIndex() >= 0) {
                m_clipEnvelope.onMouseMove(me);
                return true;
            }
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_activeTab == Tab::Files)
            return m_filesTab.onMouseUp(e);
        if (m_activeTab == Tab::Presets)
            return m_presetsTab.onMouseUp(e);
        // v2 knob drag wrapping up — flush the release + drop v1 capture.
        if (m_v2Dragging) {
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, m_v2Dragging->bounds());
            m_v2Dragging->dispatchMouseUp(ev);
            m_v2Dragging = nullptr;
            releaseMouse();
            return true;
        }
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            // v2 dropdowns toggle on mouseDown directly — no mouseUp
            // forwarding needed for them.
            if (hitWidget(m_faEnableBtn, e.x, e.y))
                return m_faEnableBtn.onMouseUp(e);
            if (m_faCrossfader.onMouseUp(e)) return true;
        }
        if (m_activeTab == Tab::Clip && m_clipAutoLanes) {
            if (m_clipEnvelope.onMouseUp(e)) return true;
        }
        return false;
    }

    // v2 dropdowns route keyboard + scroll events through LayerStack
    // when open, so the App doesn't need a panel-side "hasOpenDropdown"
    // query to gate shortcut routing. Kept for API compatibility but
    // always returns false now.
    bool hasOpenDropdown() const { return false; }

    bool onKeyDown(KeyEvent& /*e*/) override {
        // v2 dropdowns consume keys via LayerStack before the panel.
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (m_activeTab == Tab::Files)
            return m_filesTab.onScroll(e);
        if (m_activeTab == Tab::Presets)
            return m_presetsTab.onScroll(e);
        // v2 dropdowns route wheel events to their popup automatically
        // when open (LayerStack dispatch). No per-panel glue needed.
        if (m_activeTab == Tab::Midi && m_midiMonitor) {
            m_midiScrollOffset += static_cast<int>(e.dy * 3);
            if (m_midiScrollOffset < 0) m_midiScrollOffset = 0;
            m_midiAutoScroll = false; // user scrolled manually
            return true;
        }
        return false;
    }

    // Text editing support — v1 search inputs + v2 FwKnob inline
    // edit mode (double-click opens a text buffer; Enter commits,
    // Escape cancels, digits/±/. feed the buffer).
    bool hasEditingWidget() const {
        return m_faBarCountKnob.isEditing() ||
               m_filesTab.isSearchEditing() ||
               m_presetsTab.isSearchEditing();
    }
    // Keep old name for backward compat with App.cpp
    bool hasEditingKnob() const { return hasEditingWidget(); }
    bool forwardKeyDown(int key) {
        // v2 knob grabs Enter / Escape / Backspace while editing.
        // Digits arrive via forwardTextInput (SDL TEXT_INPUT), so we
        // don't translate them here. The knob ignores other keys.
        if (m_faBarCountKnob.isEditing()) {
            ::yawn::ui::fw2::KeyEvent ke;
            switch (key) {
                case 27 /*SDLK_ESCAPE*/:    ke.key = ::yawn::ui::fw2::Key::Escape;    break;
                case 13 /*SDLK_RETURN*/:    ke.key = ::yawn::ui::fw2::Key::Enter;     break;
                case 8  /*SDLK_BACKSPACE*/: ke.key = ::yawn::ui::fw2::Key::Backspace; break;
                default:                    return false;
            }
            return m_faBarCountKnob.dispatchKeyDown(ke);
        }
        if (m_filesTab.isSearchEditing()) return m_filesTab.forwardKeyDown(key);
        if (m_presetsTab.isSearchEditing()) return m_presetsTab.forwardKeyDown(key);
        return false;
    }
    bool forwardTextInput(const char* text) {
        if (m_faBarCountKnob.isEditing()) {
            m_faBarCountKnob.takeTextInput(text ? text : "");
            return true;
        }
        if (m_filesTab.isSearchEditing()) return m_filesTab.forwardTextInput(text);
        if (m_presetsTab.isSearchEditing()) return m_presetsTab.forwardTextInput(text);
        return false;
    }
    void cancelEditingKnobs() {
        if (m_faBarCountKnob.isEditing())
            m_faBarCountKnob.endEdit(/*commit*/false);
        m_filesTab.cancelEditing();
        m_presetsTab.cancelEditing();
    }

    void paint(UIContext& ctx) override {
        m_lastCtx = ctx;
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        r.drawRect(x, y, w, h, Color{32, 32, 36});

        // ── Tab bar ──
        r.drawRect(x, y, w, kTabH, Color{38, 38, 42});
        r.drawRect(x, y + kTabH - 1, w, 1, Theme::clipSlotBorder);

        if (f.isLoaded()) {
            float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.85f;
            static const char* tabNames[] = {"Files", "Presets", "Clip", "MIDI"};
            // Compute tab widths from text
            for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i)
                m_tabWidths[i] = f.textWidth(tabNames[i], scale) + kTabPad;
            float tx = x + 2.0f;
            for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i) {
                float tw = m_tabWidths[i];
                bool active = (static_cast<Tab>(i) == m_activeTab);
                if (active)
                    r.drawRect(tx, y + 2, tw, kTabH - 3, Color{50, 50, 56});
                f.drawText(r, tabNames[i], tx + kTabPad * 0.5f, y + 5, scale,
                           active ? Theme::textPrimary : Theme::textDim);
                tx += tw;
            }
        }

        // ── Tab content ──
        float bodyY = y + kTabH;
        float bodyH = h - kTabH;
        r.pushClip(x, bodyY, w, bodyH);

        switch (m_activeTab) {
        case Tab::Files:
            m_filesTab.paint(r, f, x, bodyY, w, bodyH, ctx);
            break;
        case Tab::Presets:
            m_presetsTab.paint(r, f, x, bodyY, w, bodyH, ctx);
            break;
        case Tab::Clip:
            paintClipTab(r, f, x, bodyY, w, bodyH, ctx);
            break;
        case Tab::Midi:
            paintMidiTab(r, f, x, bodyY, w, bodyH);
            break;
        default:
            break;
        }

        r.popClip();

        // Dropdown overlays (outside clip region)
        // v2 dropdown popups paint via LayerStack::paintLayers in App;
        // no per-panel paintOverlay call needed.
    }

private:
    static constexpr float kTabH = 24.0f;
    static constexpr float kTabPad = 16.0f; // horizontal padding per tab

    Tab m_activeTab = Tab::Files;
    UIContext m_lastCtx{};
    float m_tabWidths[4] = {55, 55, 55, 55}; // computed in paint

    // Files and Presets tabs
    BrowserFilesTab   m_filesTab;
    BrowserPresetsTab m_presetsTab;

    // MIDI monitor state
    midi::MidiMonitorBuffer* m_midiMonitor = nullptr;
    PortNameFn m_portNameFn;
    int  m_midiScrollOffset = 0;
    bool m_midiAutoScroll = true;
    bool m_midiShowClock = false;
    bool m_midiShowSysEx = true;
    bool m_midiShowRealtime = true;  // Start/Stop/Continue

    // Follow action state
    FollowAction* m_followActionPtr = nullptr;
    FwButton   m_faEnableBtn;
    ::yawn::ui::fw2::FwKnob   m_faBarCountKnob;
    // Tracks which v2 widget currently owns a drag — set in
    // onMouseDown, used by onMouseMove/Up to forward translated
    // events. Null when no v2 drag is in progress.
    ::yawn::ui::fw2::Widget*  m_v2Dragging = nullptr;

    // Clip-envelope editor state (visual clips only for now).
    std::vector<automation::AutomationLane>* m_clipAutoLanes = nullptr;
    int     m_clipAutoTrackIdx = -1;
    double  m_clipAutoLength   = 4.0;
    // Unified target entry: either one of the 8 A..H knobs OR a shader
    // @range uniform. The dropdown lists all of these.
    struct ClipTargetEntry {
        bool        isShaderParam = false;  // false = A..H knob
        int         knobIndex     = -1;     // 0..7 when isShaderParam=false
        std::string paramName;              // non-empty when isShaderParam=true
    };
    std::vector<ClipTargetEntry> m_clipTargets;
    std::vector<std::string>     m_clipShaderParamNames;
    int      m_clipSelectedTarget = 0;
    ::yawn::ui::fw2::FwDropDown m_clipTargetDropdown;
    AutomationEnvelopeWidget m_clipEnvelope;
    ::yawn::ui::fw2::FwDropDown m_faActionADropdown;
    ::yawn::ui::fw2::FwDropDown m_faActionBDropdown;
    FwCrossfader m_faCrossfader;

    static bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
    }

    // Build the dropdown list: A..H followed by every shader @range
    // uniform the current clip exposes. Called whenever the set of
    // available params changes (clip selection, shader reload).
    void rebuildClipTargetList() {
        m_clipTargets.clear();
        std::vector<std::string> labels;
        labels.reserve(8 + m_clipShaderParamNames.size());
        for (int i = 0; i < 8; ++i) {
            ClipTargetEntry e;
            e.isShaderParam = false;
            e.knobIndex     = i;
            m_clipTargets.push_back(e);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "Knob %c",
                           static_cast<char>('A' + i));
            labels.emplace_back(buf);
        }
        for (const auto& name : m_clipShaderParamNames) {
            ClipTargetEntry e;
            e.isShaderParam = true;
            e.paramName     = name;
            m_clipTargets.push_back(e);
            labels.push_back(name);
        }
        m_clipTargetDropdown.setItems(labels);
        if (m_clipSelectedTarget < 0 ||
            m_clipSelectedTarget >= static_cast<int>(m_clipTargets.size())) {
            m_clipSelectedTarget = 0;
        }
        m_clipTargetDropdown.setSelectedIndex(m_clipSelectedTarget);
    }

    // Find-or-create the lane for the currently-selected dropdown entry.
    automation::AutomationLane* ensureSelectedLane() {
        if (!m_clipAutoLanes) return nullptr;
        if (m_clipSelectedTarget < 0 ||
            m_clipSelectedTarget >= static_cast<int>(m_clipTargets.size()))
            return nullptr;
        const auto& entry = m_clipTargets[m_clipSelectedTarget];
        for (auto& lane : *m_clipAutoLanes) {
            if (entry.isShaderParam) {
                if (lane.target.type == automation::TargetType::VisualParam &&
                    lane.target.paramName == entry.paramName) {
                    return &lane;
                }
            } else {
                if (lane.target.type == automation::TargetType::VisualKnob &&
                    lane.target.paramIndex == entry.knobIndex) {
                    return &lane;
                }
            }
        }
        automation::AutomationLane fresh;
        fresh.target = entry.isShaderParam
            ? automation::AutomationTarget::visualParam(
                  m_clipAutoTrackIdx, entry.paramName)
            : automation::AutomationTarget::visualKnob(
                  m_clipAutoTrackIdx, entry.knobIndex);
        m_clipAutoLanes->push_back(std::move(fresh));
        return &m_clipAutoLanes->back();
    }

    // Read the envelope points for the current selection and push
    // them into the widget so users see what's saved.
    void syncClipEnvelope() {
        std::vector<std::pair<double, float>> pts;
        if (m_clipAutoLanes && m_clipSelectedTarget >= 0 &&
            m_clipSelectedTarget < static_cast<int>(m_clipTargets.size())) {
            const auto& entry = m_clipTargets[m_clipSelectedTarget];
            for (auto& lane : *m_clipAutoLanes) {
                bool match = false;
                if (entry.isShaderParam) {
                    match = lane.target.type == automation::TargetType::VisualParam &&
                            lane.target.paramName == entry.paramName;
                } else {
                    match = lane.target.type == automation::TargetType::VisualKnob &&
                            lane.target.paramIndex == entry.knobIndex;
                }
                if (!match) continue;
                for (auto& p : lane.envelope.points()) {
                    pts.push_back({p.time, p.value});
                }
                break;
            }
        }
        m_clipEnvelope.setPoints(pts);
        m_clipEnvelope.setTimeRange(0.0, m_clipAutoLength);
        m_clipEnvelope.setValueRange(0.0f, 1.0f);
    }

    void initClipEnvelopeWidgets() {
        // Keep the default 8-item popup cap so the overlay doesn't
        // run past the bottom of the browser panel. Wheel-scroll
        // inside the popup reveals the rest of the entries.
        rebuildClipTargetList();
        m_clipTargetDropdown.setOnChange([this](int idx, const std::string&) {
            m_clipSelectedTarget = idx;
            syncClipEnvelope();
        });

        m_clipEnvelope.setOnPointAdd([this](double time, float value) {
            auto* lane = ensureSelectedLane();
            if (!lane) return;
            lane->envelope.addPoint(time, value);
            syncClipEnvelope();
        });
        m_clipEnvelope.setOnPointMove([this](int idx, double time, float value) {
            auto* lane = ensureSelectedLane();
            if (!lane) return;
            lane->envelope.movePoint(idx, time, value);
            syncClipEnvelope();
        });
        m_clipEnvelope.setOnPointRemove([this](int idx) {
            auto* lane = ensureSelectedLane();
            if (!lane) return;
            lane->envelope.removePoint(idx);
            syncClipEnvelope();
        });
    }

    void initFollowActionWidgets() {
        m_faEnableBtn.setLabel("Off");
        m_faEnableBtn.setOnClick([this]() {
            if (m_followActionPtr) {
                m_followActionPtr->enabled = !m_followActionPtr->enabled;
                m_faEnableBtn.setLabel(m_followActionPtr->enabled ? "On" : "Off");
            }
        });

        // v2 FwKnob — integer 1..64 bars. No label, value shown as int.
        m_faBarCountKnob.setRange(1.0f, 64.0f);
        m_faBarCountKnob.setDefaultValue(1.0f);
        m_faBarCountKnob.setValue(1.0f);
        m_faBarCountKnob.setStep(1.0f);
        m_faBarCountKnob.setShowLabel(false);
        m_faBarCountKnob.setValueFormatter([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
            return buf;
        });
        m_faBarCountKnob.setOnChange([this](float v) {
            if (m_followActionPtr)
                m_followActionPtr->barCount = static_cast<int>(v);
        });

        static const std::vector<std::string> kNames = {
            "None", "Stop", "Play Again", "Next", "Previous",
            "First", "Last", "Random", "Any"
        };
        m_faActionADropdown.setItems(kNames);
        m_faActionADropdown.setSelectedIndex(0);
        m_faActionADropdown.setOnChange([this](int idx, const std::string&) {
            if (m_followActionPtr)
                m_followActionPtr->actionA = static_cast<FollowActionType>(idx);
        });

        m_faActionBDropdown.setItems(kNames);
        m_faActionBDropdown.setSelectedIndex(0);
        m_faActionBDropdown.setOnChange([this](int idx, const std::string&) {
            if (m_followActionPtr)
                m_followActionPtr->actionB = static_cast<FollowActionType>(idx);
        });

        m_faCrossfader.setLabels("A", "B");
        m_faCrossfader.setDefault(100.0f);
        m_faCrossfader.setValue(100.0f);
        m_faCrossfader.setOnChange([this](float v) {
            if (m_followActionPtr)
                m_followActionPtr->chanceA = static_cast<int>(v);
        });
    }

    void paintClipTab(Renderer2D& r, Font& f, float x, float y,
                      float w, float /*h*/, UIContext& ctx) {
        float pad = 10.0f;
        float sx = x + pad;
        float sw = w - pad * 2;
        float labelScale = 10.0f / Theme::kFontSize;

        // Section header
        r.drawRect(sx, y + 4, sw, 1.0f, Color{50, 50, 55, 255});
        float headerY = y + 8.0f;
        f.drawText(r, "Follow Actions", sx, headerY, labelScale, Theme::textPrimary);

        if (!m_followActionPtr) {
            f.drawText(r, "Select a clip slot", sx, headerY + 20.0f,
                       labelScale, Theme::textDim);
            return;
        }

        // Sync values from data
        m_faEnableBtn.setLabel(m_followActionPtr->enabled ? "On" : "Off");
        // v2 knob: sync from backing data. While the user is dragging
        // or inline-editing the knob, their input fires onChange → the
        // backing barCount is already updated, so setValue is a clean
        // no-op on equal values. (Edit-buffer display is decoupled
        // from m_value, so syncing during edit is harmless.)
        m_faBarCountKnob.setValue(static_cast<float>(m_followActionPtr->barCount));
        m_faActionADropdown.setSelectedIndex(static_cast<int>(m_followActionPtr->actionA));
        m_faActionBDropdown.setSelectedIndex(static_cast<int>(m_followActionPtr->actionB));
        if (!m_faCrossfader.isDragging())
            m_faCrossfader.setValue(static_cast<float>(m_followActionPtr->chanceA));

        // Layout widgets — vertical stack for narrow panel
        float rowY = headerY + 18.0f;
        float inputH = 20.0f;
        float rowGap = 4.0f;
        float labelCol = 60.0f;
        float inputX = sx + labelCol;

        // Enable
        f.drawText(r, "Enable", sx, rowY + 3.0f, labelScale, Theme::textDim);
        m_faEnableBtn.layout(Rect{inputX, rowY, 40.0f, inputH}, ctx);
        m_faEnableBtn.paint(ctx);
        rowY += inputH + 12.0f;

        // Bars — v2 knob, render through fw2 UIContext.
        f.drawText(r, "Bars", sx, rowY + 16.0f, labelScale, Theme::textDim);
        {
            auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
            m_faBarCountKnob.layout(Rect{inputX, rowY, 44.0f, 44.0f}, v2ctx);
            m_faBarCountKnob.render(v2ctx);
        }
        rowY += 50.0f;

        // Action A / Action B — labels then dropdowns side by side
        float gap = 8.0f;
        float halfW = (sw - gap) * 0.5f;
        float rightX = sx + halfW + gap;

        f.drawText(r, "Action A", sx, rowY, labelScale, Theme::textSecondary);
        f.drawText(r, "Action B", rightX, rowY, labelScale, Theme::textSecondary);
        rowY += 20.0f;
        {
            auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
            m_faActionADropdown.layout(Rect{sx, rowY, halfW, inputH}, v2ctx);
            m_faActionADropdown.render(v2ctx);
            m_faActionBDropdown.layout(Rect{rightX, rowY, halfW, inputH}, v2ctx);
            m_faActionBDropdown.render(v2ctx);
        }
        rowY += inputH + 10.0f;

        // Crossfader (A/B chance)
        m_faCrossfader.layout(Rect{sx, rowY, sw, 28.0f}, ctx);
        m_faCrossfader.paint(ctx);
        rowY += 28.0f + 14.0f;

        // ── Clip envelope editor ──────────────────────────────────
        // Only meaningful when the slot has a visual clip (lanes
        // pointer is set for visual slots only). For audio/MIDI we
        // leave the section hidden to avoid UX confusion.
        if (!m_clipAutoLanes) return;

        r.drawRect(sx, rowY, sw, 1.0f, Color{50, 50, 55, 255});
        f.drawText(r, "Clip Envelope", sx, rowY + 8.0f, labelScale,
                    Theme::textPrimary);
        // Larger gap between the section header and the button row
        // so the label text isn't cropped by the button outlines.
        rowY += 28.0f;

        // Target dropdown — A..H plus shader @range uniforms.
        // v2 widget; popup paints via LayerStack.
        {
            auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
            m_clipTargetDropdown.layout(Rect{sx, rowY, sw, 20.0f}, v2ctx);
            m_clipTargetDropdown.render(v2ctx);
        }
        rowY += 24.0f;

        // Envelope drawing area.
        m_clipEnvelope.setTimeRange(0.0, m_clipAutoLength);
        m_clipEnvelope.setValueRange(0.0f, 1.0f);
        m_clipEnvelope.layout(Rect{sx, rowY, sw, 80.0f}, ctx);
        m_clipEnvelope.paint(ctx);
    }

    void paintMidiTab(Renderer2D& r, Font& f, float x, float y,
                      float w, float bodyH) {
        if (!f.isLoaded() || !m_midiMonitor) {
            return;
        }

        float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.78f;
        float textH = f.pixelHeight() * scale;
        float rowH = textH + 4.0f;

        // ── Toolbar: [Clear] [Auto▼] ──
        float toolY = y + 2.0f;
        float toolH = textH + 6.0f;
        float btnW = 46.0f;

        // Auto-scroll button (right-most) — centered text
        float bx = x + w - btnW - 4.0f;
        Color autoCol = m_midiAutoScroll ? Color{60, 130, 80} : Color{50, 50, 56};
        r.drawRect(bx, toolY, btnW, toolH, autoCol);
        {
            float tw = f.textWidth("Auto", scale);
            f.drawText(r, "Auto", bx + (btnW - tw) * 0.5f, toolY + (toolH - textH) * 0.5f, scale, Theme::textPrimary);
        }

        // Clear button — centered text
        bx -= btnW + 4.0f;
        r.drawRect(bx, toolY, btnW, toolH, Color{50, 50, 56});
        {
            float tw = f.textWidth("Clear", scale);
            f.drawText(r, "Clear", bx + (btnW - tw) * 0.5f, toolY + (toolH - textH) * 0.5f, scale, Theme::textPrimary);
        }

        // Status: message count
        {
            char countBuf[32];
            std::snprintf(countBuf, sizeof(countBuf), "%zu msgs",
                          m_midiMonitor->count());
            f.drawText(r, countBuf, x + 4, toolY + (toolH - textH) * 0.5f, scale, Theme::textDim);
        }

        // ── Filter checkboxes row ──
        float filterY = toolY + toolH + 2.0f;
        float filterH = textH + 2.0f;
        float cbSize = 10.0f;
        float cbTextOff = cbSize + 3.0f;
        float cbW = 52.0f;
        float fScale = scale * 0.9f;

        auto drawCheckbox = [&](float cbx, float fy, float, const char* label, bool checked) {
            float cy = fy + (filterH - cbSize) * 0.5f;
            r.drawRect(cbx, cy, cbSize, cbSize, Color{50, 50, 56});
            if (checked) {
                r.drawRect(cbx + 2, cy + 2, cbSize - 4, cbSize - 4, Color{100, 180, 120});
            }
            f.drawText(r, label, cbx + cbTextOff, fy + (filterH - textH * 0.9f) * 0.5f, fScale,
                       checked ? Theme::textPrimary : Theme::textDim);
        };

        float fx = x + 4;
        drawCheckbox(fx, filterY, cbW, "Clock", m_midiShowClock);
        fx += cbW + 4;
        drawCheckbox(fx, filterY, cbW, "SysEx", m_midiShowSysEx);
        fx += cbW + 4;
        drawCheckbox(fx, filterY, 62.0f, "RT Msgs", m_midiShowRealtime);

        float listY = filterY + filterH + 2.0f;
        float listH = bodyH - (listY - y);
        if (listH <= 0) return;

        // ── Column header ──
        float headerH = textH + 2.0f;
        r.drawRect(x, listY, w, headerH, Color{38, 38, 42});
        float hScale = scale * 0.9f;
        float cx = x + 4;
        float hTextY = listY + (headerH - textH * 0.9f) * 0.5f;
        f.drawText(r, "Time",  cx, hTextY, hScale, Theme::textDim);  cx += 76;
        f.drawText(r, "Port",  cx, hTextY, hScale, Theme::textDim);  cx += 36;
        f.drawText(r, "Ch",    cx, hTextY, hScale, Theme::textDim);  cx += 24;
        f.drawText(r, "Type",  cx, hTextY, hScale, Theme::textDim);  cx += 52;
        f.drawText(r, "Data",  cx, hTextY, hScale, Theme::textDim);

        listY += headerH;
        listH -= headerH;
        if (listH <= 0) return;

        r.pushClip(x, listY, w, listH);

        // Build filtered index mapping for visible entries
        size_t oldest = m_midiMonitor->oldestValid();
        size_t head   = m_midiMonitor->headIndex();

        // Count visible (filtered) entries
        using ET = midi::MidiMonitorEntry::Type;
        auto isVisible = [&](const midi::MidiMonitorEntry& e) -> bool {
            switch (e.type) {
            case ET::Clock:    return m_midiShowClock;
            case ET::SysEx:    return m_midiShowSysEx;
            case ET::Start:
            case ET::Stop:
            case ET::Continue: return m_midiShowRealtime;
            default:           return true;
            }
        };

        size_t filteredTotal = 0;
        for (size_t i = oldest; i < head; ++i) {
            if (isVisible(m_midiMonitor->at(i))) ++filteredTotal;
        }

        int visibleRows = static_cast<int>(listH / rowH);
        if (visibleRows < 1) visibleRows = 1;

        int maxScroll = static_cast<int>(filteredTotal) - visibleRows;
        if (maxScroll < 0) maxScroll = 0;

        if (m_midiAutoScroll) {
            m_midiScrollOffset = maxScroll;
        } else {
            if (m_midiScrollOffset > maxScroll)
                m_midiScrollOffset = maxScroll;
        }

        // Render only visible rows with filtering
        int skipCount = m_midiScrollOffset;
        int rowsDrawn = 0;
        for (size_t i = oldest; i < head && rowsDrawn < visibleRows; ++i) {
            const auto& e = m_midiMonitor->at(i);
            if (!isVisible(e)) continue;
            if (skipCount > 0) { --skipCount; continue; }

            float ry = listY + rowsDrawn * rowH;

            // Alternating row bg
            if (rowsDrawn & 1)
                r.drawRect(x, ry, w, rowH, Color{36, 36, 40});

            // Type-based color coding
            Color typeCol = Theme::textPrimary;
            switch (e.type) {
            case ET::NoteOn:          typeCol = Color{100, 200, 120}; break;
            case ET::NoteOff:         typeCol = Color{80, 150, 100};  break;
            case ET::CC:              typeCol = Color{120, 160, 220}; break;
            case ET::PitchBend:       typeCol = Color{200, 180, 100}; break;
            case ET::ProgramChange:   typeCol = Color{180, 130, 200}; break;
            case ET::ChannelPressure:
            case ET::PolyPressure:    typeCol = Color{200, 150, 130}; break;
            case ET::Clock:
            case ET::Start:
            case ET::Stop:
            case ET::Continue:        typeCol = Color{140, 140, 160}; break;
            case ET::SysEx:           typeCol = Color{200, 100, 100}; break;
            default:                  typeCol = Theme::textDim;       break;
            }

            char buf[64];
            cx = x + 4;
            float textY = ry + (rowH - textH) * 0.5f;

            // Time (mm:ss.ms)
            uint32_t ms = e.timestamp;
            uint32_t sec = ms / 1000;
            uint32_t min = sec / 60;
            std::snprintf(buf, sizeof(buf), "%02u:%02u.%03u", min, sec % 60, ms % 1000);
            f.drawText(r, buf, cx, textY, scale, Theme::textDim);
            cx += 76;

            // Port
            std::snprintf(buf, sizeof(buf), "%d", e.portIndex + 1);
            f.drawText(r, buf, cx, textY, scale, Color{140, 140, 150});
            cx += 36;

            // Channel (1-based)
            std::snprintf(buf, sizeof(buf), "%2d", e.channel + 1);
            f.drawText(r, buf, cx, textY, scale, Theme::textPrimary);
            cx += 24;

            // Type + Data
            const char* typeName = "";
            char dataBuf[48] = "";

            switch (e.type) {
            case ET::NoteOn:
                typeName = "NoteOn";
                std::snprintf(dataBuf, sizeof(dataBuf), "%s%d  vel %d",
                              noteNameStr(e.data1), noteOctave(e.data1), e.data2);
                break;
            case ET::NoteOff:
                typeName = "NoteOf";
                std::snprintf(dataBuf, sizeof(dataBuf), "%s%d",
                              noteNameStr(e.data1), noteOctave(e.data1));
                break;
            case ET::CC:
                typeName = "CC";
                std::snprintf(dataBuf, sizeof(dataBuf), "#%-3d  val %d",
                              e.data1, e.data2);
                break;
            case ET::ProgramChange:
                typeName = "PrgCh";
                std::snprintf(dataBuf, sizeof(dataBuf), "%d", e.data1);
                break;
            case ET::PitchBend:
                typeName = "PBend";
                std::snprintf(dataBuf, sizeof(dataBuf), "%d",
                              static_cast<int>(e.pitchBend) - 8192);
                break;
            case ET::ChannelPressure:
                typeName = "ChanP";
                std::snprintf(dataBuf, sizeof(dataBuf), "%d", e.data1);
                break;
            case ET::PolyPressure:
                typeName = "PolyP";
                std::snprintf(dataBuf, sizeof(dataBuf), "%s%d  %d",
                              noteNameStr(e.data1), noteOctave(e.data1), e.data2);
                break;
            case ET::Clock:   typeName = "Clock"; break;
            case ET::Start:   typeName = "Start"; break;
            case ET::Stop:    typeName = "Stop";  break;
            case ET::Continue: typeName = "Cont"; break;
            case ET::SysEx:
                typeName = "SysEx";
                if (e.sysexLen > 0) {
                    char* p = dataBuf;
                    int n = (e.sysexLen < 8) ? e.sysexLen : 8;
                    for (int i = 0; i < n; ++i) {
                        p += std::snprintf(p, 4, "%02X ", e.sysexHead[i]);
                    }
                }
                break;
            default: typeName = "???"; break;
            }

            f.drawText(r, typeName, cx, textY, scale, typeCol);
            cx += 52;
            if (dataBuf[0])
                f.drawText(r, dataBuf, cx, textY, scale, Theme::textPrimary);

            ++rowsDrawn;
        }

        r.popClip();

        // Scrollbar indicator
        if (filteredTotal > static_cast<size_t>(visibleRows)) {
            float sbX = x + w - 4;
            float sbH = listH;
            float thumbFrac = static_cast<float>(visibleRows) / static_cast<float>(filteredTotal);
            float thumbH = std::max(8.0f, sbH * thumbFrac);
            float scrollFrac = (maxScroll > 0) ?
                static_cast<float>(m_midiScrollOffset) / static_cast<float>(maxScroll) : 0.0f;
            float thumbY = listY + scrollFrac * (sbH - thumbH);
            r.drawRect(sbX, thumbY, 3, thumbH, Color{80, 80, 90});
        }
    }

    static const char* noteNameStr(uint8_t note) {
        static const char* names[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        return names[note % 12];
    }

    static int noteOctave(uint8_t note) {
        return static_cast<int>(note / 12) - 1;
    }

};

} // namespace fw
} // namespace ui
} // namespace yawn
