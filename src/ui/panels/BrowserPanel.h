#pragma once
// BrowserPanel — Tabbed panel (top-right) for Files, Presets, Clip, MIDI, Config.
//
// The "Clip" tab shows follow action controls for the currently selected
// clip slot, giving them dedicated screen real-estate.
// The "MIDI" tab shows a real-time MIDI monitor for debugging.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "audio/FollowAction.h"
#include "midi/MidiMonitorBuffer.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

namespace yawn {
namespace ui {
namespace fw {

class BrowserPanel : public Widget {
public:
    enum class Tab { Files = 0, Presets, Clip, Midi, Config, COUNT };

    BrowserPanel() { initFollowActionWidgets(); }

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ── Follow action API ──
    void setFollowAction(FollowAction* fa) {
        m_followActionPtr = fa;
        if (fa) {
            m_faEnableBtn.setLabel(fa->enabled ? "On" : "Off");
            m_faBarCountKnob.setValue(static_cast<float>(fa->barCount));
            m_faActionADropdown.setSelected(static_cast<int>(fa->actionA));
            m_faActionBDropdown.setSelected(static_cast<int>(fa->actionB));
            m_faChanceAKnob.setValue(static_cast<float>(fa->chanceA));
        }
    }

    FollowAction* followActionPtr() const { return m_followActionPtr; }

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
                if (mx >= tx && mx < tx + kTabW) {
                    m_activeTab = static_cast<Tab>(i);
                    return true;
                }
                tx += kTabW;
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

        // Clip tab: follow action widgets
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            if (m_faActionADropdown.isOpen())
                return m_faActionADropdown.onMouseDown(e);
            if (m_faActionBDropdown.isOpen())
                return m_faActionBDropdown.onMouseDown(e);
            if (hitWidget(m_faEnableBtn, mx, my))
                return m_faEnableBtn.onMouseDown(e);
            if (hitWidget(m_faBarCountKnob, mx, my))
                return m_faBarCountKnob.onMouseDown(e);
            if (hitWidget(m_faActionADropdown, mx, my))
                return m_faActionADropdown.onMouseDown(e);
            if (hitWidget(m_faActionBDropdown, mx, my))
                return m_faActionBDropdown.onMouseDown(e);
            if (hitWidget(m_faChanceAKnob, mx, my))
                return m_faChanceAKnob.onMouseDown(e);
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            MouseMoveEvent me = e;
            if (m_faActionADropdown.isOpen()) {
                m_faActionADropdown.onMouseMove(me);
                return true;
            }
            if (m_faActionBDropdown.isOpen()) {
                m_faActionBDropdown.onMouseMove(me);
                return true;
            }
            if (m_faBarCountKnob.onMouseMove(me)) return true;
            if (m_faChanceAKnob.onMouseMove(me)) return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_activeTab == Tab::Clip && m_followActionPtr) {
            if (m_faActionADropdown.isOpen())
                return m_faActionADropdown.onMouseUp(e);
            if (m_faActionBDropdown.isOpen())
                return m_faActionBDropdown.onMouseUp(e);
            if (hitWidget(m_faEnableBtn, e.x, e.y))
                return m_faEnableBtn.onMouseUp(e);
            if (m_faBarCountKnob.onMouseUp(e)) return true;
            if (hitWidget(m_faActionADropdown, e.x, e.y))
                return m_faActionADropdown.onMouseUp(e);
            if (hitWidget(m_faActionBDropdown, e.x, e.y))
                return m_faActionBDropdown.onMouseUp(e);
            if (m_faChanceAKnob.onMouseUp(e)) return true;
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (m_activeTab == Tab::Midi && m_midiMonitor) {
            m_midiScrollOffset += static_cast<int>(e.dy * 3);
            if (m_midiScrollOffset < 0) m_midiScrollOffset = 0;
            m_midiAutoScroll = false; // user scrolled manually
            return true;
        }
        return false;
    }

