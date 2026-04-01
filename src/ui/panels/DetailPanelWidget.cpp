// DetailPanelWidget.cpp — rendering and event implementations.
// Split from DetailPanelWidget.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "DetailPanelWidget.h"
#include "../Renderer.h"
#include "../Font.h"

namespace yawn {
namespace ui {
namespace fw {

bool DetailPanelWidget::handleRightClick(float mx, float my) {
    if (!m_open || my < m_bounds.y + kHandleHeight) return false;
    if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

    for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
        auto& db = m_deviceWidgets[i]->bounds();
        if (mx < db.x || mx >= db.x + db.w) continue;
        if (my < db.y || my >= db.y + db.h) continue;

        auto& ref = m_deviceRefs[i];
        if (!m_deviceWidgets[i]->isExpanded()) return true;

        int paramCount = ref.paramCount();
        for (int p = 0; p < paramCount; ++p) {
            ref.setParam(p, ref.paramDefault(p));
            m_deviceWidgets[i]->updateParamValue(p, ref.paramDefault(p));
        }
        return handleKnobRightClick(i, mx, my);
    }
    return false;
}

void DetailPanelWidget::layout(const Rect& bounds, const UIContext& ctx) {
    m_bounds = bounds;
    if (!m_open) return;
    float bodyY = bounds.y + kHandleHeight;
    float bodyH = m_animatedHeight - kHandleHeight;
    if (bodyH < 0) bodyH = 0;

    if (m_viewMode == ViewMode::AudioClip && m_clipPtr) {
        float clipHeaderH = kClipWaveformH + kClipPropsH + kClipSectionGap * 2;
        float scrollY = bodyY + clipHeaderH;
        float scrollH = std::max(0.0f, bodyH - clipHeaderH);
        m_scroll.measure(Constraints::loose(bounds.w, scrollH), ctx);
        m_scroll.layout(Rect{bounds.x, scrollY, bounds.w, scrollH}, ctx);
    } else {
        m_scroll.measure(Constraints::loose(bounds.w, bodyH), ctx);
        m_scroll.layout(Rect{bounds.x, bodyY, bounds.w, bodyH}, ctx);
    }
}

void DetailPanelWidget::paint(UIContext& ctx) {
    auto& renderer = *ctx.renderer;
    auto& font = *ctx.font;
    float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w;

    renderer.pushClip(x, y, w, m_animatedHeight);

    renderer.drawRect(x, y, w, kHandleHeight, Color{55, 55, 60, 255});
    {
        float cx = x + w * 0.5f;
        float cy = y + kHandleHeight * 0.5f;
        Color dotCol{90, 90, 95, 255};
        float dotR = 1.0f;
        float spacing = 6.0f;
        renderer.drawRect(cx - spacing - dotR, cy - dotR, dotR * 2, dotR * 2, dotCol);
        renderer.drawRect(cx - dotR,            cy - dotR, dotR * 2, dotR * 2, dotCol);
        renderer.drawRect(cx + spacing - dotR, cy - dotR, dotR * 2, dotR * 2, dotCol);
    }

    if (m_animatedHeight > kCollapsedHeight + 1.0f) {
        float bodyY = y + kHandleHeight;
        float bodyH = m_animatedHeight - kHandleHeight;
        renderer.drawRect(x, bodyY, w, bodyH, Color{28, 28, 32, 255});

        float hScale = 14.0f / Theme::kFontSize;

        if (m_viewMode == ViewMode::AudioClip && m_clipPtr) {
            paintAudioClipView(renderer, font, x, bodyY, w, bodyH, hScale, ctx);
        } else if (m_deviceWidgets.empty()) {
            font.drawText(renderer, "No devices on track",
                          x + 20, bodyY + 20, hScale, Theme::textDim);
        } else {
            updateParamValues();
            updateVisualizerData();
            m_scroll.paint(ctx);
        }
    }

    renderer.popClip();
}

bool DetailPanelWidget::onMouseDown(MouseEvent& e) {
    float mx = e.x, my = e.y;
    if (my < m_bounds.y || my > m_bounds.y + height()) return false;
    if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

    m_panelFocused = true;

    if (my < m_bounds.y + kHandleHeight) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastHandleClickTime).count();
        if (elapsed < 300) {
            toggle();
            m_lastHandleClickTime = {};
            return true;
        }
        m_lastHandleClickTime = now;
        m_handleDragActive = true;
        m_handleDragStartY = my;
        m_handleDragStartH = m_userPanelHeight;
        captureMouse();
        return true;
    }

