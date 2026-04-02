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
        float overviewH = WaveformWidget::kOverviewH + WaveformWidget::kOverviewGap;
        float clipHeaderH = kClipTitleRowH + kClipWaveformH + overviewH
                          + kClipPropsH + kClipSectionGap * 2;
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

    // Dropdown overlay must render outside clip region
    if (m_viewMode == ViewMode::AudioClip && m_warpModeDropdown.isOpen())
        m_warpModeDropdown.paintOverlay(ctx);
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

    if (!m_open) return false;

    // Forward to waveform widget in audio clip view
    if (m_viewMode == ViewMode::AudioClip) {
        // When dropdown is open, forward ALL clicks to it (select item or close)
        if (m_warpModeDropdown.isOpen())
            return m_warpModeDropdown.onMouseDown(e);
        auto& wb = m_waveformWidget.bounds();
        if (mx >= wb.x && mx < wb.x + wb.w && my >= wb.y && my < wb.y + wb.h) {
            e.lx = mx - wb.x;
            e.ly = my - wb.y;
            if (m_waveformWidget.onMouseDown(e)) return true;
        }
        // Warp mode dropdown
        if (hitWidget(m_warpModeDropdown, mx, my))
            return m_warpModeDropdown.onMouseDown(e);
        // Detect button
        if (hitWidget(m_detectBtn, mx, my))
            return m_detectBtn.onMouseDown(e);
        // Gain knob
        if (hitWidget(m_gainKnob, mx, my))
            return m_gainKnob.onMouseDown(e);
        // Transpose input
        if (hitWidget(m_transposeInput, mx, my))
            return m_transposeInput.onMouseDown(e);
        // Detune input
        if (hitWidget(m_detuneInput, mx, my))
            return m_detuneInput.onMouseDown(e);
        // BPM input
        if (hitWidget(m_bpmInput, mx, my))
            return m_bpmInput.onMouseDown(e);
        // Loop toggle
        if (hitWidget(m_loopToggleBtn, mx, my))
            return m_loopToggleBtn.onMouseDown(e);
    }

    if (m_deviceWidgets.empty()) return false;

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

    // ── Title row: clip name (left) + info (right) ──
    float headerY = bodyY + 4.0f;
    float titleScale = 15.0f / Theme::kFontSize;
    font.drawText(renderer, clip.name.empty() ? "Audio Clip" : clip.name.c_str(),
                   sectionX, headerY, titleScale, Theme::textPrimary);

    if (clip.buffer) {
        int64_t frames = clip.lengthInFrames();
        double seconds = static_cast<double>(frames) / m_clipSampleRate;
        char durBuf[48];
        std::snprintf(durBuf, sizeof(durBuf), "%dch  %dHz  %.2fs",
                      clip.buffer->numChannels(), m_clipSampleRate, seconds);
        float durW = font.textWidth(durBuf, hScale);
        font.drawText(renderer, durBuf, x + w - pad - durW, headerY,
                      hScale, Theme::textDim);
    }

    // ── WaveformWidget: overview bar + scrollable/zoomable waveform ──
    float waveY = headerY + kClipTitleRowH - 4.0f;
    float waveH = kClipWaveformH + WaveformWidget::kOverviewH + WaveformWidget::kOverviewGap;
    m_waveformWidget.layout(Rect{sectionX, waveY, sectionW, waveH}, ctx);
    m_waveformWidget.paint(ctx);

    // Sync widget values from clip each frame
    m_gainKnob.setValue(clip.gain);
    m_transposeInput.setValue(static_cast<float>(clip.transposeSemitones));
    m_detuneInput.setValue(static_cast<float>(clip.detuneCents));
    if (clip.originalBPM > 0) m_bpmInput.setValue(static_cast<float>(clip.originalBPM));
    m_loopToggleBtn.setLabel(clip.looping ? "On" : "Off");

    // ── Horizontal control strip ──
    float stripY = waveY + waveH + kClipSectionGap;
    float labelScale = 10.0f / Theme::kFontSize;
    float labelH = 13.0f;
    float widgetY = stripY + labelH + 2.0f;
    float inputH = 20.0f;
    float knobH = 40.0f;
    float gap = 14.0f;
    float sectionGap = 24.0f;  // larger gap between control groups
    // Vertically center inputs with knob area
    float inputCenterY = widgetY + (knobH - inputH) * 0.5f;

    float cx = sectionX;

    // Gain knob
    float knobW = 44.0f;
    font.drawText(renderer, "Gain", cx, stripY, labelScale, Theme::textDim);
    m_gainKnob.layout(Rect{cx, widgetY, knobW, knobH}, ctx);
    m_gainKnob.paint(ctx);
    cx += knobW + sectionGap;

    // BPM
    float bpmW = 80.0f;
    font.drawText(renderer, "BPM", cx, stripY, labelScale, Theme::textDim);
    m_bpmInput.layout(Rect{cx, inputCenterY, bpmW, inputH}, ctx);
    m_bpmInput.paint(ctx);
    cx += bpmW + sectionGap;

    // Transpose
    float transW = 70.0f;
    font.drawText(renderer, "Transpose", cx, stripY, labelScale, Theme::textDim);
    m_transposeInput.layout(Rect{cx, inputCenterY, transW, inputH}, ctx);
    m_transposeInput.paint(ctx);
    cx += transW + gap;

    // Detune
    float detW = 70.0f;
    font.drawText(renderer, "Detune", cx, stripY, labelScale, Theme::textDim);
    m_detuneInput.layout(Rect{cx, inputCenterY, detW, inputH}, ctx);
    m_detuneInput.paint(ctx);
    cx += detW + sectionGap;

    // Warp dropdown
    float warpW = 90.0f;
    m_warpModeDropdown.setSelected(static_cast<int>(clip.warpMode));
    font.drawText(renderer, "Warp", cx, stripY, labelScale, Theme::textDim);
    m_warpModeDropdown.layout(Rect{cx, inputCenterY, warpW, inputH}, ctx);
    m_warpModeDropdown.paint(ctx);
    cx += warpW + gap;

    // Detect button
    float detectW = 60.0f;
    m_detectBtn.layout(Rect{cx, inputCenterY, detectW, inputH}, ctx);
    m_detectBtn.paint(ctx);
    cx += detectW + sectionGap;

    // Loop toggle
    font.drawText(renderer, "Loop", cx, stripY, labelScale, Theme::textDim);
    m_loopToggleBtn.layout(Rect{cx, inputCenterY, 50.0f, inputH}, ctx);
    m_loopToggleBtn.paint(ctx);

    // ── Effects separator ──
    float fxSepY = stripY + labelH + 2.0f + knobH + 6.0f;
    renderer.drawRect(sectionX, fxSepY, sectionW, 1.0f, Color{50, 50, 55, 255});

    float fxLabelY = fxSepY + 3.0f;
    font.drawText(renderer, "Audio Effects", sectionX, fxLabelY, labelScale, Theme::textDim);

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
