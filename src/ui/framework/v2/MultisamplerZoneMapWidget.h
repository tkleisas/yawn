#pragma once
// fw2::MultisamplerZoneMapWidget — 2D key×velocity zone-map editor for
// the Multisampler instrument. Sits below the existing zone-list +
// per-zone-editor inside MultisamplerDisplayPanel.
//
// Layout:
//
//   ┌────────────────────────────────────────────────────────────┐
//   │░░ Zone0 ░░                                                 │ vel 127
//   │             ░░░░ Zone1 ░░░░                                │
//   │░░░░░░░░░░░░░░░ Zone2 (selected, root marker shown) ░░░░░░░░│
//   │                                                            │ vel 1
//   │ ┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│┃│  │ piano strip
//   └────────────────────────────────────────────────────────────┘
//   key 0 (C-1)                                          key 127 (G9)
//
// X-axis: MIDI key 0..127 (linear, low key = left).
// Y-axis: velocity 1..127 (high vel = top — "louder is up").
//
// Interaction:
//   · click interior            → select zone
//   · drag interior             → move zone (preserves dimensions)
//   · drag edge (5 px band)     → resize that edge
//   · drag corner (overlap)     → resize both edges
//   · cursor hint via region    (no SDL cursor changes here — we
//                                draw a 2-px highlight on the
//                                hovered edge to telegraph what a
//                                press would do)
//
// Threading: pure view, identical to MultisamplerDisplayPanel. The
// host pushes ZoneRows in via setZones() each frame; user drags fire
// FieldCallback back to the host which mutates the live
// Multisampler::Zone (same path as the per-field knob editors).

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/MultisamplerZoneRow.h"
#include "ui/Theme.h"
#include "util/NoteNames.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class MultisamplerZoneMapWidget : public Widget {
public:
    using ZoneRow = MultisamplerZoneRow;

    using FieldCallback  = std::function<void(int zoneIdx, const ZoneRow&)>;
    using SelectCallback = std::function<void(int zoneIdx)>;

    MultisamplerZoneMapWidget() {
        setName("MultisamplerZoneMap");
        // We own our own dispatch — captureMouse is called explicitly
        // in onMouseDown when the press lands on a zone. Without
        // disabling auto-capture, a press on empty map area would
        // capture the widget anyway and any subsequent move would
        // re-enter dispatchMouseMove → capturedWidget() → us, which is
        // benign here but pointless work.
        setAutoCaptureOnUnhandledPress(false);
    }

    // ─── Host-driven state ──────────────────────────────────────────
    void setZones(std::vector<ZoneRow> zones) {
        if (zones != m_zones) {
            m_zones = std::move(zones);
            if (m_selected >= static_cast<int>(m_zones.size()))
                m_selected = m_zones.empty() ? -1 : 0;
        }
    }

    int  selectedZone() const { return m_selected; }
    void setSelectedZone(int idx) {
        idx = std::clamp(idx, -1, static_cast<int>(m_zones.size()) - 1);
        if (idx == m_selected) return;
        m_selected = idx;
        if (m_onSelect) m_onSelect(idx);
    }

    // ─── Callbacks ──────────────────────────────────────────────────
    void setOnZoneFieldChange(FieldCallback cb) { m_onZoneChange = std::move(cb); }
    void setOnSelectionChanged(SelectCallback cb) { m_onSelect = std::move(cb); }

    // ─── Layout / render ────────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, std::max(c.minH, 92.0f)});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        // Map area is the bounds minus a thin piano-keyboard strip at
        // the bottom for orientation.
        m_mapRect = {bounds.x, bounds.y,
                     bounds.w, bounds.h - kPianoStripH};
        m_pianoRect = {bounds.x, bounds.y + bounds.h - kPianoStripH,
                       bounds.w, kPianoStripH};
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible() || !ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        // ── Background ──
        r.drawRect(m_mapRect.x, m_mapRect.y, m_mapRect.w, m_mapRect.h,
                   Color{20, 20, 24, 255});

        // Octave gridlines (every 12 keys) so the eye can find C2,
        // C3, C4 etc. without counting cells. C4 (key 60) gets a
        // slightly brighter line as the canonical reference.
        for (int oct = 0; oct < 11; ++oct) {
            const int key = oct * 12;
            if (key > 127) break;
            const float gx = keyCellLeft(key);
            const Color col = (key == 60) ? Color{55, 55, 70, 255}
                                          : Color{38, 38, 44, 255};
            r.drawRect(gx, m_mapRect.y, 1.0f, m_mapRect.h, col);
        }

        // ── Zones (selected last so it draws on top) ──
        for (int i = 0; i < static_cast<int>(m_zones.size()); ++i) {
            if (i == m_selected) continue;
            renderZone(ctx, i, /*isSelected=*/false);
        }
        if (m_selected >= 0 && m_selected < static_cast<int>(m_zones.size()))
            renderZone(ctx, m_selected, /*isSelected=*/true);

        // ── Hovered-edge hint ──
        if (m_hoverIdx >= 0 && m_hoverRegion != Region::Interior &&
            m_dragIdx < 0) {
            renderEdgeHint(ctx, m_hoverIdx, m_hoverRegion);
        }

        // ── Piano keyboard strip ──
        renderPianoStrip(ctx);

        // ── Hover tooltip (top-right corner) ──
        if (tm && m_hoverIdx >= 0 &&
            m_hoverIdx < static_cast<int>(m_zones.size()) && m_dragIdx < 0) {
            const auto& z = m_zones[m_hoverIdx];
            char buf[80];
            std::snprintf(buf, sizeof(buf), "Z%d  %s-%s  v%d-%d",
                m_hoverIdx,
                ::yawn::util::midiNoteName(z.lowKey).c_str(),
                ::yawn::util::midiNoteName(z.highKey).c_str(),
                z.lowVel, z.highVel);
            drawReadout(r, *tm, buf, Color{15, 15, 18, 230},
                        Color{220, 220, 225, 255});
        }

        // ── Live drag readout ──
        if (tm && m_dragIdx >= 0 &&
            m_dragIdx < static_cast<int>(m_zones.size())) {
            const auto& z = m_zones[m_dragIdx];
            char buf[80];
            std::snprintf(buf, sizeof(buf), "%s-%s  v%d-%d",
                ::yawn::util::midiNoteName(z.lowKey).c_str(),
                ::yawn::util::midiNoteName(z.highKey).c_str(),
                z.lowVel, z.highVel);
            drawReadout(r, *tm, buf, Color{15, 25, 35, 235},
                        Color{180, 220, 240, 255});
        }
    }

    void drawReadout(Renderer2D& r, TextMetrics& tm,
                     const char* buf, Color bg, Color fg) const {
        const float tw = tm.textWidth(buf, kFsTooltip);
        const float pad = 5.0f;
        const float boxH = kFsTooltip + 4.0f;
        const float tx = m_mapRect.x + m_mapRect.w - tw - pad * 2 - 2;
        const float ty = m_mapRect.y + 2;
        r.drawRect(tx - pad, ty, tw + pad * 2, boxH, bg);
        tm.drawText(r, buf, tx, ty + 2, kFsTooltip, fg);
    }
