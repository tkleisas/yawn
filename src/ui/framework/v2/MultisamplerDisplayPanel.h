#pragma once
// fw2::MultisamplerDisplayPanel — zone list + per-zone editor for the
// Multisampler instrument. Sits inside DeviceWidget's custom-panel
// slot (set via DeviceWidget::setCustomPanel) and replaces the
// generic param-knob grid.
//
// Layout (within ~360 × 196 px):
//
//   ┌───────────────────────────────────────────────────────────────┐
//   │ [Auto-Sample…]  [+ Drop sample]   [Remove]    Zones: N        │  ← toolbar
//   ├──────────────────────┬────────────────────────────────────────┤
//   │ # Root  Key   Vel    │ ▸ Zone N                               │
//   │ 0 C2    C-B1  1-64   │   Root [C4]    Tune [+0.0]  Vol [▾]    │
//   │ 1 C3    C2-B3 1-64   │   Lo   [C0]    Hi   [B7]    Pan [▾]    │
//   │ 2 C2    ...          │   VLo  [1]     VHi  [127]   Loop[☐]    │
//   │ ...                  │   File: foo_bass_C2.wav                │
//   │                      │                                        │
//   └──────────────────────┴────────────────────────────────────────┘
//
// The panel is a pure VIEW. Edits flow back to the host via callbacks
// (setOnZoneFieldChange, setOnRemoveZone, setOnAutoSampleClicked). The
// host (DetailPanelWidget integration in App.cpp) writes through to
// the Multisampler's Zone struct.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/NumberInput.h"
#include "ui/Theme.h"
#include "util/NoteNames.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class MultisamplerDisplayPanel : public Widget {
public:
    // Mirror of Multisampler::Zone scoped to display fields. Pure view
    // model: the host pushes these in via setZones() each frame; the
    // panel echoes user edits back via the FieldCallback.
    struct ZoneRow {
        int   rootNote = 60;
        int   lowKey   = 0;
        int   highKey  = 127;
        int   lowVel   = 1;
        int   highVel  = 127;
        float tune     = 0.0f;
        float volume   = 1.0f;
        float pan      = 0.0f;
        bool  loop     = false;
        // Display-only — basename of the source file or "<auto-sampled>".
        std::string filename;
        int   sampleFrames = 0;
    };

    using FieldCallback   = std::function<void(int zoneIdx, const ZoneRow&)>;
    using SelectCallback  = std::function<void(int zoneIdx)>;
    using IndexCallback   = std::function<void(int zoneIdx)>;
    using VoidCallback    = std::function<void()>;

    MultisamplerDisplayPanel() {
        setName("MultisamplerDisplay");
        setAutoCaptureOnUnhandledPress(false);

        m_autoSampleBtn.setLabel("Auto-Sample…");
        m_autoSampleBtn.setOnClick([this]() {
            if (m_onAutoSample) m_onAutoSample();
        });

        m_removeZoneBtn.setLabel("Remove");
        m_removeZoneBtn.setOnClick([this]() {
            if (m_selected >= 0 && m_onRemoveZone) m_onRemoveZone(m_selected);
        });

        configureFieldWidgets();
    }

    // ─── Host-driven state ──────────────────────────────────────────
    // Called every frame by the displayUpdater to refresh the view.
    // Skip-if-equal: avoids stomping on a knob the user is dragging.
    void setZones(std::vector<ZoneRow> zones) {
        if (zones != m_zones) {
            m_zones = std::move(zones);
            // Selection guard: keep within bounds.
            if (m_selected >= static_cast<int>(m_zones.size()))
                m_selected = m_zones.empty() ? -1 : 0;
            if (m_selected < 0 && !m_zones.empty()) m_selected = 0;
            syncEditorFromZone(m_selected);
        }
    }

    int  selectedZone() const     { return m_selected; }
    void setSelectedZone(int idx) {
        idx = std::clamp(idx, -1, static_cast<int>(m_zones.size()) - 1);
        if (idx == m_selected) return;
        m_selected = idx;
        syncEditorFromZone(idx);
        if (m_onSelect) m_onSelect(idx);
    }

    // ─── Callbacks (host wires to Multisampler) ─────────────────────
    void setOnZoneFieldChange(FieldCallback cb) { m_onZoneChange = std::move(cb); }
    void setOnSelectionChanged(SelectCallback cb){ m_onSelect     = std::move(cb); }
    void setOnRemoveZone(IndexCallback cb)      { m_onRemoveZone = std::move(cb); }
    void setOnAutoSampleClicked(VoidCallback cb){ m_onAutoSample = std::move(cb); }

    // ─── Layout / render ────────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, std::max(c.minH, 196.0f)});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float x = bounds.x, y = bounds.y, w = bounds.w, h = bounds.h;

        m_toolbarRect = {x, y, w, kToolbarH};

        // Auto-Sample button on the left.
        m_autoSampleBtn.layout({x + 4, y + 2, 90.0f, kToolbarH - 4}, ctx);
        // Remove button — visible only when a zone is selected.
        m_removeZoneBtn.layout({x + 100, y + 2, 60.0f, kToolbarH - 4}, ctx);

        const float listX = x;
        const float listY = y + kToolbarH;
        const float listH = h - kToolbarH;
        m_listRect = {listX, listY, kListW, listH};

        const float edX = x + kListW + 4;
        const float edY = y + kToolbarH + 4;
        const float edW = w - kListW - 8;
        const float edH = h - kToolbarH - 8;
        m_editorRect = {edX, edY, edW, edH};

        // Editor 2-column grid. Two cols of equal width.
        const float colW = (edW - 8) * 0.5f;
        const float col0X = edX;
        const float col1X = edX + colW + 8;
        const float rowH = 22.0f;
        const float fieldW = colW;
        const float labelH = 11.0f;     // tiny label above each field

        // Row 0: Root (col 0), Tune (col 1)
        layoutField(m_rootInput,  col0X, edY + labelH,             fieldW, rowH - labelH, ctx);
        layoutField(m_tuneKnob,   col1X, edY,                       fieldW, rowH * 1.6f,  ctx);

        // Row 1: Lo / Hi keys
        layoutField(m_lowKeyInput, col0X,        edY + (rowH * 1.0f) + labelH,
                    fieldW * 0.48f, rowH - labelH, ctx);
        layoutField(m_highKeyInput, col0X + fieldW * 0.52f,
                    edY + (rowH * 1.0f) + labelH,
                    fieldW * 0.48f, rowH - labelH, ctx);

        // Row 2: Lo / Hi velocities (col 0); Vol knob (col 1, taller)
        layoutField(m_lowVelInput, col0X,        edY + (rowH * 2.0f) + labelH,
                    fieldW * 0.48f, rowH - labelH, ctx);
        layoutField(m_highVelInput, col0X + fieldW * 0.52f,
                    edY + (rowH * 2.0f) + labelH,
                    fieldW * 0.48f, rowH - labelH, ctx);
        layoutField(m_volKnob, col1X, edY + rowH * 1.6f, fieldW, rowH * 1.6f, ctx);

        // Row 3: Pan knob (col 1)
        layoutField(m_panKnob, col1X, edY + rowH * 3.2f, fieldW, rowH * 1.6f, ctx);

        // Row 3 (col 0): Loop toggle
        layoutField(m_loopToggle, col0X, edY + rowH * 3.0f + 2.0f,
                    50.0f, rowH - 4, ctx);
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        const auto& b = m_bounds;
        r.drawRect(b.x, b.y, b.w, b.h, Color{30, 30, 35, 255});

        // ── Toolbar background ──
        r.drawRect(m_toolbarRect.x, m_toolbarRect.y,
                   m_toolbarRect.w, m_toolbarRect.h,
                   Color{38, 38, 44, 255});
        m_autoSampleBtn.render(ctx);
        m_removeZoneBtn.setVisible(m_selected >= 0);
        if (m_removeZoneBtn.isVisible()) m_removeZoneBtn.render(ctx);

        if (tm) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Zones: %d",
                          static_cast<int>(m_zones.size()));
            tm->drawText(r, buf,
                         m_toolbarRect.x + m_toolbarRect.w - 60,
                         m_toolbarRect.y + 4, kFsLabel,
                         Color{180, 180, 180, 255});
        }

        // ── Zone list ──
        renderZoneList(ctx);

        // ── Selected-zone editor ──
        if (m_selected >= 0 && m_selected < static_cast<int>(m_zones.size())) {
            renderEditor(ctx);
        } else if (tm) {
            tm->drawText(r, "No zone selected",
                         m_editorRect.x + 8, m_editorRect.y + 8,
                         kFsLabel, Color{120, 120, 120, 255});
            if (m_zones.empty()) {
                tm->drawText(r,
                             "Drop a sample on the panel,",
                             m_editorRect.x + 8, m_editorRect.y + 28,
                             kFsLabel, Color{140, 140, 140, 255});
                tm->drawText(r,
                             "or click Auto-Sample…",
                             m_editorRect.x + 8, m_editorRect.y + 44,
                             kFsLabel, Color{140, 140, 140, 255});
            }
        }
    }
