#pragma once
// WaveformWidget — Interactive scrollable/zoomable waveform display.
//
// Features:
//   - Mono or stereo rendering (auto-detects from clip channel count)
//   - Horizontal scroll (mouse wheel) and zoom (Ctrl+wheel)
//   - Mini overview bar with viewport indicator
//   - Playhead tracking with auto-scroll during playback
//   - Transient marker display
//   - Warp marker display, creation (dbl-click), drag, delete (Alt+click)
//   - Loop region overlay with draggable start/end markers

#include "Widget.h"
#include "EventSystem.h"
#include "UIContext.h"
#include "audio/Clip.h"
#include "ui/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

class WaveformWidget : public Widget {
public:
    // Layout constants
    static constexpr float kOverviewH    = 18.0f;  // mini overview bar height
    static constexpr float kOverviewGap  = 2.0f;
    static constexpr float kMinZoom      = 1.0f;    // 1 sample per pixel (max zoom in)
    static constexpr float kMaxZoom      = 100000.0f; // samples per pixel (max zoom out)
    static constexpr float kScrollSpeed  = 40.0f;   // pixels per scroll tick
    static constexpr float kZoomFactor   = 1.15f;   // zoom multiplier per scroll tick

    void setClip(const audio::Clip* clip) {
        if (m_clip == clip) return;
        m_clip = clip;
        resetView();
    }

    const audio::Clip* clip() const { return m_clip; }

    void setPlayPosition(int64_t pos) { m_playPosition = pos; }
    void setPlaying(bool playing) {
        m_playing = playing;
        if (playing) m_followPlayhead = true;
    }

    // Reset view to show entire clip
    void resetView() {
        if (!m_clip || !m_clip->buffer || m_clip->buffer->numFrames() <= 0) {
            m_samplesPerPixel = 100.0;
            m_scrollOffset = 0.0;
            return;
        }
        float waveW = m_bounds.w - 4.0f;
        if (waveW < 1.0f) waveW = 1.0f;
        int64_t total = m_clip->effectiveLoopEnd() - m_clip->loopStart;
        if (total <= 0) total = m_clip->buffer->numFrames();
        m_samplesPerPixel = static_cast<double>(total) / waveW;
        if (m_samplesPerPixel < kMinZoom) m_samplesPerPixel = kMinZoom;
        m_scrollOffset = static_cast<double>(m_clip->loopStart);
    }

    // ─── Rendering ──────────────────────────────────────────────────────

#ifndef YAWN_TEST_BUILD
    void paint(UIContext& ctx) override {
        if (!ctx.renderer || !ctx.font) return;
        auto& r = *ctx.renderer;

        float x = m_bounds.x, y = m_bounds.y;
        float w = m_bounds.w, h = m_bounds.h;

        // Overview bar
        float ovY = y;
        paintOverviewBar(r, x, ovY, w, kOverviewH);

        // Main waveform area
        float waveY = ovY + kOverviewH + kOverviewGap;
        float waveH = h - kOverviewH - kOverviewGap;
        if (waveH < 4.0f) return;

        // Background
        r.drawRect(x, waveY, w, waveH, Color{20, 20, 24, 255});
        r.drawRectOutline(x, waveY, w, waveH, Color{50, 50, 55, 255});

        if (!m_clip || !m_clip->buffer || m_clip->buffer->numFrames() <= 0) return;

        // Auto-scroll to playhead
        if (m_playing && m_followPlayhead) {
            double playPx = (static_cast<double>(m_playPosition) - m_scrollOffset) / m_samplesPerPixel;
            if (playPx < 0 || playPx > w * 0.85)
                m_scrollOffset = static_cast<double>(m_playPosition) - w * 0.15 * m_samplesPerPixel;
        }

        r.pushClip(x, waveY, w, waveH);
        paintWaveform(r, x, waveY, w, waveH);
        paintLoopMarkers(r, x, waveY, w, waveH);
        paintTransients(r, *ctx.font, x, waveY, w, waveH);
        paintWarpMarkers(r, *ctx.font, x, waveY, w, waveH);
        paintPlayhead(r, x, waveY, w, waveH);
        // Center line
        r.drawRect(x, waveY + waveH * 0.5f - 0.25f, w, 0.5f, Color{50, 50, 55, 128});
        r.popClip();
    }
#else
    void paint(UIContext&) override {}
#endif

    // ─── Events ─────────────────────────────────────────────────────────

