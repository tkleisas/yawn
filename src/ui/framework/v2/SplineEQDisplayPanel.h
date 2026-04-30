#pragma once
// fw2::SplineEQDisplayPanel — visualization panel for the Spline EQ.
//
// Phase B-1 (this commit): visualization only.
//   - Log-frequency grid (octave lines + dB gridlines)
//   - Pre-EQ spectrum (blue) and post-EQ spectrum (orange) overlaid
//     using the SplineEQ's published inputSpectrum/outputSpectrum
//     accessors
//   - Cascaded magnitude response curve drawn through all enabled
//     nodes, computed analytically per pixel column
//   - Node markers (circles) at each enabled node's (freq, gain)
//     position, coloured by node type
//
// Phase B-2 (next commit): mouse interaction.
//   - Drag node → set freq/gain
//   - Scroll wheel on hovered node → adjust Q
//   - Shift-drag vertical → adjust Q (trackpad-friendly alternative)
//   - Click empty area → add new node
//   - Right-click node → context menu (Type / Delete)
//
// The panel is wired in via DetailPanelWidget::setupAudioEffectDisplay
// at width 400 px (wider than the standard ~200 px display panels —
// the EQ specifically needs the room for legible curves).

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/GroupedKnobBody.h"   // CustomDeviceBody base
#include "ui/Theme.h"
#include "effects/SplineEQ.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace yawn {
namespace ui {
namespace fw2 {

// CustomDeviceBody (not plain Widget) so the panel REPLACES the
// per-param knob list as the device's body. This matters because
// DeviceWidget's preferredWidth() sums up the knob count to size
// the device strip; with 40 EQ knobs that pushes the strip past
// 1200 px and the custom panel inherits that wide bound. As a
// custom body, the panel controls its own preferredBodyWidth() so
// the strip sizes to whatever we want (270 px) regardless of knob
// count. The knobs are still settable via automation / preset, just
// not shown in the strip — the panel is the editor.
class SplineEQDisplayPanel : public CustomDeviceBody {
public:
    SplineEQDisplayPanel() {
        setName("SplineEQDisplay");
        setAutoCaptureOnUnhandledPress(false);
    }

    // ── CustomDeviceBody contract ───────────────────────────────────
    // 360 px (= 270 * 4/3) — the previous 270 px was a touch tight
    // for the freq response curve to read clearly across the full
    // 20 Hz – 20 kHz log span. User-tuned by eye.
    float preferredBodyWidth() const override { return 360.0f; }
    void  updateParamValue(int /*index*/, float /*value*/) override {
        // No-op — the panel reads from the live SplineEQ pointer
        // every frame, so external param changes show up
        // automatically without needing an explicit notification.
    }
    void  setOnParamChange(std::function<void(int, float)> cb) override {
        m_onParamChange = std::move(cb);
    }

    // ── Host-driven state ───────────────────────────────────────────
    // The detail panel calls this each frame from displayUpdater.
    // We keep a raw pointer to the live SplineEQ — its accessors
    // are read at frame rate while audio thread mutates the same
    // params; for visualisation that's fine (eventual consistency
    // is the goal, not sample-accurate sync).
    void setEQ(effects::SplineEQ* eq) { m_eq = eq; }
    void setSampleRate(double sr)     { m_sampleRate = sr; }

    // setOnParamChange is inherited from CustomDeviceBody (declared
    // above as the override). Callback signature is std::function<
    // void(int paramIdx, float value)> — DetailPanelWidget wires it
    // to DeviceRef::setParam so panel-side edits go through the
    // same automation / MIDI Learn / preset path as knob edits.

