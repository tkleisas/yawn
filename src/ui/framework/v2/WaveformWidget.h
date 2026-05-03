#pragma once
// fw2::WaveformWidget — scrollable/zoomable audio-clip waveform.
//
// Migrated from v1 fw::WaveformWidget. Self-contained paint + interaction
// logic: overview bar, main waveform, beat grid, warp markers (drag /
// dbl-click to add / Alt+click to delete), loop-region handles, playhead,
// zoom buttons. Capture pattern matches the v1 widget (captureMouse on
// any drag-start marker, releaseMouse on mouseUp). fw2 TextMetrics takes
// pixel sizes directly; the labels use ~14 / ~18 px (v1 native was
// ~16.6 / ~20.3 per the font-scale pitfall — dialed a couple px smaller
// to match DeviceHeaderWidget's post-iteration look).

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "audio/Clip.h"
#include "ui/Theme.h"
#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class WaveformWidget : public Widget {
public:
    // Layout constants
    static constexpr float kOverviewH    = 18.0f;
    static constexpr float kOverviewGap  = 2.0f;
    static constexpr float kMinZoom      = 1.0f;
    static constexpr float kMaxZoom      = 100000.0f;
    static constexpr float kScrollSpeed  = 40.0f;
    static constexpr float kZoomFactor   = 1.15f;

    // Label font sizes in actual pixels. v1 wrote pt/Theme::kFontSize(=26)
    // as scale against the 48-px bake → actual ~16.6 / ~20.3 px. Dialed
    // down ~2 px to match the DeviceHeader post-iteration appearance.
    static constexpr float kFsLabel = 14.0f;
    static constexpr float kFsButton = 18.0f;

    WaveformWidget() { setName("WaveformWidget"); }

    void setClip(const audio::Clip* clip) {
        if (m_clip == clip) return;
        m_clip = clip;
        m_needsFitToWidth = true;
        resetView();
    }

    const audio::Clip* clip() const { return m_clip; }

    void setPlayPosition(int64_t pos) { m_playPosition = pos; }
    void setPlaying(bool playing) {
        m_playing = playing;
        if (playing) m_followPlayhead = true;
    }

    void resetView() {
        m_samplesPerPixel = 100.0;
        m_scrollOffset    = 0.0;
    }

    void setSampleRate(int sr)        { m_sampleRate = sr; }
    void setTransportBPM(double bpm)  { m_transportBPM = bpm; }
    bool snapToGrid() const           { return m_snapToGrid; }
    void setSnapToGrid(bool snap)     { m_snapToGrid = snap; }
    void toggleSnapToGrid()           { m_snapToGrid = !m_snapToGrid; }

    // ─── fw2 Widget overrides ────────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        if (m_needsFitToWidth && m_bounds.w > 1.0f) {
            m_needsFitToWidth = false;
            fitToWidth();
        }
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        const float x = m_bounds.x, y = m_bounds.y;
        const float w = m_bounds.w, h = m_bounds.h;

        const float ovY = y;
        paintOverviewBar(r, x, ovY, w, kOverviewH);

        const float waveY = ovY + kOverviewH + kOverviewGap;
        const float waveH = h - kOverviewH - kOverviewGap;
        if (waveH < 4.0f) return;

        r.drawRect(x, waveY, w, waveH, Color{20, 20, 24, 255});
        r.drawRectOutline(x, waveY, w, waveH, Color{50, 50, 55, 255});

        if (!m_clip || !m_clip->buffer || m_clip->buffer->numFrames() <= 0) return;

        // Auto-follow playhead while playing
        if (m_playing && m_followPlayhead) {
            const double totalVisible = w * m_samplesPerPixel;
            const double totalSamples = static_cast<double>(m_clip->buffer->numFrames());
            if (totalVisible < totalSamples * 0.99) {
                const double playPx = (static_cast<double>(m_playPosition) - m_scrollOffset)
                                    / m_samplesPerPixel;
                if (playPx < 0 || playPx > w * 0.85)
                    m_scrollOffset = static_cast<double>(m_playPosition)
                                    - w * 0.15 * m_samplesPerPixel;
                clampScroll(w);
            }
        }

        r.pushClip(x, waveY, w, waveH);
        paintBeatGrid(r, ctx, x, waveY, w, waveH);
        paintWaveform(r, x, waveY, w, waveH);
        paintLoopMarkers(r, x, waveY, w, waveH);
        paintTransients(r, x, waveY, w, waveH);
        paintWarpMarkers(r, ctx, x, waveY, w, waveH);
        paintPlayhead(r, x, waveY, w, waveH);
        r.drawRect(x, waveY + waveH * 0.5f - 0.25f, w, 0.5f,
                   Color{50, 50, 55, 128});
        r.popClip();

        paintZoomButtons(r, ctx, x, waveY, w);
    }