    // Knob text editing support
    bool hasEditingKnob() const {
        return m_faBarCountKnob.isEditing() || m_faChanceAKnob.isEditing();
    }
    bool forwardKeyDown(int key) {
        KeyEvent ke;
        ke.keyCode = key;
        if (m_faBarCountKnob.isEditing()) return m_faBarCountKnob.onKeyDown(ke);
        if (m_faChanceAKnob.isEditing()) return m_faChanceAKnob.onKeyDown(ke);
        return false;
    }
    bool forwardTextInput(const char* text) {
        TextInputEvent te;
        std::strncpy(te.text, text, sizeof(te.text) - 1);
        te.text[sizeof(te.text) - 1] = '\0';
        if (m_faBarCountKnob.isEditing()) return m_faBarCountKnob.onTextInput(te);
        if (m_faChanceAKnob.isEditing()) return m_faChanceAKnob.onTextInput(te);
        return false;
    }
    void cancelEditingKnobs() {
        KeyEvent ke;
        ke.keyCode = 27; // Escape
        if (m_faBarCountKnob.isEditing()) m_faBarCountKnob.onKeyDown(ke);
        if (m_faChanceAKnob.isEditing()) m_faChanceAKnob.onKeyDown(ke);
    }

    void paint(UIContext& ctx) override {
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
            static const char* tabNames[] = {"Files", "Presets", "Clip", "MIDI", "Config"};
            float tx = x + 2.0f;
            for (int i = 0; i < static_cast<int>(Tab::COUNT); ++i) {
                bool active = (static_cast<Tab>(i) == m_activeTab);
                if (active)
                    r.drawRect(tx, y + 2, kTabW, kTabH - 3, Color{50, 50, 56});
                f.drawText(r, tabNames[i], tx + 8, y + 5, scale,
                           active ? Theme::textPrimary : Theme::textDim);
                tx += kTabW;
            }
        }

        // ── Tab content ──
        float bodyY = y + kTabH;
        float bodyH = h - kTabH;
        r.pushClip(x, bodyY, w, bodyH);

        switch (m_activeTab) {
        case Tab::Clip:
            paintClipTab(r, f, x, bodyY, w, bodyH, ctx);
            break;
        case Tab::Midi:
            paintMidiTab(r, f, x, bodyY, w, bodyH);
            break;
        default:
            paintPlaceholder(r, f, x, bodyY, w, bodyH);
            break;
        }

        r.popClip();

        // Dropdown overlays (outside clip region)
        if (m_activeTab == Tab::Clip) {
            if (m_faActionADropdown.isOpen())
                m_faActionADropdown.paintOverlay(ctx);
            if (m_faActionBDropdown.isOpen())
                m_faActionBDropdown.paintOverlay(ctx);
        }
    }

private:
    static constexpr float kTabH = 24.0f;
    static constexpr float kTabW = 55.0f;

    Tab m_activeTab = Tab::Files;

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
    FwKnob     m_faBarCountKnob;
    FwDropDown m_faActionADropdown;
    FwDropDown m_faActionBDropdown;
    FwKnob     m_faChanceAKnob;

    static bool hitWidget(Widget& w, float mx, float my) {
        auto& b = w.bounds();
        return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
    }