    Size onMeasure(Constraints c, UIContext&) override {
        // Match GroupedKnobBody's pattern (the other CustomDeviceBody
        // implementor): take preferredBodyWidth on the X axis, fill
        // available height on Y. The device strip's body height is
        // typically ~150–200 px which is plenty for the curve +
        // spectrum + node markers.
        return c.constrain({preferredBodyWidth(), c.maxH});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        // Background + outline
        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{18, 18, 24, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        const float pad = 4.0f;
        const float gx = m_bounds.x + pad;
        const float gy = m_bounds.y + pad;
        const float gw = m_bounds.w - pad * 2.0f;
        const float gh = m_bounds.h - pad * 2.0f;
        if (gw < 10 || gh < 10) return;

        r.pushClip(gx, gy, gw, gh);

        renderGridlines(r, ctx, gx, gy, gw, gh);
        renderSpectrum(r, gx, gy, gw, gh);
        renderResponseCurve(r, gx, gy, gw, gh);
        renderNodes(r, ctx, gx, gy, gw, gh);
        renderHoverInfo(r, ctx, gx, gy, gw, gh);

        r.popClip();
    }
#endif

    // ── Mouse interaction ───────────────────────────────────────────
    bool onMouseDown(MouseEvent& e) override {
        if (!m_eq) return false;
        const Rect r = gridRect();
        if (e.x < r.x || e.x >= r.x + r.w ||
            e.y < r.y || e.y >= r.y + r.h) return false;

        const int hit = hitTestNode(e.x, e.y, r);

        if (e.button == MouseButton::Right) {
            // Right-click = cycle node type forward (or backward
            // with Shift). Quick context-menu replacement; full
            // popup menu is a future improvement.
            if (hit >= 0) {
                int t = static_cast<int>(m_eq->nodeType(hit));
                const int dir = (e.modifiers & ModifierKey::Shift) ? -1 : 1;
                t = (t + dir + effects::SplineEQ::ntCount)
                    % effects::SplineEQ::ntCount;
                emitParam(hit, effects::SplineEQ::pfType,
                          static_cast<float>(t));
            }
            return true;
        }

        if (hit >= 0) {
            // Double-click on a node deletes it (disables the node).
            // Standard "click to interact, double-click to remove"
            // discoverable pattern — no menu hunt required.
            if (e.clickCount >= 2) {
                emitParam(hit, effects::SplineEQ::pfEnabled, 0.0f);
                return true;
            }
            m_dragNodeIdx = hit;
            m_dragShift = (e.modifiers & ModifierKey::Shift) != 0;
            m_dragStartFreq = m_eq->nodeFreqHz(hit);
            m_dragStartGain = m_eq->nodeGainDb(hit);
            m_dragStartQ    = m_eq->nodeQ(hit);
            m_dragStartX    = e.x;
            m_dragStartY    = e.y;
            captureMouse();
            return true;
        }

        // Empty-grid click: enable the first disabled node and place
        // it at the clicked frequency / gain. Provides the "click to
        // add" affordance without needing a separate "+ Add Node"
        // button. Type defaults to Peak — user can right-click to
        // change.
        for (int n = 0; n < effects::SplineEQ::kMaxNodes; ++n) {
            if (m_eq->nodeEnabled(n)) continue;
            const float fHz = xToFreq(e.x, r);
            const float gDb = yToGainDb(e.y, r);
            emitParam(n, effects::SplineEQ::pfFreq,
                      effects::SplineEQ::hzToTune(fHz));
            emitParam(n, effects::SplineEQ::pfGain, gDb);
            emitParam(n, effects::SplineEQ::pfType,
                      static_cast<float>(effects::SplineEQ::ntPeak));
            emitParam(n, effects::SplineEQ::pfEnabled, 1.0f);
            // Auto-grab the new node so the user's mouse-down → drag
            // gesture starts moving it immediately.
            m_dragNodeIdx = n;
            m_dragShift = false;
            m_dragStartFreq = fHz;
            m_dragStartGain = gDb;
            m_dragStartQ    = m_eq->nodeQ(n);
            m_dragStartX    = e.x;
            m_dragStartY    = e.y;
            captureMouse();
            return true;
        }
        return true;   // consume the click so it doesn't fall through
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (!m_eq) return false;
        const Rect r = gridRect();
        // Hover tracking — drives the per-node info readout. We
        // update m_hoverNode any time the mouse moves over the
        // panel; while dragging we lock it to the dragged node so
        // the readout doesn't flicker as the cursor sweeps over
        // other nodes mid-drag.
        if (m_dragNodeIdx >= 0) m_hoverNode = m_dragNodeIdx;
        else                    m_hoverNode = hitTestNode(e.x, e.y, r);
        if (m_dragNodeIdx < 0) return false;
        if (m_dragShift) {
            // Shift-drag: vertical motion = Q. Pixel scaling is
            // chosen so dragging the full grid height takes the Q
            // through its useful range (0.1..10).
            const float dy = e.y - m_dragStartY;
            const float qSpan = 9.9f;   // 10 - 0.1
            const float perPx = qSpan / std::max(20.0f, r.h);
            const float newQ = std::clamp(
                m_dragStartQ - dy * perPx, 0.10f, 10.0f);
            emitParam(m_dragNodeIdx, effects::SplineEQ::pfQ, newQ);
        } else {
            // Normal drag: X = freq, Y = gain. Reads from the live
            // mouse position, not delta-from-start, so the cursor
            // stays glued to the node throughout the drag.
            const float fHz = xToFreq(e.x, r);
            const float gDb = yToGainDb(e.y, r);
            emitParam(m_dragNodeIdx, effects::SplineEQ::pfFreq,
                      effects::SplineEQ::hzToTune(fHz));
            emitParam(m_dragNodeIdx, effects::SplineEQ::pfGain, gDb);
        }
        return true;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_dragNodeIdx >= 0) {
            m_dragNodeIdx = -1;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onScroll(ScrollEvent& e) override {
        if (!m_eq) return false;
        const Rect r = gridRect();
        if (e.x < r.x || e.x >= r.x + r.w ||
            e.y < r.y || e.y >= r.y + r.h) return false;

        const int hit = hitTestNode(e.x, e.y, r);
        if (hit < 0) return false;
        // Wheel up → larger Q (narrower bell), wheel down → smaller
        // Q (wider). 10% per notch feels right; precise scroll wheels
        // get smaller increments via dy magnitude.
        const float curQ = m_eq->nodeQ(hit);
        const float step = std::max(0.05f, std::abs(e.dy)) * 0.1f;
        const float newQ = std::clamp(
            curQ * (e.dy > 0 ? (1.0f + step) : 1.0f / (1.0f + step)),
            0.10f, 10.0f);
        emitParam(hit, effects::SplineEQ::pfQ, newQ);
        return true;
    }

private:
#ifndef YAWN_TEST_BUILD

    // ── Coordinate helpers ──
    // X axis: log frequency 20 Hz – 20 kHz over the full grid width.
    // Y axis: linear dB gain ±kMaxGainDb (default 18) — match the EQ
    // engine's per-node clamp so a node maxed-out exactly hits the
    // top/bottom of the panel.
    static constexpr float kFreqLow  = 20.0f;
    static constexpr float kFreqHigh = 20000.0f;
    static constexpr float kMaxGainDb = 18.0f;

    static float freqToX(float hz, float gx, float gw) {
        const float t = std::log(std::clamp(hz, kFreqLow, kFreqHigh) / kFreqLow)
                       / std::log(kFreqHigh / kFreqLow);
        return gx + t * gw;
    }
    static float dbToY(float db, float gy, float gh) {
        const float t = (db + kMaxGainDb) / (2.0f * kMaxGainDb);
        return gy + (1.0f - std::clamp(t, 0.0f, 1.0f)) * gh;
    }

    void renderGridlines(::yawn::ui::Renderer2D& r, UIContext& ctx,
                          float gx, float gy, float gw, float gh) {
        // Vertical octave lines at 100 / 1k / 10k Hz with sub-decade
        // accents. Keeps the panel readable without becoming busy.
        const Color majC{60, 60, 70, 200};
        const Color minC{40, 40, 48, 160};
        for (float decade = 100.0f; decade <= 10000.0f; decade *= 10.0f) {
            for (int m = 1; m <= 9; ++m) {
                const float hz = decade * m;
                if (hz < kFreqLow || hz > kFreqHigh) continue;
                const float x = freqToX(hz, gx, gw);
                const Color& c = (m == 1) ? majC : minC;
                r.drawRect(x, gy, 1, gh, c);
            }
        }
        // Horizontal dB lines: 0 (centre, brightest), ±6, ±12.
        for (int db : {-12, -6, 6, 12}) {
            const float y = dbToY(static_cast<float>(db), gy, gh);
            r.drawRect(gx, y, gw, 1, minC);
        }
        const float zeroY = dbToY(0.0f, gy, gh);
        r.drawRect(gx, zeroY, gw, 1, Color{90, 90, 105, 220});

        // Frequency-axis labels — small, only at the major decades.
        if (auto* tm = ctx.textMetrics) {
            const float fs = 9.0f * (48.0f / 26.0f);   // ≈ 16.6 px
            const Color lblC{120, 120, 140, 200};
            for (auto [hz, lbl] : std::initializer_list<std::pair<float, const char*>>{
                     {100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}}) {
                const float x = freqToX(hz, gx, gw);
                tm->drawText(r, lbl, x + 2, gy + gh - fs - 2, fs, lblC);
            }
        }
    }

    void renderSpectrum(::yawn::ui::Renderer2D& r,
                         float gx, float gy, float gw, float gh) {
        if (!m_eq) return;
        const float* in  = m_eq->inputSpectrum();
        const float* out = m_eq->outputSpectrum();
        const int n = m_eq->spectrumSize();
        if (!in || !out || n < 2) return;

        // Convert linear magnitudes to dB and draw as filled regions
        // from the bottom of the panel. Pre is blue-tinted, post is
        // orange-tinted — both translucent so they overlay readably.
        const Color preC {80,  140, 220, 110};
        const Color postC{255, 160,  60, 130};

        // Map spectrum bins (which are log-binned 20 Hz..Nyquist by
        // SplineEQ) onto pixel columns. Bin index → freq → x.
        const float nyq = static_cast<float>(m_sampleRate) * 0.5f;
        const float logLow  = std::log(20.0f);
        const float logHigh = std::log(nyq);

        // Draw post first (so pre overlays as the brighter colour),
        // then pre. Both as thin vertical bars at each bin.
        // Magnitude → dB → screen Y. Floor at -60 dB so the noise
        // floor doesn't fill the whole panel.
        auto magToY = [&](float lin) {
            const float db = 20.0f * std::log10(std::max(lin, 1e-6f));
            const float clamped = std::clamp(db, -60.0f, 0.0f);
            const float t = (clamped + 60.0f) / 60.0f;   // 0 at -60, 1 at 0
            return gy + gh - t * gh * 0.65f;             // top 35% reserved for curve
        };

        for (int b = 0; b < n - 1; ++b) {
            const float t   = static_cast<float>(b) /
                              static_cast<float>(n - 1);
            const float fc  = std::exp(logLow + t * (logHigh - logLow));
            const float t2  = static_cast<float>(b + 1) /
                              static_cast<float>(n - 1);
            const float fc2 = std::exp(logLow + t2 * (logHigh - logLow));
            const float x   = freqToX(fc,  gx, gw);
            const float x2  = freqToX(fc2, gx, gw);
            const float w   = std::max(1.0f, x2 - x);

            const float yIn  = magToY(in[b]);
            const float yOut = magToY(out[b]);
            r.drawRect(x, yIn,  w, gy + gh - yIn,  preC);
            r.drawRect(x, yOut, w, gy + gh - yOut, postC);
        }
    }

    // Analytic biquad magnitude at frequency f. Computes the digital
    // transfer-function magnitude |H(e^jω)| using the RBJ EQ-cookbook
    // formulas — same coefficients the Biquad class uses internally,
    // evaluated symbolically at the target frequency. Avoids
    // exposing the per-node biquad state from SplineEQ.
    static float nodeMagAt(effects::SplineEQ::NodeType type,
                            float fc, float gainDb, float q,
                            float f, double sr) {
        const double w0 = 2.0 * 3.14159265358979323846 *
                          static_cast<double>(fc) / sr;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * std::max(0.05f, q));
        const double A = (type == effects::SplineEQ::ntPeak ||
                          type == effects::SplineEQ::ntLowShelf ||
                          type == effects::SplineEQ::ntHighShelf)
                          ? std::pow(10.0, gainDb / 40.0) : 1.0;
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (type) {
            case effects::SplineEQ::ntPeak:
                b0 = 1 + alpha * A; b1 = -2 * cosw0; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cosw0; a2 = 1 - alpha / A;
                break;
            case effects::SplineEQ::ntLowShelf: {
                const double sa = std::sqrt(A);
                b0 = A * ((A + 1) - (A - 1) * cosw0 + 2 * sa * alpha);
                b1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
                b2 = A * ((A + 1) - (A - 1) * cosw0 - 2 * sa * alpha);
                a0 = (A + 1) + (A - 1) * cosw0 + 2 * sa * alpha;
                a1 = -2 * ((A - 1) + (A + 1) * cosw0);
                a2 = (A + 1) + (A - 1) * cosw0 - 2 * sa * alpha;
                break;
            }
            case effects::SplineEQ::ntHighShelf: {
                const double sa = std::sqrt(A);
                b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * sa * alpha);
                b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
                b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * sa * alpha);
                a0 = (A + 1) - (A - 1) * cosw0 + 2 * sa * alpha;
                a1 = 2 * ((A - 1) - (A + 1) * cosw0);
                a2 = (A + 1) - (A - 1) * cosw0 - 2 * sa * alpha;
                break;
            }
            case effects::SplineEQ::ntNotch:
                b0 = 1; b1 = -2 * cosw0; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
                break;
            case effects::SplineEQ::ntLowCut:
                b0 = (1 + cosw0) / 2; b1 = -(1 + cosw0); b2 = (1 + cosw0) / 2;
                a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
                break;
            case effects::SplineEQ::ntHighCut:
                b0 = (1 - cosw0) / 2; b1 = 1 - cosw0; b2 = (1 - cosw0) / 2;
                a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
                break;
            default: break;
        }
        // Normalise by a0
        b0 /= a0; b1 /= a0; b2 /= a0;
        a1 /= a0; a2 /= a0;
        // |H(e^jω)| = |B(e^jω)| / |A(e^jω)|
        const double w = 2.0 * 3.14159265358979323846 *
                         static_cast<double>(f) / sr;
        const double cw = std::cos(w), sw = std::sin(w);
        const double cw2 = std::cos(2 * w), sw2 = std::sin(2 * w);
        const double bRe = b0 + b1 * cw + b2 * cw2;
        const double bIm = -(b1 * sw + b2 * sw2);
        const double aRe = 1 + a1 * cw + a2 * cw2;
        const double aIm = -(a1 * sw + a2 * sw2);
        const double bMag = std::sqrt(bRe * bRe + bIm * bIm);
        const double aMag = std::sqrt(aRe * aRe + aIm * aIm);
        return static_cast<float>(bMag / std::max(1e-9, aMag));
    }

