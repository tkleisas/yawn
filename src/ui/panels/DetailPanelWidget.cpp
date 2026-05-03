// DetailPanelWidget.cpp — rendering and event implementations.
// Split from DetailPanelWidget.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "DetailPanelWidget.h"
#include "../Renderer.h"
#include "../Font.h"
#include "../../util/Logger.h"
#include "ui/framework/v2/V1MenuBridge.h"

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
// v1 Font is baked at 48 px. The original code computed scales as
// `pt / Theme::kFontSize (=26)` against that 48-px bake; for fw2's
// pixel-sized TextMetrics interface we materialise the equivalent
// pixel size at the call site so the on-screen output stays identical.
constexpr float kFontBakePx = 48.0f;
constexpr float kPt9Px      = 9.0f  / 26.0f * kFontBakePx;   // ~16.6
constexpr float kPt10Px     = 10.0f / 26.0f * kFontBakePx;   // ~18.5
} // anon

bool DetailPanelWidget::handleRightClick(float mx, float my) {
    if (!m_open || my < m_bounds.y + kHandleHeight) return false;
    if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

    // v1 device context menu retired — LayerStack intercepts upstream.

    for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
        auto& db = m_deviceWidgets[i]->bounds();
        if (mx < db.x || mx >= db.x + db.w) continue;
        if (my < db.y || my >= db.y + db.h) continue;

        if (!m_deviceWidgets[i]->isExpanded()) return true;

        return handleKnobRightClick(i, mx, my);
    }
    return false;
}

void DetailPanelWidget::onLayout(Rect bounds, UIContext& ctx) {
    if (!m_open) return;
    float bodyY = bounds.y + kHandleHeight;
    float bodyH = m_animatedHeight - kHandleHeight;
    if (bodyH < 0) bodyH = 0;

    if (m_viewMode == ViewMode::AudioClip && m_clipPtr) {
        // Match render's dynamic layout: title + waveform(fills space) + control strip + effects
        float overviewExtra = WaveformWidget::kOverviewH
                            + WaveformWidget::kOverviewGap;
        static constexpr float kControlStripH = 76.0f;
        static constexpr float kFxLabelH      = 18.0f;
        float fxReserve = m_deviceWidgets.empty() ? kFxLabelH
            : std::max(220.0f, bodyH * 0.35f);
        float fixedBelow = kControlStripH + kClipSectionGap + overviewExtra + fxReserve;
        float availableForWave = bodyH - kClipTitleRowH - fixedBelow;
        static constexpr float kMinWaveH = 60.0f;
        float waveH = std::max(kMinWaveH, availableForWave) + overviewExtra;
        float stripY = bodyY + kClipTitleRowH + waveH + kClipSectionGap;
        float fxSepY = stripY + 5.0f + 13.0f + 6.0f + 50.0f + 6.0f;
        float scrollY = fxSepY + kFxLabelH;
        float scrollH = std::max(0.0f, bodyY + bodyH - scrollY);
        m_scroll.measure(Constraints::loose(bounds.w, scrollH), ctx);
        m_scroll.layout(Rect{bounds.x, scrollY, bounds.w, scrollH}, ctx);
    } else {
        m_scroll.measure(Constraints::loose(bounds.w, bodyH), ctx);
        m_scroll.layout(Rect{bounds.x, bodyY, bounds.w, bodyH}, ctx);
    }
}