#endif

    // ─── Events ───────────────────────────────────────────────────────

    bool onScroll(ScrollEvent& e) override {
        if (!m_clip || !m_clip->buffer) return false;

        // Self-derive local coords. The DetailPanelWidget forwards
        // raw events without re-computing e.lx/e.ly, so the values
        // arriving here are still in screen space. Re-deriving from
        // the (always-correct) global e.x/e.y + bounds keeps every
        // hit-test in this widget independent of caller behaviour.
        e.lx = e.x - m_bounds.x;
        e.ly = e.y - m_bounds.y;

        const float  w      = m_bounds.w;
        const double mouseX = static_cast<double>(e.lx);

        if ((e.modifiers & ModifierKey::Ctrl) != 0) {
            const double sampleAtMouse = m_scrollOffset + mouseX * m_samplesPerPixel;
            if (e.dy > 0)  m_samplesPerPixel /= kZoomFactor;
            else           m_samplesPerPixel *= kZoomFactor;
            m_samplesPerPixel = std::clamp(m_samplesPerPixel,
                                            static_cast<double>(kMinZoom),
                                            static_cast<double>(kMaxZoom));
            m_scrollOffset = sampleAtMouse - mouseX * m_samplesPerPixel;
        } else {
            const double scrollSamples = kScrollSpeed * m_samplesPerPixel
                                         * (e.dy > 0 ? -1.0 : 1.0);
            m_scrollOffset += scrollSamples;
        }
        clampScroll(w);
        m_followPlayhead = false;
        return true;
    }

    bool onMouseDown(MouseEvent& e) override {
        if (!m_clip || !m_clip->buffer) return false;
        if (e.button != MouseButton::Left) return false;

        // See onScroll: self-derive local coords so loop / warp
        // marker hit-tests work even when the dispatcher (currently
        // DetailPanelWidget) hasn't translated e.lx into widget
        // space. This was the root cause of the start-loop marker
        // being un-grabbable — its local pixel was ~0 and the
        // global e.lx was always far from it.
        e.lx = e.x - m_bounds.x;
        e.ly = e.y - m_bounds.y;

        const float ovBottom = m_bounds.y + kOverviewH;
        if (e.y < ovBottom) {
            const int64_t totalSamples = m_clip->buffer->numFrames();
            const double  frac = static_cast<double>(e.lx) / m_bounds.w;
            m_scrollOffset = frac * totalSamples - (m_bounds.w * 0.5) * m_samplesPerPixel;
            clampScroll(m_bounds.w);
            m_followPlayhead  = false;
            m_draggingOverview = true;
            captureMouse();
            return true;
        }

        // Zoom / snap buttons (absolute coords)
        const float mx = e.x, my = e.y;
        if (hitRect(m_zoomInBtn,  mx, my)) { zoomIn();           return true; }
        if (hitRect(m_zoomOutBtn, mx, my)) { zoomOut();          return true; }
        if (hitRect(m_zoomFitBtn, mx, my)) { fitToWidth();       return true; }
        if (hitRect(m_snapBtn,    mx, my)) { toggleSnapToGrid(); return true; }

        // Loop marker hit
        {
            const float tolerance = 8.0f;
            const float startPx = sampleToPixel(m_clip->loopStart);
            const float endPx   = sampleToPixel(m_clip->effectiveLoopEnd());
            if (std::abs(e.lx - startPx) < tolerance) {
                m_draggingLoopMarker = 1;
                captureMouse();
                return true;
            }
            if (std::abs(e.lx - endPx) < tolerance) {
                m_draggingLoopMarker = 2;
                captureMouse();
                return true;
            }
        }

        // Warp marker hit
        if (m_clip->warpMode != audio::WarpMode::Off) {
            const int hitIdx = hitTestWarpMarker(e.lx, 6.0f);
            if (hitIdx >= 0) {
                if ((e.modifiers & ModifierKey::Alt) != 0) {
                    auto& markers = const_cast<std::vector<audio::WarpMarker>&>(m_clip->warpMarkers);
                    if (hitIdx < static_cast<int>(markers.size()))
                        markers.erase(markers.begin() + hitIdx);
                    return true;
                }
                m_draggingMarker = hitIdx;
                captureMouse();
                return true;
            }
        }

        // Double-click → create warp marker
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastClickTime).count();
        m_lastClickTime = now;

        if (elapsed < 300 && m_clip->warpMode != audio::WarpMode::Off) {
            const double samplePos = m_scrollOffset
                                   + static_cast<double>(e.lx) * m_samplesPerPixel;
            int64_t sp = static_cast<int64_t>(std::clamp(samplePos, 0.0,
                static_cast<double>(m_clip->buffer->numFrames() - 1)));
            sp = snapSampleToBeat(sp);

            double beatPos = 0.0;
            if (m_clip->originalBPM > 0.0 && m_sampleRate > 0) {
                const double samplesPerBeat = (60.0 / m_clip->originalBPM) * m_sampleRate;
                beatPos = static_cast<double>(sp) / samplesPerBeat;
            }

            audio::WarpMarker newMarker{sp, beatPos};
            auto& markers = const_cast<std::vector<audio::WarpMarker>&>(m_clip->warpMarkers);
            auto it = std::lower_bound(markers.begin(), markers.end(), newMarker,
                [](const audio::WarpMarker& a, const audio::WarpMarker& b) {
                    return a.samplePosition < b.samplePosition;
                });
            markers.insert(it, newMarker);
            return true;
        }

        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        if (m_draggingOverview || m_draggingMarker >= 0 || m_draggingLoopMarker > 0) {
            m_draggingOverview   = false;
            m_draggingMarker     = -1;
            m_draggingLoopMarker = 0;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        // Same self-derive trick — the drag math uses e.lx and the
        // dispatcher doesn't translate, so we need a stable local x.
        e.lx = e.x - m_bounds.x;
        e.ly = e.y - m_bounds.y;
        if (m_draggingOverview) {
            const int64_t totalSamples = m_clip->buffer->numFrames();
            const double  frac = static_cast<double>(e.lx) / m_bounds.w;
            m_scrollOffset = frac * totalSamples - (m_bounds.w * 0.5) * m_samplesPerPixel;
            clampScroll(m_bounds.w);
            return true;
        }
        if (m_draggingMarker >= 0 && m_clip) {
            auto& markers = const_cast<std::vector<audio::WarpMarker>&>(m_clip->warpMarkers);
            if (m_draggingMarker < static_cast<int>(markers.size())) {
                const double samplePos = m_scrollOffset
                                       + static_cast<double>(e.lx) * m_samplesPerPixel;
                int64_t sp = static_cast<int64_t>(std::clamp(samplePos, 0.0,
                    static_cast<double>(m_clip->buffer->numFrames() - 1)));
                sp = snapSampleToBeat(sp);
                if (m_draggingMarker > 0)
                    sp = std::max(sp, markers[m_draggingMarker - 1].samplePosition + 1);
                if (m_draggingMarker < static_cast<int>(markers.size()) - 1)
                    sp = std::min(sp, markers[m_draggingMarker + 1].samplePosition - 1);
                markers[m_draggingMarker].samplePosition = sp;
            }
            return true;
        }
        if (m_draggingLoopMarker > 0 && m_clip) {
            const double samplePos = m_scrollOffset
                                    + static_cast<double>(e.lx) * m_samplesPerPixel;
            int64_t sp = static_cast<int64_t>(std::clamp(samplePos, 0.0,
                static_cast<double>(m_clip->buffer->numFrames())));
            sp = snapSampleToBeat(sp);
            auto* mclip = const_cast<audio::Clip*>(m_clip);
            if (m_draggingLoopMarker == 1) {
                const int64_t maxStart = mclip->effectiveLoopEnd() - 1;
                mclip->loopStart = std::clamp(sp, int64_t(0), maxStart);
            } else {
                const int64_t minEnd = mclip->loopStart + 1;
                const int64_t maxEnd = mclip->buffer->numFrames();
                const int64_t newEnd = std::clamp(sp, minEnd, maxEnd);
                mclip->loopEnd = (newEnd >= maxEnd) ? -1 : newEnd;
            }
            return true;
        }
        return false;
    }

    // ─── Zoom + view ops ────────────────────────────────────────────

    void fitToWidth() {
        if (!m_clip || !m_clip->buffer || m_clip->buffer->numFrames() <= 0) return;
        float waveW = m_bounds.w - 4.0f;
        if (waveW < 1.0f) waveW = 1.0f;
        const int64_t total = m_clip->buffer->numFrames();
        m_samplesPerPixel = static_cast<double>(total) / waveW;
        if (m_samplesPerPixel < kMinZoom) m_samplesPerPixel = kMinZoom;
        m_scrollOffset = 0.0;
    }

    void zoomIn()  { applyZoom(1.0 / kZoomFactor, m_bounds.w * 0.5); }
    void zoomOut() { applyZoom(kZoomFactor, m_bounds.w * 0.5); }

    // ─── Public state (used by consumers / tests) ────────────────────

    const audio::Clip* m_clip = nullptr;
    double  m_samplesPerPixel = 100.0;
    double  m_scrollOffset    = 0.0;
    int64_t m_playPosition    = 0;
    bool    m_playing         = false;
    bool    m_followPlayhead  = true;
    bool    m_draggingOverview = false;
    bool    m_needsFitToWidth  = false;
    bool    m_snapToGrid       = false;
    int     m_draggingMarker     = -1;
    int     m_draggingLoopMarker = 0;
    int     m_sampleRate   = static_cast<int>(kDefaultSampleRate);
    double  m_transportBPM = 120.0;
    std::chrono::steady_clock::time_point m_lastClickTime{};

    static constexpr float kZoomBtnSize = 18.0f;
    static constexpr float kZoomBtnGap  = 2.0f;
    Rect m_zoomInBtn{}, m_zoomOutBtn{}, m_zoomFitBtn{}, m_snapBtn{};