    void renderResponseCurve(::yawn::ui::Renderer2D& r,
                              float gx, float gy, float gw, float gh) {
        if (!m_eq) return;
        const Color curveC{255, 230, 100, 240};

        // For each pixel column compute the cascaded magnitude across
        // all enabled nodes (in dB, sum), map to Y, draw 1-px line
        // segments. Skipping the analytic biquad math for disabled
        // nodes keeps the inner loop lean — at most kMaxNodes ops
        // per column.
        const int cols = static_cast<int>(gw);
        const float logLow  = std::log(kFreqLow);
        const float logHigh = std::log(kFreqHigh);

        float prevX = gx, prevY = dbToY(0.0f, gy, gh);
        for (int i = 0; i <= cols; ++i) {
            const float t  = static_cast<float>(i) / static_cast<float>(cols);
            const float hz = std::exp(logLow + t * (logHigh - logLow));
            float gainDb = 0.0f;
            for (int n = 0; n < effects::SplineEQ::kMaxNodes; ++n) {
                if (!m_eq->nodeEnabled(n)) continue;
                const float lin = nodeMagAt(
                    m_eq->nodeType(n),
                    m_eq->nodeFreqHz(n),
                    m_eq->nodeGainDb(n),
                    m_eq->nodeQ(n),
                    hz, m_sampleRate);
                gainDb += 20.0f * std::log10(std::max(lin, 1e-6f));
            }
            const float x = gx + t * gw;
            const float y = dbToY(gainDb, gy, gh);
            // Thin two-pixel-wide line by drawing slightly above and
            // below — cheaper than computing perpendicular offsets,
            // visually equivalent at the curve's slope angles.
            if (i > 0) {
                const float x0 = std::min(prevX, x);
                const float x1 = std::max(prevX, x);
                const float ymin = std::min(prevY, y);
                const float ymax = std::max(prevY, y);
                r.drawRect(x0, ymin - 0.5f,
                            std::max(1.0f, x1 - x0 + 1.0f),
                            std::max(1.0f, ymax - ymin + 1.0f),
                            curveC);
            }
            prevX = x;
            prevY = y;
        }
    }