#endif

    bool onMouseDown(MouseEvent& e) override {
        if (!rectContains(m_mapRect, e.x, e.y)) return false;

        Hit h = hitTest(e.x, e.y);
        if (h.zoneIdx < 0) {
            // Empty area click — defocus selection.
            setSelectedZone(-1);
            return true;
        }

        // Select first (so the editor on the host panel reflects what
        // the user is now dragging).
        setSelectedZone(h.zoneIdx);

        m_dragIdx       = h.zoneIdx;
        m_dragRegion    = h.region;
        m_dragStartX    = e.x;
        m_dragStartY    = e.y;
        m_dragStartZone = m_zones[h.zoneIdx];

        captureMouse();
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        // Hover tracking when not dragging.
        if (m_dragIdx < 0) {
            Hit h = hitTest(e.x, e.y);
            if (h.zoneIdx != m_hoverIdx || h.region != m_hoverRegion) {
                m_hoverIdx    = h.zoneIdx;
                m_hoverRegion = h.region;
                invalidate();
            }
            return false;
        }

        // Active drag — translate pixel delta to (key, vel) delta and
        // mutate the appropriate edge(s).
        const float dxPx = e.x - m_dragStartX;
        const float dyPx = e.y - m_dragStartY;
        const int   dKey = pixelsToKeyDelta(dxPx);
        const int   dVel = pixelsToVelDelta(dyPx);  // negative = drag up = increase vel

        ZoneRow next = m_dragStartZone;

        switch (m_dragRegion) {
            case Region::Interior: {
                // Move: shift both lo/hi by the same delta, clamp so
                // the zone stays inside [0,127] without changing its
                // size.
                const int kSpan = m_dragStartZone.highKey - m_dragStartZone.lowKey;
                int newLow = std::clamp(m_dragStartZone.lowKey + dKey, 0, 127 - kSpan);
                next.lowKey  = newLow;
                next.highKey = newLow + kSpan;
                // Velocity move (drag up = increase vel → subtract dVel).
                const int vSpan = m_dragStartZone.highVel - m_dragStartZone.lowVel;
                int newVLo = std::clamp(m_dragStartZone.lowVel - dVel, 1, 127 - vSpan);
                next.lowVel  = newVLo;
                next.highVel = newVLo + vSpan;
                // Keep the rootNote anchored to the zone's lowKey
                // offset so moving the zone moves the root with it
                // (Kontakt / Logic Sampler default).
                const int rootOffset = m_dragStartZone.rootNote - m_dragStartZone.lowKey;
                next.rootNote = std::clamp(next.lowKey + rootOffset, 0, 127);
                break;
            }
            case Region::EdgeL:
            case Region::CornerTL:
            case Region::CornerBL:
                next.lowKey = std::clamp(m_dragStartZone.lowKey + dKey,
                                         0, m_dragStartZone.highKey);
                break;
            case Region::EdgeR:
            case Region::CornerTR:
            case Region::CornerBR:
                next.highKey = std::clamp(m_dragStartZone.highKey + dKey,
                                          m_dragStartZone.lowKey, 127);
                break;
            case Region::EdgeT:
            case Region::EdgeB:
            default: break;
        }
        switch (m_dragRegion) {
            case Region::EdgeT:
            case Region::CornerTL:
            case Region::CornerTR:
                // Top edge = highVel; drag up = decrease pixel y → -dVel
                next.highVel = std::clamp(m_dragStartZone.highVel - dVel,
                                          m_dragStartZone.lowVel, 127);
                break;
            case Region::EdgeB:
            case Region::CornerBL:
            case Region::CornerBR:
                next.lowVel = std::clamp(m_dragStartZone.lowVel - dVel,
                                         1, m_dragStartZone.highVel);
                break;
            default: break;
        }

        if (next != m_zones[m_dragIdx]) {
            m_zones[m_dragIdx] = next;
            if (m_onZoneChange) m_onZoneChange(m_dragIdx, next);
            invalidate();
        }
        return true;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_dragIdx < 0) return false;
        m_dragIdx    = -1;
        m_dragRegion = Region::Interior;
        releaseMouse();
        invalidate();
        return true;
    }