    void initFollowActionWidgets() {
        m_faEnableBtn.setLabel("Off");
        m_faEnableBtn.setOnClick([this]() {
            if (m_followActionPtr) {
                m_followActionPtr->enabled = !m_followActionPtr->enabled;
                m_faEnableBtn.setLabel(m_followActionPtr->enabled ? "On" : "Off");
            }
        });

        m_faBarCountKnob.setRange(1.0f, 64.0f);
        m_faBarCountKnob.setDefault(1.0f);
        m_faBarCountKnob.setValue(1.0f);
        m_faBarCountKnob.setStep(1.0f);
        m_faBarCountKnob.setSensitivity(0.3f);
        m_faBarCountKnob.setLabel("");
        m_faBarCountKnob.setFormatCallback([](float v) -> std::string {
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
        m_faActionADropdown.setSelected(0);
        m_faActionADropdown.setOnChange([this](int idx) {
            if (m_followActionPtr)
                m_followActionPtr->actionA = static_cast<FollowActionType>(idx);
        });

        m_faActionBDropdown.setItems(kNames);
        m_faActionBDropdown.setSelected(0);
        m_faActionBDropdown.setOnChange([this](int idx) {
            if (m_followActionPtr)
                m_followActionPtr->actionB = static_cast<FollowActionType>(idx);
        });

        m_faChanceAKnob.setRange(0.0f, 100.0f);
        m_faChanceAKnob.setDefault(100.0f);
        m_faChanceAKnob.setValue(100.0f);
        m_faChanceAKnob.setStep(1.0f);
        m_faChanceAKnob.setSensitivity(0.3f);
        m_faChanceAKnob.setLabel("");
        m_faChanceAKnob.setFormatCallback([](float v) -> std::string {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(v));
            return buf;
        });
        m_faChanceAKnob.setOnChange([this](float v) {
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
        if (!m_faBarCountKnob.isEditing())
            m_faBarCountKnob.setValue(static_cast<float>(m_followActionPtr->barCount));
        m_faActionADropdown.setSelected(static_cast<int>(m_followActionPtr->actionA));
        m_faActionBDropdown.setSelected(static_cast<int>(m_followActionPtr->actionB));
        if (!m_faChanceAKnob.isEditing())
            m_faChanceAKnob.setValue(static_cast<float>(m_followActionPtr->chanceA));

        // Layout widgets vertically (panel is narrow)
        float rowY = headerY + 18.0f;
        float labelH = 14.0f;
        float inputH = 20.0f;
        float knobW = 48.0f, knobH = 50.0f;
        float dropW = sw - 60.0f;
        if (dropW < 80.0f) dropW = 80.0f;
        float rowGap = 6.0f;

        // Enable
        f.drawText(r, "Enable", sx, rowY, labelScale, Theme::textDim);
        float inputX = sx + 60.0f;
        m_faEnableBtn.layout(Rect{inputX, rowY - 1, 50.0f, inputH}, ctx);
        m_faEnableBtn.paint(ctx);
        rowY += labelH + rowGap;

        // Bar count
        f.drawText(r, "Bars", sx, rowY + 12.0f, labelScale, Theme::textDim);
        m_faBarCountKnob.layout(Rect{inputX, rowY, knobW, knobH}, ctx);
        m_faBarCountKnob.paint(ctx);
        rowY += knobH + rowGap;

        // Action A
        f.drawText(r, "Action A", sx, rowY + 2.0f, labelScale, Theme::textDim);
        m_faActionADropdown.layout(Rect{inputX, rowY, dropW, inputH}, ctx);
        m_faActionADropdown.paint(ctx);
        rowY += inputH + rowGap;

        // Action B
        f.drawText(r, "Action B", sx, rowY + 2.0f, labelScale, Theme::textDim);
        m_faActionBDropdown.layout(Rect{inputX, rowY, dropW, inputH}, ctx);
        m_faActionBDropdown.paint(ctx);
        rowY += inputH + rowGap;

        // Chance A
        f.drawText(r, "Chance A", sx, rowY + 12.0f, labelScale, Theme::textDim);
        m_faChanceAKnob.layout(Rect{inputX, rowY, knobW, knobH}, ctx);
        m_faChanceAKnob.paint(ctx);
    }

    void paintMidiTab(Renderer2D& r, Font& f, float x, float y,
                      float w, float bodyH) {
        if (!f.isLoaded() || !m_midiMonitor) {
            return;
        }

        float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.78f;
        float rowH = 14.0f;

        // ── Toolbar: [Clear] [Auto▼] ──
        float toolY = y + 2.0f;
        float toolH = 18.0f;
        float btnW = 42.0f;

        // Auto-scroll button (right-most) — centered text
        float bx = x + w - btnW - 4.0f;
        Color autoCol = m_midiAutoScroll ? Color{60, 130, 80} : Color{50, 50, 56};
        r.drawRect(bx, toolY, btnW, toolH, autoCol);
        {
            float tw = f.textWidth("Auto", scale);
            f.drawText(r, "Auto", bx + (btnW - tw) * 0.5f, toolY + (toolH - rowH) * 0.5f + 1, scale, Theme::textPrimary);
        }

        // Clear button — centered text
        bx -= btnW + 4.0f;
        r.drawRect(bx, toolY, btnW, toolH, Color{50, 50, 56});
        {
            float tw = f.textWidth("Clear", scale);
            f.drawText(r, "Clear", bx + (btnW - tw) * 0.5f, toolY + (toolH - rowH) * 0.5f + 1, scale, Theme::textPrimary);
        }

        // Status: message count
        {
            char countBuf[32];
            std::snprintf(countBuf, sizeof(countBuf), "%zu msgs",
                          m_midiMonitor->count());
            f.drawText(r, countBuf, x + 4, toolY + (toolH - rowH) * 0.5f + 1, scale, Theme::textDim);
        }

        // ── Filter checkboxes row ──
        float filterY = toolY + toolH + 2.0f;
        float filterH = 14.0f;
        float cbSize = 10.0f;
        float cbTextOff = cbSize + 3.0f;
        float cbW = 52.0f;
        float fScale = scale * 0.9f;

        auto drawCheckbox = [&](float fx, float fy, float cw, const char* label, bool checked) {
            float cy = fy + (filterH - cbSize) * 0.5f;
            r.drawRect(fx, cy, cbSize, cbSize, Color{50, 50, 56});
            if (checked) {
                r.drawRect(fx + 2, cy + 2, cbSize - 4, cbSize - 4, Color{100, 180, 120});
            }
            f.drawText(r, label, fx + cbTextOff, fy + 1, fScale,
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
        float headerH = 13.0f;
        r.drawRect(x, listY, w, headerH, Color{38, 38, 42});
        float hScale = scale * 0.9f;
        float cx = x + 4;
        f.drawText(r, "Time",  cx, listY + 1, hScale, Theme::textDim);  cx += 64;
        f.drawText(r, "Port",  cx, listY + 1, hScale, Theme::textDim);  cx += 32;
        f.drawText(r, "Ch",    cx, listY + 1, hScale, Theme::textDim);  cx += 24;
        f.drawText(r, "Type",  cx, listY + 1, hScale, Theme::textDim);  cx += 48;
        f.drawText(r, "Data",  cx, listY + 1, hScale, Theme::textDim);

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

            // Time (mm:ss.ms) — wider column
            uint32_t ms = e.timestamp;
            uint32_t sec = ms / 1000;
            uint32_t min = sec / 60;
            std::snprintf(buf, sizeof(buf), "%02u:%02u.%03u", min, sec % 60, ms % 1000);
            f.drawText(r, buf, cx, ry + 1, scale, Theme::textDim);
            cx += 64;

            // Port — wider column
            std::snprintf(buf, sizeof(buf), "%d", e.portIndex + 1);
            f.drawText(r, buf, cx, ry + 1, scale, Theme::textDim);
            cx += 32;

            // Channel (1-based) — wider column
            std::snprintf(buf, sizeof(buf), "%2d", e.channel + 1);
            f.drawText(r, buf, cx, ry + 1, scale, Theme::textPrimary);
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

            f.drawText(r, typeName, cx, ry + 1, scale, typeCol);
            cx += 48;
            if (dataBuf[0])
                f.drawText(r, dataBuf, cx, ry + 1, scale, Theme::textPrimary);

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

    void paintPlaceholder(Renderer2D& r, Font& f, float x, float y,
                          float w, float h) {
        if (f.isLoaded() && h > 40 && w > 80) {
            float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.7f;
            const char* label = "BROWSER";
            if (m_activeTab == Tab::Presets) label = "PRESETS";
            else if (m_activeTab == Tab::Config) label = "CONFIG";
            float tw = f.textWidth(label, scale);
            f.drawText(r, label, x + (w - tw) * 0.5f, y + h * 0.5f - 8,
                       scale, Theme::textDim);
        }
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