    // Per-node info readout — small floating panel showing freq /
    // gain / Q for the currently hovered (or dragged) node. Sits
    // above the node at a small offset; dodges the panel edges so
    // it never clips off-screen.
    void renderHoverInfo(::yawn::ui::Renderer2D& r, UIContext& ctx,
                          float gx, float gy, float gw, float gh) {
        if (!m_eq || m_hoverNode < 0) return;
        if (!m_eq->nodeEnabled(m_hoverNode)) return;
        auto* tm = ctx.textMetrics;
        if (!tm) return;

        const float fhz = m_eq->nodeFreqHz(m_hoverNode);
        const float gdb = m_eq->nodeGainDb(m_hoverNode);
        const float q   = m_eq->nodeQ(m_hoverNode);

        char buf[64];
        if (fhz >= 1000.0f) std::snprintf(buf, sizeof(buf),
            "%d  %.2fkHz  %+.1fdB  Q%.2f", m_hoverNode + 1,
            fhz / 1000.0f, gdb, q);
        else                 std::snprintf(buf, sizeof(buf),
            "%d  %.0fHz  %+.1fdB  Q%.2f", m_hoverNode + 1,
            fhz, gdb, q);

        const float fs = 9.0f * (48.0f / 26.0f);   // ≈ 16.6 px
        const float pad = 4.0f;
        const float tw  = tm->textWidth(buf, fs);
        const float boxW = tw + pad * 2;
        const float boxH = fs + pad;

        // Anchor above the node, fall back to below if too close to
        // the top edge. Keep within the grid horizontally.
        const float nx = freqToX(fhz, gx, gw);
        const float ny = dbToY(gdb,   gy, gh);
        float boxX = std::clamp(nx - boxW * 0.5f,
                                gx + 2.0f, gx + gw - boxW - 2.0f);
        float boxY = ny - boxH - 12.0f;
        if (boxY < gy + 2.0f) boxY = ny + 14.0f;     // flip below if no room above

        r.drawRect(boxX, boxY, boxW, boxH, Color{12, 12, 18, 230});
        r.drawRectOutline(boxX, boxY, boxW, boxH, Color{180, 180, 200, 220});
        tm->drawText(r, buf, boxX + pad, boxY + pad * 0.5f, fs,
                      Color{230, 230, 240, 255});
    }