private:
    void applyZoom(double factor, double centerPx) {
        if (!m_clip || !m_clip->buffer) return;
        const double sampleAtCenter = m_scrollOffset + centerPx * m_samplesPerPixel;
        m_samplesPerPixel *= factor;
        m_samplesPerPixel = std::clamp(m_samplesPerPixel,
                                        static_cast<double>(kMinZoom),
                                        static_cast<double>(kMaxZoom));
        m_scrollOffset = sampleAtCenter - centerPx * m_samplesPerPixel;
        clampScroll(m_bounds.w);
        m_followPlayhead = false;
    }

    void clampScroll(float widgetW) {
        if (!m_clip || !m_clip->buffer) return;
        const double maxOff = static_cast<double>(m_clip->buffer->numFrames())
                            - widgetW * m_samplesPerPixel;
        if (m_scrollOffset > maxOff) m_scrollOffset = maxOff;
        if (m_scrollOffset < 0.0)    m_scrollOffset = 0.0;
    }

    float sampleToPixel(int64_t sample) const {
        return static_cast<float>((static_cast<double>(sample) - m_scrollOffset)
                                   / m_samplesPerPixel);
    }

    int64_t snapSampleToBeat(int64_t sp) const {
        if (!m_snapToGrid || !m_clip) return sp;
        double bpm = m_clip->originalBPM;
        if (bpm <= 0.0) bpm = m_transportBPM;
        if (bpm <= 0.0 || m_sampleRate <= 0) return sp;

        const double samplesPerBeat = (60.0 / bpm) * m_sampleRate;
        const double pixelsPerBeat  = samplesPerBeat / m_samplesPerPixel;
        int subdivPerBeat = 1;
        if (pixelsPerBeat > 120.0)     subdivPerBeat = 4;
        else if (pixelsPerBeat > 50.0) subdivPerBeat = 2;

        const double samplesPerSubdiv = samplesPerBeat / subdivPerBeat;
        const double sub = std::round(static_cast<double>(sp) / samplesPerSubdiv);
        return static_cast<int64_t>(sub * samplesPerSubdiv);
    }

    int hitTestWarpMarker(float localX, float tolerance) const {
        if (!m_clip) return -1;
        for (int i = 0; i < static_cast<int>(m_clip->warpMarkers.size()); ++i) {
            const float px = sampleToPixel(m_clip->warpMarkers[i].samplePosition);
            if (std::abs(localX - px) < tolerance) return i;
        }
        return -1;
    }

    static bool hitRect(const Rect& r, float mx, float my) {
        return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
    }