#endif

    bool onMouseDown(MouseEvent& e) override {
        // Toolbar buttons handle themselves first.
        if (rectContains(m_toolbarRect, e.x, e.y)) {
            if (rectContains(m_autoSampleBtn.bounds(), e.x, e.y))
                return m_autoSampleBtn.dispatchMouseDown(e);
            if (m_removeZoneBtn.isVisible() &&
                rectContains(m_removeZoneBtn.bounds(), e.x, e.y))
                return m_removeZoneBtn.dispatchMouseDown(e);
            return false;
        }

        // Zone list — click selects.
        if (rectContains(m_listRect, e.x, e.y)) {
            const float rowH = kListRowH;
            const int row = static_cast<int>(
                (e.y - m_listRect.y - kListHeaderH) / rowH);
            const int idx = row + m_listScroll;
            if (idx >= 0 && idx < static_cast<int>(m_zones.size())) {
                setSelectedZone(idx);
                return true;
            }
            return false;
        }

        // Editor field widgets — dispatch by hit-test.
        for (Widget* w : editorWidgets()) {
            if (w && w->isVisible() && rectContains(w->bounds(), e.x, e.y)) {
                return w->dispatchMouseDown(e);
            }
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (rectContains(m_listRect, e.x, e.y)) {
            const int dir = (e.dy > 0 ? -1 : (e.dy < 0 ? 1 : 0));
            const int maxScroll = std::max(
                0, static_cast<int>(m_zones.size()) - visibleListRows());
            m_listScroll = std::clamp(m_listScroll + dir, 0, maxScroll);
            return true;
        }
        return false;
    }

private:
    static constexpr float kToolbarH    = 22.0f;
    static constexpr float kListW       = 132.0f;
    static constexpr float kListHeaderH = 12.0f;
    static constexpr float kListRowH    = 14.0f;
    // Pixel sizes (v1 Font baked at 48 px; we approximate the v1
    // pt/26 scale used by other display panels).
    static constexpr float kFsLabel = 7.0f  * (48.0f / 26.0f);
    static constexpr float kFsTiny  = 6.0f  * (48.0f / 26.0f);

    // ── Sub-widget setup ──
    void configureFieldWidgets() {
        // Root note: MIDI 0..127, integer, displayed as "C4" / "F#3".
        m_rootInput.setRange(0, 127);
        m_rootInput.setStep(1);
        m_rootInput.setValueFormatter([](float v) {
            return ::yawn::util::midiNoteName(static_cast<int>(v));
        });
        m_rootInput.setOnChange([this](float v) {
            if (m_selected < 0) return;
            m_zones[m_selected].rootNote = static_cast<int>(v);
            notifyZoneChanged(m_selected);
        });

        m_lowKeyInput.setRange(0, 127);  m_lowKeyInput.setStep(1);
        m_lowKeyInput.setValueFormatter([](float v) {
            return ::yawn::util::midiNoteName(static_cast<int>(v));
        });
        m_lowKeyInput.setOnChange([this](float v) {
            if (m_selected < 0) return;
            int nv = std::clamp(static_cast<int>(v), 0, m_zones[m_selected].highKey);
            m_zones[m_selected].lowKey = nv;
            notifyZoneChanged(m_selected);
        });

        m_highKeyInput.setRange(0, 127); m_highKeyInput.setStep(1);
        m_highKeyInput.setValueFormatter([](float v) {
            return ::yawn::util::midiNoteName(static_cast<int>(v));
        });
        m_highKeyInput.setOnChange([this](float v) {
            if (m_selected < 0) return;
            int nv = std::clamp(static_cast<int>(v), m_zones[m_selected].lowKey, 127);
            m_zones[m_selected].highKey = nv;
            notifyZoneChanged(m_selected);
        });

        m_lowVelInput.setRange(1, 127);  m_lowVelInput.setStep(1);
        m_lowVelInput.setFormat("%.0f");
        m_lowVelInput.setOnChange([this](float v) {
            if (m_selected < 0) return;
            int nv = std::clamp(static_cast<int>(v), 1, m_zones[m_selected].highVel);
            m_zones[m_selected].lowVel = nv;
            notifyZoneChanged(m_selected);
        });

        m_highVelInput.setRange(1, 127); m_highVelInput.setStep(1);
        m_highVelInput.setFormat("%.0f");
        m_highVelInput.setOnChange([this](float v) {
            if (m_selected < 0) return;
            int nv = std::clamp(static_cast<int>(v), m_zones[m_selected].lowVel, 127);
            m_zones[m_selected].highVel = nv;
            notifyZoneChanged(m_selected);
        });

        m_tuneKnob.setRange(-24.0f, 24.0f);
        m_tuneKnob.setDefaultValue(0.0f);
        m_tuneKnob.setLabel("Tune");
        m_tuneKnob.setValueFormatter([](float v) {
            char buf[16]; std::snprintf(buf, sizeof(buf), "%+.2f", v);
            return std::string(buf);
        });
        m_tuneKnob.setOnChange([this](float v) {
            if (m_selected < 0) return;
            m_zones[m_selected].tune = v;
            notifyZoneChanged(m_selected);
        });

        m_volKnob.setRange(0.0f, 2.0f);
        m_volKnob.setDefaultValue(1.0f);
        m_volKnob.setLabel("Vol");
        m_volKnob.setValueFormatter([](float v) {
            if (v < 0.001f) return std::string("-inf");
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%+.1fdB", 20.0f * std::log10(v));
            return std::string(buf);
        });
        m_volKnob.setOnChange([this](float v) {
            if (m_selected < 0) return;
            m_zones[m_selected].volume = v;
            notifyZoneChanged(m_selected);
        });

        m_panKnob.setRange(-1.0f, 1.0f);
        m_panKnob.setDefaultValue(0.0f);
        m_panKnob.setLabel("Pan");
        m_panKnob.setValueFormatter([](float v) {
            if (std::abs(v) < 0.01f) return std::string("C");
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%c%.0f",
                          v < 0 ? 'L' : 'R',
                          std::abs(v) * 100.0f);
            return std::string(buf);
        });
        m_panKnob.setOnChange([this](float v) {
            if (m_selected < 0) return;
            m_zones[m_selected].pan = v;
            notifyZoneChanged(m_selected);
        });

        m_loopToggle.setLabel("Loop");
        m_loopToggle.setOnChange([this](bool on) {
            if (m_selected < 0) return;
            m_zones[m_selected].loop = on;
            notifyZoneChanged(m_selected);
        });
    }

    std::vector<Widget*> editorWidgets() {
        return {&m_rootInput, &m_lowKeyInput, &m_highKeyInput,
                &m_lowVelInput, &m_highVelInput,
                &m_tuneKnob, &m_volKnob, &m_panKnob, &m_loopToggle};
    }

    void layoutField(Widget& w, float x, float y, float wpx, float h, UIContext& ctx) {
        w.layout({x, y, wpx, h}, ctx);
    }

    void syncEditorFromZone(int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_zones.size())) return;
        const auto& z = m_zones[idx];
        // setValue with Programmatic source so we don't fire user
        // callbacks on programmatic updates.
        m_rootInput   .setValue(static_cast<float>(z.rootNote),
                                ValueChangeSource::Programmatic);
        m_lowKeyInput .setValue(static_cast<float>(z.lowKey),
                                ValueChangeSource::Programmatic);
        m_highKeyInput.setValue(static_cast<float>(z.highKey),
                                ValueChangeSource::Programmatic);
        m_lowVelInput .setValue(static_cast<float>(z.lowVel),
                                ValueChangeSource::Programmatic);
        m_highVelInput.setValue(static_cast<float>(z.highVel),
                                ValueChangeSource::Programmatic);
        m_tuneKnob    .setValue(z.tune,    ValueChangeSource::Programmatic);
        m_volKnob     .setValue(z.volume,  ValueChangeSource::Programmatic);
        m_panKnob     .setValue(z.pan,     ValueChangeSource::Programmatic);
        m_loopToggle  .setState(z.loop,    ValueChangeSource::Programmatic);
    }

    void notifyZoneChanged(int idx) {
        if (m_onZoneChange && idx >= 0 && idx < static_cast<int>(m_zones.size()))
            m_onZoneChange(idx, m_zones[idx]);
    }

    int visibleListRows() const {
        const float usable = m_listRect.h - kListHeaderH;
        return std::max(1, static_cast<int>(usable / kListRowH));
    }

    static bool rectContains(const Rect& r, float x, float y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

#ifndef YAWN_TEST_BUILD
    void renderZoneList(UIContext& ctx) {
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;
        const auto& lr = m_listRect;
        r.drawRect(lr.x, lr.y, lr.w, lr.h, Color{25, 25, 28, 255});
        r.drawRect(lr.x, lr.y, lr.w, kListHeaderH, Color{40, 40, 46, 255});
        if (tm) {
            tm->drawText(r, "#  Root  Key      Vel",
                         lr.x + 4, lr.y + 1, kFsTiny,
                         Color{170, 170, 170, 255});
        }

        const int rowsVisible = visibleListRows();
        const int firstIdx = std::clamp(m_listScroll, 0,
            std::max(0, static_cast<int>(m_zones.size()) - 1));
        const int lastIdx = std::min(static_cast<int>(m_zones.size()),
                                     firstIdx + rowsVisible);

        for (int i = firstIdx; i < lastIdx; ++i) {
            const float ry = lr.y + kListHeaderH + (i - firstIdx) * kListRowH;
            const bool sel = (i == m_selected);
            r.drawRect(lr.x, ry, lr.w, kListRowH - 1,
                       sel ? Color{55, 70, 95, 255}
                            : (i % 2 ? Color{32, 32, 36, 255}
                                      : Color{28, 28, 32, 255}));
            if (!tm) continue;
            const auto& z = m_zones[i];
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%-2d %-4s %s-%s  %d-%d",
                i,
                ::yawn::util::midiNoteName(z.rootNote).c_str(),
                ::yawn::util::midiNoteName(z.lowKey).c_str(),
                ::yawn::util::midiNoteName(z.highKey).c_str(),
                z.lowVel, z.highVel);
            tm->drawText(r, buf, lr.x + 4, ry + 1, kFsTiny,
                         sel ? Color{240, 240, 240, 255}
                             : Color{200, 200, 200, 255});
        }

        // Scroll indicator dots
        if (static_cast<int>(m_zones.size()) > rowsVisible) {
            const float trackX = lr.x + lr.w - 4;
            r.drawRect(trackX, lr.y + kListHeaderH, 2, lr.h - kListHeaderH,
                       Color{20, 20, 24, 255});
            const float thumbH = std::max(8.0f,
                (lr.h - kListHeaderH) *
                static_cast<float>(rowsVisible) / m_zones.size());
            const float thumbY = lr.y + kListHeaderH +
                ((lr.h - kListHeaderH - thumbH) *
                 static_cast<float>(m_listScroll) /
                 std::max(1, static_cast<int>(m_zones.size()) - rowsVisible));
            r.drawRect(trackX, thumbY, 2, thumbH, Color{120, 120, 130, 255});
        }
    }

    void renderEditor(UIContext& ctx) {
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;
        const auto& er = m_editorRect;
        r.drawRect(er.x, er.y, er.w, er.h, Color{32, 32, 36, 255});

        // Header: "Zone N — file.wav"
        const auto& z = m_zones[m_selected];
        if (tm) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Zone %d", m_selected);
            tm->drawText(r, buf, er.x + 4, er.y + 2, kFsLabel,
                         Color{220, 220, 220, 255});
            if (!z.filename.empty()) {
                tm->drawText(r, z.filename, er.x + 60, er.y + 2, kFsTiny,
                             Color{160, 160, 160, 255});
            }
        }

        // Field labels above each input/knob
        if (tm) {
            tm->drawText(r, "Root", m_rootInput.bounds().x,
                         m_rootInput.bounds().y - 9, kFsTiny,
                         Color{160, 160, 160, 255});
            tm->drawText(r, "Lo",   m_lowKeyInput.bounds().x,
                         m_lowKeyInput.bounds().y - 9, kFsTiny,
                         Color{160, 160, 160, 255});
            tm->drawText(r, "Hi",   m_highKeyInput.bounds().x,
                         m_highKeyInput.bounds().y - 9, kFsTiny,
                         Color{160, 160, 160, 255});
            tm->drawText(r, "VLo",  m_lowVelInput.bounds().x,
                         m_lowVelInput.bounds().y - 9, kFsTiny,
                         Color{160, 160, 160, 255});
            tm->drawText(r, "VHi",  m_highVelInput.bounds().x,
                         m_highVelInput.bounds().y - 9, kFsTiny,
                         Color{160, 160, 160, 255});
        }

        // Render every editor widget. They paint themselves.
        for (Widget* w : editorWidgets()) {
            if (w) w->render(ctx);
        }
    }