    void renderNodes(::yawn::ui::Renderer2D& r, UIContext& ctx,
                     float gx, float gy, float gw, float gh) {
        if (!m_eq) return;
        // Node colour by type — same accents the per-node knobs would
        // get if we coloured them, so the panel's nodes are
        // recognisable at a glance.
        static const Color kNodeColors[6] = {
            {255, 220,  80, 255},   // Peak     — yellow
            {120, 200, 255, 255},   // LowShelf — light blue
            {255, 160, 120, 255},   // HighShelf— light orange
            {200, 120, 255, 255},   // Notch    — purple
            {120, 255, 160, 255},   // LowCut   — green
            {255, 120, 160, 255},   // HighCut  — pink
        };
        for (int n = 0; n < effects::SplineEQ::kMaxNodes; ++n) {
            if (!m_eq->nodeEnabled(n)) continue;
            const float fhz = m_eq->nodeFreqHz(n);
            const float gdb = m_eq->nodeGainDb(n);
            const int   t   = static_cast<int>(m_eq->nodeType(n));
            const Color c   = kNodeColors[std::clamp(t, 0, 5)];

            const float x = freqToX(fhz, gx, gw);
            const float y = dbToY(gdb, gy, gh);

            // Node disc: filled inside, outline ring, optional digit
            // label so the user can correlate panel-side nodes with
            // the regular knob list (which is indexed 1..8).
            const float radius = 6.0f;
            r.drawRect(x - radius, y - radius, radius * 2, radius * 2,
                        Color{c.r, c.g, c.b,
                              static_cast<uint8_t>(c.a * 0.5f)});
            r.drawRectOutline(x - radius, y - radius, radius * 2, radius * 2,
                              c);
            if (auto* tm = ctx.textMetrics) {
                char buf[4]; std::snprintf(buf, sizeof(buf), "%d", n + 1);
                const float fs = 8.0f * (48.0f / 26.0f);   // ≈ 14.8 px
                tm->drawText(r, buf, x - 3, y - 7, fs,
                              Color{20, 20, 28, 255});
            }
        }
    }
#endif