    bool onScroll(ScrollEvent& e) override {
        if (!m_clip || !m_clip->buffer) return false;

        float w = m_bounds.w;
        double mouseX = static_cast<double>(e.lx);

        if (e.mods.ctrl) {
            // Zoom: anchor at mouse position
            double sampleAtMouse = m_scrollOffset + mouseX * m_samplesPerPixel;
            if (e.dy > 0)
                m_samplesPerPixel /= kZoomFactor;
            else
                m_samplesPerPixel *= kZoomFactor;
            m_samplesPerPixel = std::clamp(m_samplesPerPixel, (double)kMinZoom, (double)kMaxZoom);
            m_scrollOffset = sampleAtMouse - mouseX * m_samplesPerPixel;
        } else {
            // Scroll horizontally
            double scrollSamples = kScrollSpeed * m_samplesPerPixel * (e.dy > 0 ? -1.0 : 1.0);
            m_scrollOffset += scrollSamples;
        }
        clampScroll(w);
        m_followPlayhead = false;
        return true;
    }

    bool onMouseDown(MouseEvent& e) override {
        if (!m_clip || !m_clip->buffer) return false;
        if (e.button != MouseButton::Left) return false;

        // Check if clicking in overview bar
        float ovBottom = m_bounds.y + kOverviewH;
        if (e.y < ovBottom) {
            // Click in overview → jump scroll
            int64_t totalSamples = m_clip->buffer->numFrames();
            double frac = static_cast<double>(e.lx) / m_bounds.w;
            m_scrollOffset = frac * totalSamples - (m_bounds.w * 0.5) * m_samplesPerPixel;
            clampScroll(m_bounds.w);
            m_followPlayhead = false;
            m_draggingOverview = true;
            captureMouse();
            return true;
        }

        // Check for warp marker hit
        if (m_clip->warpMode != audio::WarpMode::Off) {
            int hitIdx = hitTestWarpMarker(e.lx, 6.0f);
            if (hitIdx >= 0) {
                if (e.mods.alt) {
                    // Alt+click → delete marker
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

        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        (void)e;
        if (m_draggingOverview || m_draggingMarker >= 0) {
            m_draggingOverview = false;
            m_draggingMarker = -1;
            releaseMouse();
            return true;
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_draggingOverview) {
            int64_t totalSamples = m_clip->buffer->numFrames();
            double frac = static_cast<double>(e.lx) / m_bounds.w;
            m_scrollOffset = frac * totalSamples - (m_bounds.w * 0.5) * m_samplesPerPixel;
            clampScroll(m_bounds.w);
            return true;
        }
        if (m_draggingMarker >= 0 && m_clip) {
            auto& markers = const_cast<std::vector<audio::WarpMarker>&>(m_clip->warpMarkers);
            if (m_draggingMarker < static_cast<int>(markers.size())) {
                double samplePos = m_scrollOffset + static_cast<double>(e.lx) * m_samplesPerPixel;
                int64_t sp = static_cast<int64_t>(std::clamp(samplePos, 0.0,
                    static_cast<double>(m_clip->buffer->numFrames() - 1)));

                // Constrain between neighbors
                if (m_draggingMarker > 0)
                    sp = std::max(sp, markers[m_draggingMarker - 1].samplePosition + 1);
                if (m_draggingMarker < static_cast<int>(markers.size()) - 1)
                    sp = std::min(sp, markers[m_draggingMarker + 1].samplePosition - 1);

                markers[m_draggingMarker].samplePosition = sp;
            }
            return true;
        }
        return false;
    }

private:
    const audio::Clip* m_clip = nullptr;
    double m_samplesPerPixel = 100.0;    // zoom level
    double m_scrollOffset = 0.0;         // first visible sample
    int64_t m_playPosition = 0;
    bool m_playing = false;
    bool m_followPlayhead = true;
    bool m_draggingOverview = false;
    int m_draggingMarker = -1;           // index of warp marker being dragged

    // ─── Paint helpers ──────────────────────────────────────────────────

    void clampScroll(float widgetW) {
        if (!m_clip || !m_clip->buffer) return;
        double maxOff = static_cast<double>(m_clip->buffer->numFrames())
                      - widgetW * m_samplesPerPixel;
        if (m_scrollOffset > maxOff) m_scrollOffset = maxOff;
        if (m_scrollOffset < 0.0) m_scrollOffset = 0.0;
    }

    // Sample position → pixel x (relative to widget left)
    float sampleToPixel(int64_t sample) const {
        return static_cast<float>((static_cast<double>(sample) - m_scrollOffset)
                                   / m_samplesPerPixel);
    }

    int hitTestWarpMarker(float localX, float tolerance) const {
        if (!m_clip) return -1;
        for (int i = 0; i < static_cast<int>(m_clip->warpMarkers.size()); ++i) {
            float px = sampleToPixel(m_clip->warpMarkers[i].samplePosition);
            if (std::abs(localX - px) < tolerance) return i;
        }
        return -1;
    }

#ifndef YAWN_TEST_BUILD
    void paintOverviewBar(Renderer2D& r, float x, float y, float w, float h) {
        r.drawRect(x, y, w, h, Color{15, 15, 18, 255});
        r.drawRectOutline(x, y, w, h, Color{40, 40, 45, 255});

        if (!m_clip || !m_clip->buffer || m_clip->buffer->numFrames() <= 0) return;

        const auto& buf = *m_clip->buffer;
        int nch = buf.numChannels();

        // Draw mini waveform
        if (nch >= 2)
            r.drawWaveformStereo(buf.channelData(0), buf.channelData(1),
                                 buf.numFrames(), x + 1, y + 1, w - 2, h - 2,
                                 Color{60, 120, 180, 160});
        else
            r.drawWaveform(buf.channelData(0), buf.numFrames(),
                           x + 1, y + 1, w - 2, h - 2,
                           Color{60, 120, 180, 160});

        // Viewport indicator
        double totalSamples = static_cast<double>(buf.numFrames());
        float vx = x + static_cast<float>(m_scrollOffset / totalSamples * w);
        float vw = static_cast<float>((m_bounds.w * m_samplesPerPixel) / totalSamples * w);
        if (vw < 4.0f) vw = 4.0f;
        if (vx + vw > x + w) vw = x + w - vx;
        r.drawRectOutline(vx, y, vw, h, Color{255, 255, 255, 140}, 1.0f);
        r.drawRect(vx, y, vw, h, Color{255, 255, 255, 20});
    }

    void paintWaveform(Renderer2D& r, float x, float y, float w, float h) {
        const auto& buf = *m_clip->buffer;
        int nch = buf.numChannels();

        // Visible sample range
        int64_t startSample = static_cast<int64_t>(m_scrollOffset);
        int64_t endSample = startSample + static_cast<int64_t>(w * m_samplesPerPixel);
        if (startSample < 0) startSample = 0;
        if (endSample > buf.numFrames()) endSample = buf.numFrames();

        int visibleSamples = static_cast<int>(endSample - startSample);
        if (visibleSamples <= 0) return;

        float drawX = x + sampleToPixel(startSample);
        float drawW = static_cast<float>(visibleSamples / m_samplesPerPixel);

        Color wfCol{100, 180, 255, 200};
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
        float px = x + sampleToPixel(m_playPosition);
        r.drawRect(px, y, 1.5f, h, Color{255, 255, 255, 220});
    }

    void paintLoopMarkers(Renderer2D& r, float x, float y, float, float h) {
        int64_t totalFrames = m_clip->buffer->numFrames();
        if (m_clip->loopStart > 0) {
            float lx = x + sampleToPixel(m_clip->loopStart);
            r.drawRect(lx, y, 1.5f, h, Color{255, 200, 0, 180});
        }
        int64_t le = m_clip->effectiveLoopEnd();
        if (le > 0 && le < totalFrames) {
            float lx = x + sampleToPixel(le);
            r.drawRect(lx, y, 1.5f, h, Color{255, 200, 0, 180});
        }
    }

    void paintTransients(Renderer2D& r, Font&, float x, float y, float, float) {
        for (auto tPos : m_clip->transients) {
            float px = x + sampleToPixel(tPos);
            // Small triangle tick at the top
            float triSize = 4.0f;
            r.drawTriangle(px - triSize, y + 1,
                           px + triSize, y + 1,
                           px, y + triSize + 1,
                           Color{255, 160, 40, 180});
        }
    }

    void paintWarpMarkers(Renderer2D& r, Font& font, float x, float y, float, float h) {
        if (m_clip->warpMode == audio::WarpMode::Off) return;

        float labelScale = 9.0f / Theme::kFontSize;
        for (int i = 0; i < static_cast<int>(m_clip->warpMarkers.size()); ++i) {
            const auto& wm = m_clip->warpMarkers[i];
            float px = x + sampleToPixel(wm.samplePosition);

            Color markerCol = (i == m_draggingMarker)
                ? Color{255, 255, 100, 255}
                : Color{255, 220, 50, 200};

            // Dashed vertical line
            for (float dy = 0; dy < h; dy += 6.0f)
                r.drawRect(px, y + dy, 1.0f, 3.0f, markerCol);

            // Handle triangle at top
            r.drawTriangle(px - 4, y, px + 4, y, px, y + 6, markerCol);

            // Beat label
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", wm.beatPosition);
            font.drawText(r, buf, px + 3, y + 1, labelScale, markerCol);
        }
    }
#endif
};

} // namespace fw
} // namespace ui
} // namespace yawn