#endif

    // ── State ──
    std::vector<ZoneRow> m_zones;
    int m_selected   = -1;
    int m_listScroll = 0;

    Rect m_toolbarRect{};
    Rect m_listRect{};
    Rect m_editorRect{};

    FwButton       m_autoSampleBtn;
    FwButton       m_removeZoneBtn;
    FwNumberInput  m_rootInput;
    FwNumberInput  m_lowKeyInput;
    FwNumberInput  m_highKeyInput;
    FwNumberInput  m_lowVelInput;
    FwNumberInput  m_highVelInput;
    FwKnob         m_tuneKnob;
    FwKnob         m_volKnob;
    FwKnob         m_panKnob;
    FwToggle       m_loopToggle;

    FieldCallback  m_onZoneChange;
    SelectCallback m_onSelect;
    IndexCallback  m_onRemoveZone;
    VoidCallback   m_onAutoSample;
};

// ZoneRow equality so setZones can short-circuit when the host pushes
// the same data each frame.
inline bool operator==(const MultisamplerDisplayPanel::ZoneRow& a,
                       const MultisamplerDisplayPanel::ZoneRow& b) {
    return a.rootNote == b.rootNote && a.lowKey == b.lowKey &&
           a.highKey  == b.highKey  && a.lowVel == b.lowVel &&
           a.highVel  == b.highVel  && a.tune   == b.tune   &&
           a.volume   == b.volume   && a.pan    == b.pan    &&
           a.loop     == b.loop     && a.filename == b.filename &&
           a.sampleFrames == b.sampleFrames;
}
inline bool operator!=(const MultisamplerDisplayPanel::ZoneRow& a,
                       const MultisamplerDisplayPanel::ZoneRow& b) {
    return !(a == b);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