    // ── Helpers shared across mouse + render ────────────────────────
    Rect gridRect() const {
        const float pad = 4.0f;
        return Rect{ m_bounds.x + pad, m_bounds.y + pad,
                    m_bounds.w - pad * 2.0f, m_bounds.h - pad * 2.0f };
    }

    int hitTestNode(float x, float y, const Rect& r) const {
        if (!m_eq) return -1;
        // Generous 9 px hit radius — slightly bigger than the visual
        // 6 px node disc so the user doesn't have to be pixel-precise.
        constexpr float kHitR = 9.0f;
        for (int n = 0; n < effects::SplineEQ::kMaxNodes; ++n) {
            if (!m_eq->nodeEnabled(n)) continue;
            const float nx = freqToX(m_eq->nodeFreqHz(n), r.x, r.w);
            const float ny = dbToY(m_eq->nodeGainDb(n),   r.y, r.h);
            const float dx = x - nx;
            const float dy = y - ny;
            if (dx * dx + dy * dy <= kHitR * kHitR) return n;
        }
        return -1;
    }

    static float xToFreq(float x, const Rect& r) {
        const float t = std::clamp((x - r.x) / r.w, 0.0f, 1.0f);
        return 20.0f * std::pow(1000.0f, t);
    }
    static float yToGainDb(float y, const Rect& r) {
        const float t = std::clamp(1.0f - (y - r.y) / r.h, 0.0f, 1.0f);
        return (t * 2.0f - 1.0f) * 18.0f;
    }

    void emitParam(int node, effects::SplineEQ::ParamField field,
                    float value) {
        if (!m_onParamChange) return;
        m_onParamChange(effects::SplineEQ::nodeParam(node, field), value);
    }

    effects::SplineEQ* m_eq = nullptr;
    double m_sampleRate = 48000.0;

    std::function<void(int, float)> m_onParamChange;

    // Hover state — drives the per-node info readout (freq / gain /
    // Q). -1 when not over any node. Locked to m_dragNodeIdx during
    // a drag so the readout doesn't flicker as the cursor sweeps
    // past other nodes mid-drag.
    int   m_hoverNode = -1;

    // Drag state. -1 when not dragging. m_dragShift selects between
    // freq/gain drag (false) and Q drag (true). The "start" snapshots
    // are kept so future relative-drag modes (cumulative offset
    // rather than absolute position) are easy to add.
    int   m_dragNodeIdx = -1;
    bool  m_dragShift = false;
    float m_dragStartFreq = 0.0f;
    float m_dragStartGain = 0.0f;
    float m_dragStartQ    = 0.707f;
    float m_dragStartX    = 0.0f;
    float m_dragStartY    = 0.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