void DetailPanelWidget::render(UIContext& ctx) {
    if (!isVisible()) return;
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& renderer = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
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

        // Per-track latency readout (top-right of body). Always
        // shown in the device-chain view — even at 0 samples, so
        // the user can see at a glance "this track has no latency"
        // vs "the readout disappeared". Tinted brighter when > 0
        // so it's noticeable when a Limiter / NoiseGate lookahead
        // adds delay; muted otherwise.
        if (m_viewMode != ViewMode::AudioClip && !m_deviceWidgets.empty()) {
            const double sr = m_trackLatencySampleRate > 0.0
                ? m_trackLatencySampleRate : 48000.0;
            const double ms = (m_trackLatencySamples * 1000.0) / sr;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Latency: %d samples (%.1f ms)",
                          m_trackLatencySamples, ms);
            const float fs = kPt9Px;
            const float tw = ctx.textMetrics
                ? ctx.textMetrics->textWidth(buf, fs)
                : static_cast<float>(std::strlen(buf)) * fs * 0.55f;
            const float pad = 6.0f;
            const Color col = (m_trackLatencySamples > 0)
                ? Color{255, 200, 80, 220}    // amber when accumulating delay
                : ::yawn::ui::Theme::textDim; // muted when zero
            tm.drawText(renderer, buf, x + w - tw - pad, bodyY + 4, fs, col);
        }

        if (m_viewMode == ViewMode::AudioClip && m_clipPtr) {
            paintAudioClipView(renderer, tm, x, bodyY, w, bodyH, ctx);
        } else if (m_deviceWidgets.empty()) {
            tm.drawText(renderer, "No devices on track",
                        x + 20, bodyY + 20, kPt9Px, ::yawn::ui::Theme::textDim);
        } else {
            updateParamValues();
            updateVisualizerData();

            m_scroll.render(ctx);

            // Draw drag-to-reorder insertion indicator
            if (m_dragReorderActive && m_dragInsertIdx >= 0 &&
                !m_deviceWidgets.empty()) {
                float ix;
                if (m_dragInsertIdx < static_cast<int>(m_deviceWidgets.size()))
                    ix = m_deviceWidgets[m_dragInsertIdx]->bounds().x - 1.0f;
                else {
                    auto& last = m_deviceWidgets.back()->bounds();
                    ix = last.x + last.w + 1.0f;
                }
                float iy = bodyY + 2.0f;
                float ih = bodyH - 4.0f;
                renderer.drawRect(ix - 1.5f, iy, 3.0f, ih, Color{0, 200, 255, 200});
            }
        }
    }

    renderer.popClip();
}