    if (!m_open || m_deviceWidgets.empty()) return false;

    if (m_scroll.onMouseDown(e)) return true;

    for (auto* dw : m_deviceWidgets) {
        auto& db = dw->bounds();
        if (mx >= db.x && mx < db.x + db.w && my >= db.y && my < db.y + db.h) {
            if (dw->onMouseDown(e)) return true;
        }
    }
    return true;
}

bool DetailPanelWidget::handleKnobRightClick(size_t deviceIdx, float mx, float my) {
    auto& ref = m_deviceRefs[deviceIdx];
    auto* dw = m_deviceWidgets[deviceIdx];
    auto& db = dw->bounds();

    int paramCount = ref.paramCount();
    if (paramCount == 0) return true;

    float bodyY = db.y + kDeviceHeaderH;
    float bodyH = db.h - kDeviceHeaderH;
    int maxRows = kMaxKnobRows;
    float kx = db.x + 8, ky = bodyY + 4, kh = bodyH - 8;
    float kw = db.w - 16;

    if (ref.isVisualizer()) {
        float knobRowH = 68.0f;
        float vizH = bodyH - knobRowH;
        maxRows = 1;
        ky = bodyY + vizH + 2;
        kh = knobRowH - 4;
    }

    float cellW = kKnobSize + kKnobSpacing;
    float cellH = kKnobSize + 22.0f;
    int cols = static_cast<int>(std::ceil(
        static_cast<float>(paramCount) / static_cast<float>(maxRows)));

    for (int i = 0; i < paramCount; ++i) {
        int row = i / cols;
        int col = i % cols;
        float px = kx + col * cellW;
        float py = ky + row * cellH;

        if (mx >= px && mx < px + kKnobSize && my >= py && my < py + cellH) {
            float def = ref.paramDefault(i);
            ref.setParam(i, def);
            dw->updateParamValue(i, def);
            return true;
        }
    }
    (void)kw; (void)kh;
    return false;
}