private:
    enum class Region {
        Interior,
        EdgeL, EdgeR, EdgeT, EdgeB,
        CornerTL, CornerTR, CornerBL, CornerBR,
    };
    struct Hit {
        int    zoneIdx = -1;
        Region region  = Region::Interior;
    };

    static constexpr float kPianoStripH = 26.0f;   // tall enough for a 13-px label inside
    static constexpr float kEdgeHitPx   = 5.0f;
    // Font sizes — match the rest of the panel's kFsLabel/kFsTiny so
    // text is readable instead of the previous 8-10 px sub-pixel
    // mush. These are deliberately one notch larger than the panel's
    // own labels — the map widget is sized to host them.
    static constexpr float kFsTooltip   = 14.0f;
    static constexpr float kFsZoneLbl   = 14.0f;
    static constexpr float kFsPianoLbl  = 13.0f;

    // ── Coord helpers ──
    // Keys are cells of width mapW/128 — gives single-note zones
    // (lowKey == highKey) a visible ~2-3 px column instead of a
    // zero-width line.
    float keyCellLeft(int key) const {
        return m_mapRect.x + (static_cast<float>(key) / 128.0f) * m_mapRect.w;
    }
    float keyCellRight(int key) const { return keyCellLeft(key + 1); }

    int xToKey(float x) const {
        const float t = (x - m_mapRect.x) / m_mapRect.w;
        return std::clamp(static_cast<int>(std::floor(t * 128.0f)), 0, 127);
    }

    // Vel cells are mapH/127 tall; vel 127 sits at the TOP.
    float velCellTop(int vel) const {
        return m_mapRect.y + (1.0f - static_cast<float>(vel) / 127.0f) * m_mapRect.h;
    }
    int yToVel(float y) const {
        const float t = (y - m_mapRect.y) / m_mapRect.h;
        return std::clamp(static_cast<int>(std::round((1.0f - t) * 127.0f)), 1, 127);
    }

    // For drag deltas — we want raw pixel-to-units conversion (so
    // small mouse moves don't snap until they cross a cell boundary).
    int pixelsToKeyDelta(float dxPx) const {
        return static_cast<int>(std::round(dxPx / (m_mapRect.w / 128.0f)));
    }
    int pixelsToVelDelta(float dyPx) const {
        // Positive dy = mouse moved DOWN = lower velocity → return
        // positive number to subtract from highVel for "top edge
        // drag up = vel up" semantics.
        return static_cast<int>(std::round(dyPx / (m_mapRect.h / 127.0f)));
    }

    Rect zoneRect(const ZoneRow& z) const {
        const float x0 = keyCellLeft(z.lowKey);
        const float x1 = keyCellRight(z.highKey);
        const float y0 = velCellTop(z.highVel);
        const float y1 = velCellTop(z.lowVel - 1);  // bottom edge of lowVel cell
        return {x0, y0, std::max(1.0f, x1 - x0), std::max(1.0f, y1 - y0)};
    }

    static bool rectContains(const Rect& r, float x, float y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    // Hit-test: prefer the smallest-area zone under the cursor (more
    // specific zones — a single layer in a stack — beat the giant
    // background zone). Within a hit, prefer edges over interior.
    Hit hitTest(float mx, float my) const {
        if (!rectContains(m_mapRect, mx, my)) return {};

        int bestIdx = -1;
        float bestArea = 1e30f;
        for (int i = 0; i < static_cast<int>(m_zones.size()); ++i) {
            Rect zr = zoneRect(m_zones[i]);
            if (!rectContains(zr, mx, my)) continue;
            const float area = zr.w * zr.h;
            if (area < bestArea) {
                bestArea = area;
                bestIdx  = i;
            }
        }
        if (bestIdx < 0) return {};

        Rect zr = zoneRect(m_zones[bestIdx]);
        const bool nearL = (mx - zr.x) < kEdgeHitPx;
        const bool nearR = (zr.x + zr.w - mx) < kEdgeHitPx;
        const bool nearT = (my - zr.y) < kEdgeHitPx;
        const bool nearB = (zr.y + zr.h - my) < kEdgeHitPx;

        Region region = Region::Interior;
        if      (nearT && nearL) region = Region::CornerTL;
        else if (nearT && nearR) region = Region::CornerTR;
        else if (nearB && nearL) region = Region::CornerBL;
        else if (nearB && nearR) region = Region::CornerBR;
        else if (nearL)          region = Region::EdgeL;
        else if (nearR)          region = Region::EdgeR;
        else if (nearT)          region = Region::EdgeT;
        else if (nearB)          region = Region::EdgeB;
        return {bestIdx, region};
    }

#ifndef YAWN_TEST_BUILD
    // Stable per-zone colour from index. HSL-ish hue rotation; we just
    // rotate through a small palette so adjacent zone indices look
    // distinct without any one colour dominating.
    static Color zoneColor(int idx) {
        // 8-step palette tuned for the dark theme background.
        static const Color palette[8] = {
            {120, 175, 230, 255},  // sky blue
            {230, 175, 120, 255},  // orange
            {180, 230, 120, 255},  // lime
            {230, 120, 180, 255},  // pink
            {180, 120, 230, 255},  // purple
            {120, 230, 200, 255},  // teal
            {230, 220, 120, 255},  // yellow
            {230, 130, 130, 255},  // coral
        };
        return palette[idx & 7];
    }

    void renderZone(UIContext& ctx, int idx, bool isSelected) const {
        auto& r = *ctx.renderer;
        const auto& z = m_zones[idx];
        const Rect zr = zoneRect(z);
        const Color c = zoneColor(idx);

        // Translucent fill — stack so overlap zones still read.
        const uint8_t fillA = isSelected ? 110 : 60;
        r.drawRect(zr.x, zr.y, zr.w, zr.h, Color{c.r, c.g, c.b, fillA});

        // Border — 4 thin rects so we don't need a stroke primitive.
        const Color borderCol = isSelected
            ? Color{255, 255, 255, 230}
            : Color{c.r, c.g, c.b, 200};
        const float bw = isSelected ? 2.0f : 1.0f;
        r.drawRect(zr.x, zr.y, zr.w, bw, borderCol);
        r.drawRect(zr.x, zr.y + zr.h - bw, zr.w, bw, borderCol);
        r.drawRect(zr.x, zr.y, bw, zr.h, borderCol);
        r.drawRect(zr.x + zr.w - bw, zr.y, bw, zr.h, borderCol);

        // Selected: vertical line at root note + small label.
        if (isSelected) {
            const float rx = keyCellLeft(z.rootNote)
                           + (m_mapRect.w / 128.0f) * 0.5f;
            r.drawRect(rx - 0.5f, zr.y, 1.5f, zr.h,
                       Color{255, 200, 80, 220});
        }

        // Inline label "N" if there's room. Threshold scaled so the
        // glyph isn't clipped — bigger font needs a bigger min cell.
        const float minW = kFsZoneLbl * 1.6f + 4;     // ~26 px
        const float minH = kFsZoneLbl + 4;            // ~18 px
        if (ctx.textMetrics && zr.w >= minW && zr.h >= minH) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", idx);
            ctx.textMetrics->drawText(*ctx.renderer, buf,
                zr.x + 3, zr.y + 2, kFsZoneLbl,
                isSelected ? Color{15, 15, 20, 255}
                           : Color{25, 25, 30, 240});
        }
    }

    void renderEdgeHint(UIContext& ctx, int idx, Region region) const {
        auto& r = *ctx.renderer;
        const Rect zr = zoneRect(m_zones[idx]);
        const Color hi{255, 255, 255, 255};
        const float t = 2.0f;  // hint thickness
        switch (region) {
            case Region::EdgeL:
                r.drawRect(zr.x - 1, zr.y, t, zr.h, hi); break;
            case Region::EdgeR:
                r.drawRect(zr.x + zr.w - t + 1, zr.y, t, zr.h, hi); break;
            case Region::EdgeT:
                r.drawRect(zr.x, zr.y - 1, zr.w, t, hi); break;
            case Region::EdgeB:
                r.drawRect(zr.x, zr.y + zr.h - t + 1, zr.w, t, hi); break;
            case Region::CornerTL:
                r.drawRect(zr.x - 1, zr.y, t, kEdgeHitPx, hi);
                r.drawRect(zr.x, zr.y - 1, kEdgeHitPx, t, hi); break;
            case Region::CornerTR:
                r.drawRect(zr.x + zr.w - t + 1, zr.y, t, kEdgeHitPx, hi);
                r.drawRect(zr.x + zr.w - kEdgeHitPx, zr.y - 1, kEdgeHitPx, t, hi); break;
            case Region::CornerBL:
                r.drawRect(zr.x - 1, zr.y + zr.h - kEdgeHitPx, t, kEdgeHitPx, hi);
                r.drawRect(zr.x, zr.y + zr.h - t + 1, kEdgeHitPx, t, hi); break;
            case Region::CornerBR:
                r.drawRect(zr.x + zr.w - t + 1, zr.y + zr.h - kEdgeHitPx, t, kEdgeHitPx, hi);
                r.drawRect(zr.x + zr.w - kEdgeHitPx, zr.y + zr.h - t + 1, kEdgeHitPx, t, hi); break;
            case Region::Interior: default: break;
        }
    }

    void renderPianoStrip(UIContext& ctx) const {
        auto& r = *ctx.renderer;
        // White-key background — slightly lighter than the map bg so
        // the strip reads as a separate band.
        r.drawRect(m_pianoRect.x, m_pianoRect.y,
                   m_pianoRect.w, m_pianoRect.h,
                   Color{210, 210, 215, 255});
        // Black-key strips on top.
        static const bool isBlack[12] = {
            false, true, false, true, false, false,
            true, false, true, false, true, false
        };
        const float cellW = m_mapRect.w / 128.0f;
        const float blackH = m_pianoRect.h * 0.6f;
        for (int key = 0; key < 128; ++key) {
            if (!isBlack[key % 12]) continue;
            const float kx = keyCellLeft(key);
            r.drawRect(kx, m_pianoRect.y, cellW, blackH,
                       Color{30, 30, 35, 255});
        }
        // Octave-C labels (C0 .. C9). Sparse — pick the largest
        // step (1, 2 or 3 octaves) that lets a "C9" label fit
        // without overlap. Drawn INSIDE the strip's lower band
        // (white-key region) so they're always legible.
        if (ctx.textMetrics) {
            const float labelW = ctx.textMetrics->textWidth("C-1", kFsPianoLbl) + 2;
            const float octavePx = cellW * 12.0f;
            int step = 1;
            if (labelW > octavePx) step = 2;
            if (labelW > octavePx * 2) step = 3;
            for (int oct = 0; oct < 11; oct += step) {
                const int key = oct * 12;
                if (key > 127) break;
                const float kx = keyCellLeft(key) + 1;
                char buf[8];
                std::snprintf(buf, sizeof(buf), "C%d", oct - 1);
                ctx.textMetrics->drawText(r, buf, kx,
                    m_pianoRect.y + blackH - 1, kFsPianoLbl,
                    Color{40, 40, 50, 255});
            }
        }
    }
#endif

    // ── State ──
    std::vector<ZoneRow> m_zones;
    int m_selected = -1;

    Rect m_mapRect{};
    Rect m_pianoRect{};

    // Hover.
    int    m_hoverIdx    = -1;
    Region m_hoverRegion = Region::Interior;

    // Drag.
    int      m_dragIdx       = -1;
    Region   m_dragRegion    = Region::Interior;
    float    m_dragStartX    = 0.0f;
    float    m_dragStartY    = 0.0f;
    ZoneRow  m_dragStartZone{};

    FieldCallback  m_onZoneChange;
    SelectCallback m_onSelect;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