bool DetailPanelWidget::onMouseDown(MouseEvent& e) {
    float mx = e.x, my = e.y;
    LOG_INFO("UI", "DetailPanel::onMouseDown click=(%g,%g) bounds=(%g,%g,%g,%g) animH=%g viewMode=%d devices=%zu",
             mx, my, m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
             m_animatedHeight, (int)m_viewMode, m_deviceWidgets.size());
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

    // ── Descendant dispatch ──
    //
    // GOTCHA — fw2 has a SINGLE global capture slot. Calling
    // `captureMouse()` on the panel after a descendant's
    // dispatchMouseDown overwrites the descendant's auto-capture, and
    // then `onMouseMove`'s `cap != this` guard skips the forwarding —
    // dials don't turn, buttons don't release, etc. Same trap that
    // bit InstrumentRack / GroupedKnobBody before, just one layer up.
    //
    // The wrapper (DetailPanelWrapper) takes the v1 capture so the
    // App's mouseMove dispatch keeps reaching us. The fw2 capture
    // slot belongs to whichever descendant the gesture started in.
    // Don't touch it here.

    // Forward to waveform widget in audio clip view
    if (m_viewMode == ViewMode::AudioClip) {
        const auto& wb = m_waveformWidget.bounds();
        if (mx >= wb.x && mx < wb.x + wb.w && my >= wb.y && my < wb.y + wb.h) {
            if (m_waveformWidget.dispatchMouseDown(e)) return true;
            if (Widget::capturedWidget() == &m_waveformWidget) return true;
        }
        // Automation envelope editor — fw2 widget. Capture flows
        // through fw2's slot; we just dispatch and return.
        {
            const auto& eb = m_autoEnvelopeWidget.bounds();
            if (mx >= eb.x && mx < eb.x + eb.w &&
                my >= eb.y && my < eb.y + eb.h) {
                return m_autoEnvelopeWidget.dispatchMouseDown(e);
            }
        }
        // Automation target dropdown — toggles directly on mouseDown.
        {
            const auto& b = m_autoTargetDropdown.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                if (e.button == MouseButton::Left) m_autoTargetDropdown.toggle();
                return true;
            }
        }
        // Warp mode dropdown
        {
            const auto& b = m_warpModeDropdown.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                if (e.button == MouseButton::Left) m_warpModeDropdown.toggle();
                return true;
            }
        }
        // Detect button — route through the gesture SM so release
        // visual + cancel-on-pointer-out behave correctly.
        {
            const auto& b = m_detectBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                m_detectBtn.dispatchMouseDown(e);
                return true;
            }
        }
        // Knobs — drag uses the fw2 gesture SM, which auto-captures
        // the knob on press. Don't overwrite the capture here.
        auto tryKnob = [&](FwKnob& k) -> bool {
            const auto& kb = k.bounds();
            if (mx < kb.x || mx >= kb.x + kb.w) return false;
            if (my < kb.y || my >= kb.y + kb.h) return false;
            k.dispatchMouseDown(e);
            return true;
        };
        if (tryKnob(m_gainKnob))      return true;
        if (tryKnob(m_transposeKnob)) return true;
        if (tryKnob(m_detuneKnob))    return true;
        if (tryKnob(m_bpmKnob))       return true;
        // Loop toggle
        {
            const auto& b = m_loopToggleBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                m_loopToggleBtn.dispatchMouseDown(e);
                return true;
            }
        }
        // Crop / Reverse loop region buttons
        {
            const auto& b = m_cropLoopBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                m_cropLoopBtn.dispatchMouseDown(e);
                return true;
            }
        }
        {
            const auto& b = m_reverseLoopBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                m_reverseLoopBtn.dispatchMouseDown(e);
                return true;
            }
        }
    }

    if (m_deviceWidgets.empty()) return false;

    // Scroll buttons first — SnapScrollContainer only consumes
    // `<`/`>` clicks and returns false for everything else, so device
    // clicks still reach the loop below.
    if (m_scroll.dispatchMouseDown(e)) return true;

    for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
        auto* dw = m_deviceWidgets[i];
        const auto& db = dw->bounds();
        bool inside = mx >= db.x && mx < db.x + db.w && my >= db.y && my < db.y + db.h;
        if (inside) {
            if (dw->dispatchMouseDown(e)) return true;
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
            // Build AutomationTarget for this param
            automation::AutomationTarget target;
            if (ref.type == DeviceType::Instrument)
                target = automation::AutomationTarget::instrument(m_autoTrackIndex, i);
            else if (ref.type == DeviceType::AudioFx)
                target = automation::AutomationTarget::audioEffect(m_autoTrackIndex, ref.chainIndex, i);
            else
                target = automation::AutomationTarget::midiEffect(m_autoTrackIndex, ref.chainIndex, i);

            float pMin = 0.0f, pMax = 1.0f;
            // Get param range
            if (ref.midiEffect) {
                auto& info = ref.midiEffect->parameterInfo(i);
                pMin = info.minValue; pMax = info.maxValue;
            } else if (ref.instrument) {
                auto& info = ref.instrument->parameterInfo(i);
                pMin = info.minValue; pMax = info.maxValue;
            } else if (ref.audioEffect) {
                auto& info = ref.audioEffect->parameterInfo(i);
                pMin = info.minValue; pMax = info.maxValue;
            }

            if (m_learnManager) {
                openDeviceMidiLearnMenu(mx, my, target, pMin, pMax, [this, &ref, dw, i]() {
                    float def = ref.paramDefault(i);
                    DeviceRef r = ref;
                    r.setParam(i, def);
                    dw->updateParamValue(i, def);
                });
            } else {
                // Fallback: reset to default
                float def = ref.paramDefault(i);
                ref.setParam(i, def);
                dw->updateParamValue(i, def);
            }
            return true;
        }
    }
    (void)kw; (void)kh;
    return false;
}