#ifndef YAWN_TEST_BUILD
    // ─── Paint helpers ──────────────────────────────────────────────

    void paintOverviewBar(Renderer2D& r, float x, float y, float w, float h) {
        r.drawRect(x, y, w, h, Color{15, 15, 18, 255});
        r.drawRectOutline(x, y, w, h, Color{40, 40, 45, 255});

        if (!m_clip || !m_clip->buffer || m_clip->buffer->numFrames() <= 0) return;

        const auto& buf = *m_clip->buffer;
        const int nch = buf.numChannels();

        if (nch >= 2)
            r.drawWaveformStereo(buf.channelData(0), buf.channelData(1),
                                 buf.numFrames(), x + 1, y + 1, w - 2, h - 2,
                                 Color{60, 120, 180, 160});
        else
            r.drawWaveform(buf.channelData(0), buf.numFrames(),
                           x + 1, y + 1, w - 2, h - 2,
                           Color{60, 120, 180, 160});

        const double totalSamples = static_cast<double>(buf.numFrames());
        float vx = x + static_cast<float>(m_scrollOffset / totalSamples * w);
        float vw = static_cast<float>((m_bounds.w * m_samplesPerPixel) / totalSamples * w);
        if (vw < 4.0f) vw = 4.0f;
        if (vx + vw > x + w) vw = x + w - vx;
        r.drawRectOutline(vx, y, vw, h, Color{255, 255, 255, 140}, 1.0f);
        r.drawRect(vx, y, vw, h, Color{255, 255, 255, 20});
    }

    void paintBeatGrid(Renderer2D& r, UIContext& ctx,
                       float x, float y, float w, float h) {
        double bpm = m_clip->originalBPM;
        if (bpm <= 0.0) bpm = m_transportBPM;
        if (bpm <= 0.0 || m_sampleRate <= 0) return;

        const double samplesPerBeat = (60.0 / bpm) * m_sampleRate;
        const int    beatsPerBar = 4;

        const double startSample = m_scrollOffset;
        const double endSample   = m_scrollOffset + w * m_samplesPerPixel;

        const double pixelsPerBeat  = samplesPerBeat / m_samplesPerPixel;
        int subdivPerBeat = 1;
        if (pixelsPerBeat > 120.0)      subdivPerBeat = 4;
        else if (pixelsPerBeat > 50.0)  subdivPerBeat = 2;

        const double samplesPerSubdiv = samplesPerBeat / subdivPerBeat;
        const int    totalSubdivsPerBar = beatsPerBar * subdivPerBeat;

        int firstSubdiv = static_cast<int>(std::floor(startSample / samplesPerSubdiv));
        if (firstSubdiv < 0) firstSubdiv = 0;
        const int lastSubdiv = static_cast<int>(std::ceil(endSample / samplesPerSubdiv));

        if (lastSubdiv - firstSubdiv > 4000) return;

        auto* tm = ctx.textMetrics;

        for (int sub = firstSubdiv; sub <= lastSubdiv; ++sub) {
            const double samplePos = sub * samplesPerSubdiv;
            const float  px = x + static_cast<float>((samplePos - m_scrollOffset)
                                                      / m_samplesPerPixel);
            if (px < x || px > x + w) continue;

            const bool isBar  = (sub % totalSubdivsPerBar == 0);
            const bool isBeat = (!isBar && sub % subdivPerBeat == 0);

            if (isBar) {
                r.drawRect(px, y, 1.5f, h, Color{255, 160, 40, 200});
                const int barNum = sub / totalSubdivsPerBar + 1;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", barNum);
                if (tm) tm->drawText(r, buf, px + 3, y + 1, kFsLabel,
                                      Color{255, 190, 80, 230});
            } else if (isBeat) {
                r.drawRect(px, y, 1.0f, h, Color{255, 130, 30, 150});
            } else {
                r.drawRect(px, y, 0.5f, h, Color{255, 110, 20, 70});
            }
        }
    }

    void paintWaveform(Renderer2D& r, float x, float y, float w, float h) {
        const auto& buf = *m_clip->buffer;
        const int nch = buf.numChannels();

        int64_t startSample = static_cast<int64_t>(m_scrollOffset);
        int64_t endSample   = startSample + static_cast<int64_t>(w * m_samplesPerPixel);
        if (startSample < 0)                startSample = 0;
        if (endSample > buf.numFrames())    endSample   = buf.numFrames();

        const int visibleSamples = static_cast<int>(endSample - startSample);
        if (visibleSamples <= 0) return;

        const float drawX = x + sampleToPixel(startSample);
        const float drawW = static_cast<float>(visibleSamples / m_samplesPerPixel);

        const Color wfCol{100, 180, 255, 200};
        if (nch >= 2)
            r.drawWaveformStereo(buf.channelData(0) + startSample,
                                 buf.channelData(1) + startSample,
                                 visibleSamples, drawX, y + 1, drawW, h - 2, wfCol);
        else
            r.drawWaveform(buf.channelData(0) + startSample, visibleSamples,
                           drawX, y + 1, drawW, h - 2, wfCol);
    }

    void paintPlayhead(Renderer2D& r, float x, float y, float, float h) {
        if (!m_playing && m_playPosition <= 0) return;
        const float px = x + sampleToPixel(m_playPosition);
        r.drawRect(px, y, 1.5f, h, Color{255, 255, 255, 220});
    }

    void paintLoopMarkers(Renderer2D& r, float x, float y, float, float h) {
        if (!m_clip->looping) return;
        const Color loopCol{255, 200, 0, 200};
        const Color loopDrag{255, 255, 100, 255};

        const int64_t ls = m_clip->loopStart;
        const int64_t le = m_clip->effectiveLoopEnd();

        {
            const float lx = x + sampleToPixel(ls);
            const Color col = (m_draggingLoopMarker == 1) ? loopDrag : loopCol;
            r.drawRect(lx, y, 1.5f, h,   col);
            r.drawRect(lx, y, 8.0f, 2.0f, col);
            r.drawRect(lx, y, 2.0f, 10.0f, col);
        }

        {
            const float lx = x + sampleToPixel(le);
            const Color col = (m_draggingLoopMarker == 2) ? loopDrag : loopCol;
            r.drawRect(lx - 1.5f, y, 1.5f, h,   col);
            r.drawRect(lx - 8.0f, y, 8.0f, 2.0f, col);
            r.drawRect(lx - 2.0f, y, 2.0f, 10.0f, col);
        }

        const float startPx = x + sampleToPixel(ls);
        const float endPx   = x + sampleToPixel(le);
        if (endPx > startPx)
            r.drawRect(startPx, y, endPx - startPx, h, Color{255, 200, 0, 15});
    }

    void paintTransients(Renderer2D& r, float x, float y, float, float) {
        for (const auto tPos : m_clip->transients) {
            const float px = x + sampleToPixel(tPos);
            const float triSize = 4.0f;
            r.drawTriangle(px - triSize, y + 1,
                           px + triSize, y + 1,
                           px,           y + triSize + 1,
                           Color{255, 160, 40, 180});
        }
    }

    void paintWarpMarkers(Renderer2D& r, UIContext& ctx,
                          float x, float y, float, float h) {
        if (m_clip->warpMode == audio::WarpMode::Off) return;

        auto* tm = ctx.textMetrics;
        for (int i = 0; i < static_cast<int>(m_clip->warpMarkers.size()); ++i) {
            const auto& wm = m_clip->warpMarkers[i];
            const float  px = x + sampleToPixel(wm.samplePosition);

            const Color markerCol = (i == m_draggingMarker)
                ? Color{255, 255, 100, 255}
                : Color{255, 220, 50, 200};

            for (float dy = 0; dy < h; dy += 6.0f)
                r.drawRect(px, y + dy, 1.0f, 3.0f, markerCol);

            r.drawTriangle(px - 4, y, px + 4, y, px, y + 6, markerCol);

            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", wm.beatPosition);
            if (tm) tm->drawText(r, buf, px + 3, y + 1, kFsLabel, markerCol);
        }
    }

    void paintZoomButtons(Renderer2D& r, UIContext& ctx,
                          float x, float waveY, float w) {
        const float btnS = kZoomBtnSize, gap = kZoomBtnGap;
        const float bx = x + w - (btnS * 4 + gap * 3) - 4.0f;
        const float by = waveY + 4.0f;

        m_zoomInBtn  = {bx, by, btnS, btnS};
        m_zoomOutBtn = {bx + btnS + gap, by, btnS, btnS};
        m_zoomFitBtn = {bx + (btnS + gap) * 2, by, btnS, btnS};
        m_snapBtn    = {bx + (btnS + gap) * 3, by, btnS, btnS};

        const Color btnBg{30, 30, 34, 200};
        const Color btnBorder{70, 70, 75, 200};
        const Color btnText = ::yawn::ui::Theme::textSecondary;

        auto* tm = ctx.textMetrics;
        auto drawBtn = [&](const Rect& b, const char* label, bool active = false) {
            const Color bg     = active ? Color{255, 160, 40, 180} : btnBg;
            const Color border = active ? Color{255, 190, 80, 230} : btnBorder;
            const Color text   = active ? Color{20, 20, 24, 255}    : btnText;
            r.drawRect(b.x, b.y, b.w, b.h, bg);
            r.drawRectOutline(b.x, b.y, b.w, b.h, border);
            if (tm) {
                const float tw = tm->textWidth(label, kFsButton);
                const float lh = tm->lineHeight(kFsButton);
                tm->drawText(r, label,
                              b.x + (b.w - tw) * 0.5f,
                              b.y + (b.h - lh) * 0.5f - lh * 0.15f,
                              kFsButton, text);
            }
        };

        drawBtn(m_zoomInBtn,  "+");
        drawBtn(m_zoomOutBtn, "-");
        drawBtn(m_zoomFitBtn, "F");
        drawBtn(m_snapBtn,    "S", m_snapToGrid);
    }
#endif
};

} // namespace fw2
} // namespace ui
} // namespace yawn
