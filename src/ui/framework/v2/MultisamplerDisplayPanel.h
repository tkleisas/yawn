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
#include "ui/framework/v2/MultisamplerZoneRow.h"
#include "ui/framework/v2/MultisamplerZoneMapWidget.h"
#include "ui/framework/v2/DragManager.h"
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
    //
    // Defined in MultisamplerZoneRow.h — shared with the
    // MultisamplerZoneMapWidget so both can talk the same view-model
    // without a circular include between this header and the map.
    using ZoneRow = MultisamplerZoneRow;

    using FieldCallback   = std::function<void(int zoneIdx, const ZoneRow&)>;
    using SelectCallback  = std::function<void(int zoneIdx)>;
    using IndexCallback   = std::function<void(int zoneIdx)>;
    using VoidCallback    = std::function<void()>;
    using WidthCallback   = std::function<void(float newWidth)>;

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

        m_mapToggleBtn.setLabel("Map ▶");
        m_mapToggleBtn.setOnClick([this]() {
            m_mapExpanded = !m_mapExpanded;
            m_mapToggleBtn.setLabel(m_mapExpanded ? "Map ◀" : "Map ▶");
            // Tell the host the panel wants more (or less) horizontal
            // room. The wired GroupedKnobBody flips its m_displayWidth
            // and the device strip re-flows on the next measure pass.
            if (m_onPreferredWidthChanged) {
                m_onPreferredWidthChanged(m_mapExpanded ? kExpandedW : kCollapsedW);
            }
            // invalidate() bubbles up only until the first relayout
            // boundary (the DeviceWidget). The device strip's
            // SnapScrollContainer never sees the change without a
            // wholesale measure-cache flush, so it keeps drawing at
            // the old width until a window resize forces a re-layout.
            // bumpEpoch() invalidates every widget's measure cache —
            // same mechanism the framework uses for DPI / theme
            // changes — and is cheap enough for a one-off click.
            UIContext::global().bumpEpoch();
            invalidate();
        });

        configureFieldWidgets();

        // 2D zone map — selection + drag-edit feeds into the same
        // path the per-field editors use, so the host wires nothing
        // extra. Map → panel → existing notifyZoneChanged → host's
        // setOnZoneFieldChange.
        m_zoneMap.setOnSelectionChanged([this](int idx) {
            // Mirror the map's selection into the panel's selection so
            // the editor on the right and the list highlight follow.
            // Skip-if-equal — setSelectedZone re-emits to the host.
            if (idx == m_selected) return;
            setSelectedZone(idx);
        });
        m_zoneMap.setOnZoneFieldChange([this](int idx, const ZoneRow& row) {
            if (idx < 0 || idx >= static_cast<int>(m_zones.size())) return;
            m_zones[idx] = row;
            // Keep the per-field editors in sync with what the user
            // is dragging in the map (otherwise the "Lo C2" knob
            // displays a stale value while the map shows the new one).
            if (idx == m_selected) syncEditorFromZone(idx);
            notifyZoneChanged(idx);
        });
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
            // Forward to the 2D map (its own skip-if-equal handles
            // the case where the host's per-frame push is identical).
            m_zoneMap.setZones(m_zones);
            m_zoneMap.setSelectedZone(m_selected);
            scrollListToSelection();   // no-op until first layout
        }
    }

    int  selectedZone() const     { return m_selected; }
    void setSelectedZone(int idx) {
        idx = std::clamp(idx, -1, static_cast<int>(m_zones.size()) - 1);
        if (idx == m_selected) return;
        m_selected = idx;
        syncEditorFromZone(idx);
        m_zoneMap.setSelectedZone(idx);
        scrollListToSelection();   // keep the list row visible — user
                                   // may have selected via the 2D map
                                   // and the row could be off-screen
        if (m_onSelect) m_onSelect(idx);
    }

    // ─── Callbacks (host wires to Multisampler) ─────────────────────
    void setOnZoneFieldChange(FieldCallback cb) { m_onZoneChange = std::move(cb); }
    void setOnSelectionChanged(SelectCallback cb){ m_onSelect     = std::move(cb); }
    void setOnRemoveZone(IndexCallback cb)      { m_onRemoveZone = std::move(cb); }
    void setOnAutoSampleClicked(VoidCallback cb){ m_onAutoSample = std::move(cb); }
    // Fires when the Map toggle is clicked; argument is the panel's
    // new preferred width. Host wires this to GroupedKnobBody's
    // setDisplayWidth so the device strip re-flows.
    void setOnPreferredWidthChanged(WidthCallback cb) {
        m_onPreferredWidthChanged = std::move(cb);
    }
    float currentPreferredWidth() const {
        return m_mapExpanded ? kExpandedW : kCollapsedW;
    }

    // ─── Layout / render ────────────────────────────────────────────
    // Width depends on the map toggle: collapsed = original 360 px
    // (existing list+editor only), expanded = 360 + map area. Height
    // unchanged from the original ~196 px panel — map shares the
    // existing vertical band with list+editor when expanded.
    Size onMeasure(Constraints c, UIContext&) override {
        const float preferredW = currentPreferredWidth();
        const float preferredH = kToolbarH + kListEditorH;
        return c.constrain({preferredW, std::max(c.minH, preferredH)});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float x = bounds.x, y = bounds.y, w = bounds.w, h = bounds.h;

        m_toolbarRect = {x, y, w, kToolbarH};

        // Auto-Sample button on the left.
        m_autoSampleBtn.layout({x + 4, y + 2, 90.0f, kToolbarH - 4}, ctx);
        // Remove button — visible only when a zone is selected.
        m_removeZoneBtn.layout({x + 100, y + 2, 60.0f, kToolbarH - 4}, ctx);
        // Map toggle button — far right of the toolbar.
        const float toggleW = 56.0f;
        m_mapToggleBtn.layout({x + w - toggleW - 4, y + 2,
                               toggleW, kToolbarH - 4}, ctx);

        const float bandY = y + kToolbarH;
        const float bandH = std::max(40.0f, h - kToolbarH);

        // List on the far left.
        m_listRect = {x, bandY, kListW, bandH};

        // Editor pane sits to the right of the list. When the map is
        // expanded, it shrinks to leave room for the map column.
        const float edX = x + kListW + 4;
        const float edY = bandY + 4;
        const float edAvailW = w - kListW - 8
                             - (m_mapExpanded ? (kMapW + kMapGap) : 0.0f);
        const float edW = std::max(80.0f, edAvailW);
        const float edH = bandH - 8;
        m_editorRect = {edX, edY, edW, edH};

        // Map column on the right (only when expanded).
        if (m_mapExpanded) {
            const float mapX = edX + edW + kMapGap;
            const float mapY = bandY + 4;
            const float mapW = std::max(40.0f, x + w - mapX - 4);
            const float mapH = bandH - 8;
            m_zoneMap.setVisible(true);
            m_zoneMap.layout({mapX, mapY, mapW, mapH}, ctx);
        } else {
            m_zoneMap.setVisible(false);
            m_zoneMap.layout({x, y + h, 0, 0}, ctx);
        }

        // First layout (or post-resize) — m_listRect.h is now real,
        // so any deferred scroll-to-selection from setZones / ctor
        // takes effect now. Idempotent on subsequent calls.
        scrollListToSelection();

        // Editor header band ("Zone N — file.wav") owns the top
        // strip; the field grid starts below it so labels never
        // overlap the header text.
        const float headerH = 24.0f;
        const float gridY = edY + headerH;

        // Editor 2-column grid. Number-input column (left) is
        // narrower because note names + velocities are 2-3 chars max;
        // knob column (right) gets the bulk of the width so Tune /
        // Vol / Pan render at a usable size instead of as 38-px
        // postage stamps.
        const float kKnobColPx = 64.0f;   // wide enough for the knob arc + label
        const float numColW    = std::max(78.0f, edW - kKnobColPx - 8);
        const float knobColW   = std::max(56.0f, edW - numColW - 8);
        const float col0X = edX;
        const float col1X = edX + numColW + 8;
        // rowH = label band + field band. labelH must be >= line
        // height of kFsTiny (15 × ~1.2 = 18) so the label glyph never
        // dips into the field's background — was 13 with a 12-px
        // font, became a clip when the font went up to 15.
        const float rowH = 30.0f;
        const float labelH = 18.0f;

        // Knob column starts 8 px above the grid top — gives the
        // bottom-most knob (Pan) clearance from the editor's lower
        // edge without sacrificing the label-band spacing on the
        // numeric-input column.
        const float knobColY = gridY - 8.0f;

        // Row 0: Root (col 0), Tune (col 1, vertical knob)
        layoutField(m_rootInput, col0X, gridY + labelH, numColW, rowH - labelH, ctx);
        layoutField(m_tuneKnob,  col1X, knobColY,        knobColW, rowH * 1.8f, ctx);

        // Row 1: Lo / Hi keys (col 0)
        layoutField(m_lowKeyInput, col0X,        gridY + (rowH * 1.0f) + labelH,
                    numColW * 0.48f, rowH - labelH, ctx);
        layoutField(m_highKeyInput, col0X + numColW * 0.52f,
                    gridY + (rowH * 1.0f) + labelH,
                    numColW * 0.48f, rowH - labelH, ctx);

        // Row 2: Lo / Hi velocities (col 0); Vol knob (col 1)
        layoutField(m_lowVelInput, col0X,        gridY + (rowH * 2.0f) + labelH,
                    numColW * 0.48f, rowH - labelH, ctx);
        layoutField(m_highVelInput, col0X + numColW * 0.52f,
                    gridY + (rowH * 2.0f) + labelH,
                    numColW * 0.48f, rowH - labelH, ctx);
        layoutField(m_volKnob, col1X, knobColY + rowH * 1.8f, knobColW, rowH * 1.8f, ctx);

        // Row 3: Pan knob (col 1)
        layoutField(m_panKnob, col1X, knobColY + rowH * 3.6f, knobColW, rowH * 1.8f, ctx);

        // Row 3 (col 0): Loop toggle
        layoutField(m_loopToggle, col0X, gridY + rowH * 3.0f + 2.0f,
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
        m_mapToggleBtn.render(ctx);

        if (tm) {
            // "Zones: N" label sits LEFT of the Map toggle button.
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Zones: %d",
                          static_cast<int>(m_zones.size()));
            tm->drawText(r, buf,
                         m_mapToggleBtn.bounds().x - 64,
                         m_toolbarRect.y + 4, kFsLabel,
                         Color{180, 180, 180, 255});
        }

        // ── Zone list ──
        renderZoneList(ctx);

        // ── Selected-zone editor ──
        if (m_selected >= 0 && m_selected < static_cast<int>(m_zones.size())) {
            renderEditor(ctx);
        } else if (tm) {
            // Fill the editor area so the empty-state isn't drawn over
            // a flat scrim — gives the prompt some visual weight.
            r.drawRect(m_editorRect.x, m_editorRect.y,
                       m_editorRect.w, m_editorRect.h,
                       Color{32, 32, 36, 255});
            tm->drawText(r, "No zone selected",
                         m_editorRect.x + 8, m_editorRect.y + 8,
                         kFsLabel, Color{170, 170, 175, 255});
            if (m_zones.empty()) {
                tm->drawText(r,
                             "Drop a sample on the panel,",
                             m_editorRect.x + 8, m_editorRect.y + 32,
                             kFsLabel, Color{150, 150, 155, 255});
                tm->drawText(r,
                             "or click Auto-Sample…",
                             m_editorRect.x + 8, m_editorRect.y + 52,
                             kFsLabel, Color{150, 150, 155, 255});
            }
        }

        // ── 2D zone map (only when expanded via toolbar toggle) ──
        if (m_mapExpanded) m_zoneMap.render(ctx);

        // "You can drop a sample here" highlight — common helper.
        DragManager::renderDropHighlight(b, ctx);
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
            if (rectContains(m_mapToggleBtn.bounds(), e.x, e.y))
                return m_mapToggleBtn.dispatchMouseDown(e);
            return false;
        }

        // Zone list — click selects, or drag scrollbar thumb.
        if (rectContains(m_listRect, e.x, e.y)) {
            // Scrollbar hit-test takes precedence so a press on the
            // thumb starts a drag rather than selecting a row that
            // happens to sit behind the scrollbar.
            if (rectContains(m_scrollbarRect, e.x, e.y)) {
                const Rect th = scrollbarThumbRect();
                if (rectContains(th, e.x, e.y)) {
                    m_scrollbarDragActive  = true;
                    m_scrollbarDragOffsetY = e.y - th.y;
                    captureMouse();
                    return true;
                }
                // Click on track outside the thumb — page-jump in
                // that direction (one page = rowsVisible).
                const int rowsVisible = visibleListRows();
                const int total = static_cast<int>(m_zones.size());
                const int maxScroll = std::max(0, total - rowsVisible);
                m_listScroll = std::clamp(
                    m_listScroll + (e.y < th.y ? -rowsVisible : rowsVisible),
                    0, maxScroll);
                invalidate();
                return true;
            }
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

        // 2D zone map (right side when expanded).
        if (m_mapExpanded && rectContains(m_zoneMap.bounds(), e.x, e.y)) {
            return m_zoneMap.dispatchMouseDown(e);
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (rectContains(m_listRect, e.x, e.y)) {
            // Scroll one row per detent. SDL's wheel typically reports
            // dy = ±1 per tick on a regular wheel; trackpad smooth-
            // scroll sends more frequent fractional events, which
            // also work fine through this clamp.
            const int dir = (e.dy > 0 ? -1 : (e.dy < 0 ? 1 : 0));
            const int maxScroll = std::max(
                0, static_cast<int>(m_zones.size()) - visibleListRows());
            m_listScroll = std::clamp(m_listScroll + dir, 0, maxScroll);
            invalidate();
            return true;
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_scrollbarDragActive) {
            const Rect t = scrollbarTrackRect();
            const Rect th = scrollbarThumbRect();
            const float travel = std::max(1.0f, t.h - th.h);
            const float pos = std::clamp(
                e.y - t.y - m_scrollbarDragOffsetY, 0.0f, travel);
            const int total = static_cast<int>(m_zones.size());
            const int rowsVisible = visibleListRows();
            const int maxScroll = std::max(0, total - rowsVisible);
            m_listScroll = static_cast<int>(std::round(
                pos / travel * static_cast<float>(maxScroll)));
            m_listScroll = std::clamp(m_listScroll, 0, maxScroll);
            invalidate();
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_scrollbarDragActive) {
            m_scrollbarDragActive = false;
            releaseMouse();
            invalidate();
            return true;
        }
        return false;
    }

private:
    static constexpr float kToolbarH    = 22.0f;
    // Wider list trades visible-row count for legibility — ~7 rows
    // at the new 22-px row height is fine since wheel/scrollbar
    // both work and the 2D map is the primary navigation tool.
    static constexpr float kListW       = 184.0f;
    static constexpr float kListHeaderH = 22.0f;
    static constexpr float kListRowH    = 22.0f;
    static constexpr float kScrollbarW  = 8.0f;   // visible track + thumb
    // List + editor band sits beneath the toolbar. Tall enough for
    // the new 1.8×rowH Tune/Vol/Pan knob stack (rowH*5.4 + header +
    // padding ≈ 220).
    static constexpr float kListEditorH = 230.0f;
    // Map column (horizontal layout). Sits to the right of the editor
    // when the toolbar's "Map ▶" toggle is on. Width chosen so each
    // MIDI key gets ~3.75 px (480/128) — text labels readable, zone
    // index labels fit, no awkward octave-label pile-ups.
    static constexpr float kMapW        = 480.0f;
    static constexpr float kMapGap      = 6.0f;
    // Panel widths: collapsed = original list+editor only;
    // expanded   = adds the map column.
    static constexpr float kCollapsedW  = 360.0f;
    static constexpr float kExpandedW   = kCollapsedW + kMapGap + kMapW;
    // Pixel sizes — match Theme::metrics fontSize / fontSizeSmall so
    // the panel reads at the same scale as the rest of the UI. The v1
    // pt/26 scaling used by older panels rendered at ~11–13 px which
    // was too small for label rows in this 196-px-tall panel.
    static constexpr float kFsLabel = 14.0f;   // headline + section labels
    static constexpr float kFsTiny  = 15.0f;   // list rows + field captions

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

    // Bring the selected zone's row into the visible window. Called
    // whenever the selection changes (map click, host push, etc.) so
    // a zone selected via the 2D map at index 50 doesn't leave the
    // user staring at a list still scrolled to row 0. Standard
    // "scroll just enough" behaviour: only moves the view when the
    // selection is outside [m_listScroll, m_listScroll + visible).
    void scrollListToSelection() {
        if (m_selected < 0) return;
        // Defer until first layout — setZones can fire before
        // onLayout runs, in which case m_listRect.h = 0 and
        // visibleListRows() collapses to 1, scrolling the list off
        // the bottom for any selection > 0.
        if (m_listRect.h <= 0) return;
        const int rows = visibleListRows();
        const int total = static_cast<int>(m_zones.size());
        const int maxScroll = std::max(0, total - rows);
        if (m_selected < m_listScroll) {
            m_listScroll = m_selected;
        } else if (m_selected >= m_listScroll + rows) {
            m_listScroll = m_selected - rows + 1;
        }
        m_listScroll = std::clamp(m_listScroll, 0, maxScroll);
    }

    static bool rectContains(const Rect& r, float x, float y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    // Scrollbar geometry — kept OUT of the YAWN_TEST_BUILD guard
    // because onMouseDown's hit-test also calls them, and onMouseDown
    // is compiled in test mode. The render() side just reads what
    // they compute.
    Rect scrollbarTrackRect() const {
        return { m_listRect.x + m_listRect.w - kScrollbarW - 1,
                 m_listRect.y + kListHeaderH,
                 kScrollbarW,
                 m_listRect.h - kListHeaderH };
    }

    Rect scrollbarThumbRect() const {
        const Rect t = scrollbarTrackRect();
        const int rowsVisible = visibleListRows();
        const int total = static_cast<int>(m_zones.size());
        const int maxScroll = std::max(1, total - rowsVisible);
        const float thumbH = std::max(20.0f,
            t.h * static_cast<float>(rowsVisible) / std::max(1, total));
        const float travel = t.h - thumbH;
        const float thumbY = t.y + travel *
            static_cast<float>(m_listScroll) / maxScroll;
        return { t.x, thumbY, t.w, thumbH };
    }

#ifndef YAWN_TEST_BUILD
    void renderZoneList(UIContext& ctx) {
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;
        const auto& lr = m_listRect;
        // Reserve space at the right for the visible scrollbar so row
        // text doesn't run under the thumb.
        const float contentR = lr.x + lr.w - kScrollbarW - 2;
        r.drawRect(lr.x, lr.y, lr.w, lr.h, Color{25, 25, 28, 255});
        r.drawRect(lr.x, lr.y, lr.w, kListHeaderH, Color{40, 40, 46, 255});
        if (tm) {
            tm->drawText(r, "# Root  Key       Vel",
                         lr.x + 4, lr.y + 3, kFsTiny,
                         Color{180, 180, 185, 255});
        }

        const int rowsVisible = visibleListRows();
        const int firstIdx = std::clamp(m_listScroll, 0,
            std::max(0, static_cast<int>(m_zones.size()) - 1));
        const int lastIdx = std::min(static_cast<int>(m_zones.size()),
                                     firstIdx + rowsVisible);

        for (int i = firstIdx; i < lastIdx; ++i) {
            const float ry = lr.y + kListHeaderH + (i - firstIdx) * kListRowH;
            const bool sel = (i == m_selected);
            r.drawRect(lr.x, ry, contentR - lr.x, kListRowH - 1,
                       sel ? Color{55, 70, 95, 255}
                            : (i % 2 ? Color{32, 32, 36, 255}
                                      : Color{28, 28, 32, 255}));
            if (!tm) continue;
            const auto& z = m_zones[i];
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%-2d %-4s %s-%s %d-%d",
                i,
                ::yawn::util::midiNoteName(z.rootNote).c_str(),
                ::yawn::util::midiNoteName(z.lowKey).c_str(),
                ::yawn::util::midiNoteName(z.highKey).c_str(),
                z.lowVel, z.highVel);
            tm->drawText(r, buf, lr.x + 4, ry + 3, kFsTiny,
                         sel ? Color{240, 240, 240, 255}
                             : Color{200, 200, 200, 255});
        }

        // Visible draggable scrollbar (8 px wide track + brighter
        // thumb). Dragging the thumb is wired in onMouseDown /
        // onMouseMove via m_scrollbarDragActive.
        if (static_cast<int>(m_zones.size()) > rowsVisible) {
            m_scrollbarRect = scrollbarTrackRect();
            r.drawRect(m_scrollbarRect.x, m_scrollbarRect.y,
                       m_scrollbarRect.w, m_scrollbarRect.h,
                       Color{20, 20, 24, 255});
            const Rect th = scrollbarThumbRect();
            const Color thumbCol = m_scrollbarDragActive
                ? Color{200, 200, 215, 255}
                : Color{150, 150, 165, 255};
            r.drawRect(th.x + 1, th.y, th.w - 2, th.h, thumbCol);
        } else {
            m_scrollbarRect = {};
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

        // Field labels above each input/knob. labelDy ≈ -lineHeight
        // so the label's glyph box sits cleanly in the row's label
        // band without dipping into the field's background. Was -12
        // when fonts were 12 px; bumped here to match kFsTiny=15.
        if (tm) {
            const float labelDy = -18.0f;
            const Color labelCol{180, 180, 185, 255};
            tm->drawText(r, "Root", m_rootInput.bounds().x,
                         m_rootInput.bounds().y + labelDy, kFsTiny, labelCol);
            tm->drawText(r, "Lo",   m_lowKeyInput.bounds().x,
                         m_lowKeyInput.bounds().y + labelDy, kFsTiny, labelCol);
            tm->drawText(r, "Hi",   m_highKeyInput.bounds().x,
                         m_highKeyInput.bounds().y + labelDy, kFsTiny, labelCol);
            tm->drawText(r, "VLo",  m_lowVelInput.bounds().x,
                         m_lowVelInput.bounds().y + labelDy, kFsTiny, labelCol);
            tm->drawText(r, "VHi",  m_highVelInput.bounds().x,
                         m_highVelInput.bounds().y + labelDy, kFsTiny, labelCol);
        }

        // Render every editor widget. They paint themselves.
        for (Widget* w : editorWidgets()) {
            if (w) w->render(ctx);
        }
    }
#endif

    // ── State ──
    std::vector<ZoneRow> m_zones;
    int m_selected     = -1;
    int m_listScroll   = 0;
    bool m_mapExpanded = false;  // toggled by toolbar's "Map ▶" button

    Rect m_toolbarRect{};
    Rect m_listRect{};
    Rect m_editorRect{};
    Rect m_scrollbarRect{};   // computed in renderZoneList; used by hit-test

    // Scrollbar drag state.
    bool  m_scrollbarDragActive  = false;
    float m_scrollbarDragOffsetY = 0.0f;

    FwButton                    m_autoSampleBtn;
    FwButton                    m_removeZoneBtn;
    FwButton                    m_mapToggleBtn;
    FwNumberInput               m_rootInput;
    FwNumberInput               m_lowKeyInput;
    FwNumberInput               m_highKeyInput;
    FwNumberInput               m_lowVelInput;
    FwNumberInput               m_highVelInput;
    FwKnob                      m_tuneKnob;
    FwKnob                      m_volKnob;
    FwKnob                      m_panKnob;
    FwToggle                    m_loopToggle;
    MultisamplerZoneMapWidget   m_zoneMap;

    FieldCallback  m_onZoneChange;
    SelectCallback m_onSelect;
    IndexCallback  m_onRemoveZone;
    VoidCallback   m_onAutoSample;
    WidthCallback  m_onPreferredWidthChanged;
};

// (operator== / != live in MultisamplerZoneRow.h alongside the type.)

} // namespace fw2
} // namespace ui
} // namespace yawn