void DetailPanelWidget::paintAudioClipView(Renderer2D& renderer, Font& font,
                        float x, float bodyY, float w, float bodyH,
                        float hScale, UIContext& ctx) {
    const auto& clip = *m_clipPtr;
    float pad = 8.0f;
    float sectionX = x + pad;
    float sectionW = w - pad * 2;

    float headerY = bodyY + 4.0f;
    float titleScale = 15.0f / Theme::kFontSize;
    font.drawText(renderer, clip.name.empty() ? "Audio Clip" : clip.name.c_str(),
                   sectionX, headerY, titleScale, Theme::textPrimary);

    if (clip.buffer) {
        int64_t frames = clip.lengthInFrames();
        double seconds = static_cast<double>(frames) / m_clipSampleRate;
        char durBuf[32];
        std::snprintf(durBuf, sizeof(durBuf), "%.2fs  %lld smp",
                      seconds, static_cast<long long>(frames));
        float durW = static_cast<float>(std::strlen(durBuf)) * 7.0f * hScale;
        font.drawText(renderer, durBuf, x + w - pad - durW, headerY,
                      hScale, Theme::textDim);
    }

    float waveY = headerY + 20.0f;
    float waveH = kClipWaveformH;
    float waveW = sectionW;

    renderer.drawRect(sectionX, waveY, waveW, waveH, Color{20, 20, 24, 255});
    renderer.drawRectOutline(sectionX, waveY, waveW, waveH, Color{50, 50, 55, 255});
    renderer.drawRect(sectionX, waveY + waveH * 0.5f - 0.5f, waveW, 1.0f,
                      Color{50, 50, 55, 255});

    if (clip.buffer && clip.buffer->numFrames() > 0) {
        const float* samples = clip.buffer->channelData(0);
        int sampleCount = clip.buffer->numFrames();
        int64_t start = clip.loopStart;
        int64_t end = clip.effectiveLoopEnd();
        if (start < 0) start = 0;
        if (end > sampleCount) end = sampleCount;
        int visibleSamples = static_cast<int>(end - start);
        if (visibleSamples > 0) {
            renderer.drawWaveform(samples + start, visibleSamples,
                                  sectionX + 1, waveY + 1,
                                  waveW - 2, waveH - 2,
                                  Color{100, 180, 255, 200});
        }

        if (clip.loopStart > 0) {
            float frac = static_cast<float>(clip.loopStart) / sampleCount;
            float lx = sectionX + 1 + frac * (waveW - 2);
            renderer.drawRect(lx, waveY, 1.0f, waveH, Color{255, 200, 0, 160});
        }
        if (clip.loopEnd > 0 && clip.loopEnd < clip.buffer->numFrames()) {
            float frac = static_cast<float>(clip.loopEnd) / sampleCount;
            float lx = sectionX + 1 + frac * (waveW - 2);
            renderer.drawRect(lx, waveY, 1.0f, waveH, Color{255, 200, 0, 160});
        }
    }

    float propsY = waveY + waveH + kClipSectionGap;
    float propH = 16.0f;
    float propGap = 2.0f;
    float col1X = sectionX;
    float col2X = sectionX + sectionW * 0.33f;
    float col3X = sectionX + sectionW * 0.66f;
    float labelScale = 11.0f / Theme::kFontSize;
    float valScale = 12.0f / Theme::kFontSize;
    Color labelCol = Theme::textDim;
    Color valCol = Theme::textPrimary;

    font.drawText(renderer, "Gain", col1X, propsY, labelScale, labelCol);
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f dB",
                      20.0f * std::log10(std::max(clip.gain, 0.0001f)));
        font.drawText(renderer, buf, col1X, propsY + propH, valScale, valCol);
    }

    font.drawText(renderer, "BPM", col2X, propsY, labelScale, labelCol);
    {
        char buf[16];
        if (clip.originalBPM > 0)
            std::snprintf(buf, sizeof(buf), "%.1f", clip.originalBPM);
        else
            std::snprintf(buf, sizeof(buf), "---");
        font.drawText(renderer, buf, col2X, propsY + propH, valScale, valCol);
    }

    font.drawText(renderer, "Warp", col3X, propsY, labelScale, labelCol);
    {
        const char* warpNames[] = {"Off", "Auto", "Beats", "Tones", "Texture", "Repitch"};
        int idx = static_cast<int>(clip.warpMode);
        if (idx < 0 || idx > 5) idx = 0;
        font.drawText(renderer, warpNames[idx], col3X, propsY + propH, valScale, valCol);
    }

    float row2Y = propsY + propH * 2 + propGap;
    font.drawText(renderer, "Loop", col1X, row2Y, labelScale, labelCol);
    font.drawText(renderer, clip.looping ? "On" : "Off",
                   col1X, row2Y + propH, valScale,
                   clip.looping ? Color{100, 255, 100, 255} : valCol);

    if (clip.buffer) {
        font.drawText(renderer, "Channels", col2X, row2Y, labelScale, labelCol);
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", clip.buffer->numChannels());
            font.drawText(renderer, buf, col2X, row2Y + propH, valScale, valCol);
        }

        font.drawText(renderer, "Rate", col3X, row2Y, labelScale, labelCol);
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d Hz", m_clipSampleRate);
            font.drawText(renderer, buf, col3X, row2Y + propH, valScale, valCol);
        }
    }

    float fxSepY = propsY + kClipPropsH;
    renderer.drawRect(sectionX, fxSepY, sectionW, 1.0f, Color{50, 50, 55, 255});

    float fxLabelY = fxSepY + 3.0f;
    font.drawText(renderer, "Audio Effects", sectionX, fxLabelY, labelScale, labelCol);

    if (!m_deviceWidgets.empty()) {
        updateParamValues();
        updateVisualizerData();
        m_scroll.paint(ctx);
    } else {
        float noFxY = fxLabelY + 14.0f;
        font.drawText(renderer, "No effects", sectionX + 80.0f, noFxY,
                      labelScale, Theme::textDim);
    }
}

} // namespace fw
} // namespace ui
} // namespace yawn