void DetailPanelWidget::paintAudioClipView(Renderer2D& renderer, TextMetrics& tm,
                        float x, float bodyY, float w, float bodyH,
                        UIContext& ctx) {
    if (!m_clipPtr) return;
    const auto& clip = *m_clipPtr;
    float pad = 8.0f;
    float sectionX = x + pad;
    float sectionW = w - pad * 2;

    // ── Title row: clip name (left) + info (right) ──
    float headerY = bodyY + 4.0f;
    tm.drawText(renderer, clip.name.empty() ? std::string("Audio Clip") : clip.name,
                sectionX, headerY, kPt9Px, ::yawn::ui::Theme::textPrimary);

    if (clip.buffer) {
        int64_t frames = clip.lengthInFrames();
        double seconds = static_cast<double>(frames) / m_clipSampleRate;
        char durBuf[48];
        std::snprintf(durBuf, sizeof(durBuf), "%dch  %dHz  %.2fs",
                      clip.buffer->numChannels(), m_clipSampleRate, seconds);
        float durW = tm.textWidth(durBuf, kPt9Px);
        tm.drawText(renderer, durBuf, x + w - pad - durW, headerY,
                    kPt9Px, ::yawn::ui::Theme::textDim);
    }

    // ── WaveformWidget: overview bar + scrollable/zoomable waveform ──
    float waveY = headerY + kClipTitleRowH - 4.0f;
    float overviewExtra = WaveformWidget::kOverviewH
                        + WaveformWidget::kOverviewGap;
    static constexpr float kControlStripH = 88.0f;
    static constexpr float kFxLabelH      = 18.0f;
    float fxReserve = m_deviceWidgets.empty() ? kFxLabelH
        : std::max(220.0f, bodyH * 0.35f);
    float fixedBelow = kControlStripH + kClipSectionGap + overviewExtra + fxReserve;
    float availableForWave = bodyH - (waveY - bodyY) - fixedBelow;
    static constexpr float kMinWaveH = 60.0f;
    float waveH = std::max(kMinWaveH, availableForWave) + overviewExtra;
    {
        m_waveformWidget.measure(
            Constraints::tight(sectionW, waveH), ctx);
        m_waveformWidget.layout(
            Rect{sectionX, waveY, sectionW, waveH}, ctx);
        m_waveformWidget.render(ctx);
    }

    // Overlay automation envelope on waveform area (semi-transparent)
    if (m_clipAutoLanes && m_autoSelectedLaneIdx >= 0 &&
        m_autoSelectedLaneIdx < static_cast<int>(m_clipAutoLanes->size())) {
        syncEnvelopeFromLane();
        float envY = waveY + overviewExtra;
        float envH = waveH - overviewExtra;
        m_autoEnvelopeWidget.measure(
            Constraints::tight(sectionW, envH), ctx);
        m_autoEnvelopeWidget.layout(
            Rect{sectionX, envY, sectionW, envH}, ctx);
        m_autoEnvelopeWidget.render(ctx);
    }

    // Sync widget values from clip each frame. Skip the sync on any
    // knob that's mid-drag — our writes are synchronous to the clip
    // data but the knob's own m_rawValue / m_value tracking shouldn't
    // be overwritten mid-gesture. (Also skip during edit — the buffer
    // is shown independently of m_value, so mutating m_value would
    // discard the user's typed text on commit.)
    auto syncKnob = [](FwKnob& k, float v) {
        if (!k.isDragging() && !k.isEditing()) k.setValue(v);
    };
    syncKnob(m_gainKnob,      clip.gain);
    syncKnob(m_transposeKnob, static_cast<float>(clip.transposeSemitones));
    syncKnob(m_detuneKnob,    static_cast<float>(clip.detuneCents));
    if (clip.originalBPM > 0)
        syncKnob(m_bpmKnob, static_cast<float>(clip.originalBPM));
    // Keep loop toggle's visible state in sync with the clip.
    m_loopToggleBtn.setLabel(clip.looping ? "On" : "Off");
    m_loopToggleBtn.setState(clip.looping);

    // ── Horizontal control strip ──
    float stripY = waveY + waveH + kClipSectionGap;
    renderer.drawRect(sectionX, stripY, sectionW, 1.0f, Color{50, 50, 55, 255});
    stripY += 4.0f;
    float labelH = 13.0f;
    float widgetY = stripY + labelH + 6.0f;
    float knobH = 50.0f;
    float knobW = 48.0f;
    float gap = 14.0f;
    float sectionGap = 24.0f;
    float inputH = 20.0f;
    float inputCenterY = widgetY + (knobH - inputH) * 0.5f;

    float cx = sectionX;

    // Gain knob
    tm.drawText(renderer, "Gain", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_gainKnob.layout(Rect{cx, widgetY, knobW, knobH}, ctx);
    m_gainKnob.render(ctx);
    cx += knobW + sectionGap;

    // BPM knob
    tm.drawText(renderer, "BPM", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_bpmKnob.layout(Rect{cx, widgetY, knobW, knobH}, ctx);
    m_bpmKnob.render(ctx);
    cx += knobW + sectionGap;

    // Transpose knob
    tm.drawText(renderer, "Trans", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_transposeKnob.layout(Rect{cx, widgetY, knobW, knobH}, ctx);
    m_transposeKnob.render(ctx);
    cx += knobW + sectionGap;

    // Detune knob
    tm.drawText(renderer, "Detune", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_detuneKnob.layout(Rect{cx, widgetY, knobW, knobH}, ctx);
    m_detuneKnob.render(ctx);
    cx += knobW + sectionGap;

    // Warp dropdown
    float warpW = 90.0f;
    m_warpModeDropdown.setSelectedIndex(static_cast<int>(clip.warpMode));
    tm.drawText(renderer, "Warp", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_warpModeDropdown.layout(Rect{cx, inputCenterY, warpW, inputH}, ctx);
    m_warpModeDropdown.render(ctx);
    cx += warpW + gap;

    // Detect button
    float detectW = 70.0f;
    m_detectBtn.layout(Rect{cx, inputCenterY, detectW, inputH}, ctx);
    m_detectBtn.render(ctx);
    cx += detectW + sectionGap;

    // Loop toggle
    tm.drawText(renderer, "Loop", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_loopToggleBtn.layout(Rect{cx, inputCenterY, 50.0f, inputH}, ctx);
    m_loopToggleBtn.render(ctx);
    cx += 50.0f + 6.0f;

    // Crop / Reverse — destructive ops on the loop region. Sit
    // tight against the Loop toggle so they read as related.
    m_cropLoopBtn.layout(Rect{cx, inputCenterY, 50.0f, inputH}, ctx);
    m_cropLoopBtn.render(ctx);
    cx += 50.0f + 4.0f;
    m_reverseLoopBtn.layout(Rect{cx, inputCenterY, 64.0f, inputH}, ctx);
    m_reverseLoopBtn.render(ctx);
    cx += 64.0f + sectionGap;

    // Automation target dropdown
    float autoDropW = 160.0f;
    tm.drawText(renderer, "Auto", cx, stripY, kPt10Px, ::yawn::ui::Theme::textDim);
    m_autoTargetDropdown.layout(Rect{cx, inputCenterY, autoDropW, inputH}, ctx);
    m_autoTargetDropdown.render(ctx);

    // ── Effects section ──
    float fxSepY = stripY + labelH + 2.0f + knobH + 6.0f;
    renderer.drawRect(sectionX, fxSepY, sectionW, 1.0f, Color{50, 50, 55, 255});
    float fxLabelY = fxSepY + 3.0f;
    tm.drawText(renderer, "Audio Effects", sectionX, fxLabelY, kPt10Px, ::yawn::ui::Theme::textDim);

    if (!m_deviceWidgets.empty()) {
        updateParamValues();
        updateVisualizerData();
        m_scroll.render(ctx);
    } else {
        float noFxY = fxLabelY + 14.0f;
        tm.drawText(renderer, "No effects", sectionX + 80.0f, noFxY,
                    kPt10Px, ::yawn::ui::Theme::textDim);
    }
}

void DetailPanelWidget::openDeviceMidiLearnMenu(float mx, float my,
                                                 const automation::AutomationTarget& target,
                                                 float paramMin, float paramMax,
                                                 std::function<void()> resetAction) {
    using Item = ::yawn::ui::ContextMenu::Item;
    std::vector<Item> items;

    bool hasMapping = m_learnManager && m_learnManager->findByTarget(target) != nullptr;
    bool isLearning = m_learnManager && m_learnManager->isLearning() &&
                      m_learnManager->learnTarget() == target;

    if (isLearning) {
        Item cancelItem;
        cancelItem.label = "Cancel Learn";
        cancelItem.action = [this]() {
            if (m_learnManager) m_learnManager->cancelLearn();
        };
        items.push_back(std::move(cancelItem));
    } else {
        Item learnItem;
        learnItem.label = "MIDI Learn";
        learnItem.action = [this, target, paramMin, paramMax]() {
            if (m_learnManager)
                m_learnManager->startLearn(target, paramMin, paramMax);
        };
        items.push_back(std::move(learnItem));
    }

    if (hasMapping) {
        auto* mapping = m_learnManager->findByTarget(target);
        Item removeItem;
        removeItem.label = "Remove " + mapping->label();
        removeItem.action = [this, target]() {
            if (m_learnManager) m_learnManager->removeByTarget(target);
        };
        items.push_back(std::move(removeItem));
    }

    Item sep;
    sep.separator = true;
    items.push_back(std::move(sep));

    Item resetItem;
    resetItem.label = "Reset to Default";
    resetItem.action = std::move(resetAction);
    items.push_back(std::move(resetItem));

    ContextMenu::show(
        v1ItemsToFw2(std::move(items)),
        Point{mx, my});
}

} // namespace fw2
} // namespace ui
} // namespace yawn
