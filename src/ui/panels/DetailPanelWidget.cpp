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
namespace fw {

bool DetailPanelWidget::handleRightClick(float mx, float my) {
    if (!m_open || my < m_bounds.y + kHandleHeight) return false;
    if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

    // v1 device context menu retired — LayerStack intercepts upstream.

    for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
        auto& db = m_deviceWidgets[i]->bounds();
        if (mx < db.x || mx >= db.x + db.w) continue;
        if (my < db.y || my >= db.y + db.h) continue;

        auto& ref = m_deviceRefs[i];
        if (!m_deviceWidgets[i]->isExpanded()) return true;

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
        // Match paint's dynamic layout: title + waveform(fills space) + control strip + effects
        float overviewExtra = WaveformWidget::kOverviewH + WaveformWidget::kOverviewGap;
        static constexpr float kControlStripH = 76.0f;
        static constexpr float kFxLabelH      = 18.0f;
        float fxReserve = m_deviceWidgets.empty() ? kFxLabelH
            : std::max(220.0f, bodyH * 0.35f);
        float fixedBelow = kControlStripH + kClipSectionGap + overviewExtra + fxReserve;
        float availableForWave = bodyH - kClipTitleRowH - fixedBelow;
        static constexpr float kMinWaveH = 60.0f;
        float waveH = std::max(kMinWaveH, availableForWave) + overviewExtra;
        float stripY = bodyY + kClipTitleRowH + waveH + kClipSectionGap;
        float fxSepY = stripY + 5.0f + 13.0f + 6.0f + 50.0f + 6.0f; // sep+pad + labelH + gap + knobH + margin
        float scrollY = fxSepY + kFxLabelH;
        float scrollH = std::max(0.0f, bodyY + bodyH - scrollY);
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

    // v2 dropdown popups paint via LayerStack::paintLayers in App —
    // no per-panel paintOverlay call needed.

    // v1 device context menu retired — fw2::ContextMenu paints via
    // LayerStack in App's render loop.
}

bool DetailPanelWidget::onMouseDown(MouseEvent& e) {
    float mx = e.x, my = e.y;
    if (my < m_bounds.y || my > m_bounds.y + height()) return false;
    if (mx < m_bounds.x || mx > m_bounds.x + m_bounds.w) return false;

    // v1 device context menu retired — LayerStack handles open-menu
    // clicks upstream in App::pollEvents.

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
        // v2 dropdown popups are handled by LayerStack upstream; when
        // the popup is open we don't reach this point for clicks.
        auto& wb = m_waveformWidget.bounds();
        if (mx >= wb.x && mx < wb.x + wb.w && my >= wb.y && my < wb.y + wb.h) {
            e.lx = mx - wb.x;
            e.ly = my - wb.y;
            if (m_waveformWidget.onMouseDown(e)) return true;
        }
        // Automation envelope editor — fw2 widget; bridge the v1 event
        // and hook into m_v2Dragging so subsequent moves/ups flow back
        // through the gesture SM (the envelope captures fw2 mouse when
        // a point drag starts).
        {
            const auto& eb = m_autoEnvelopeWidget.bounds();
            if (mx >= eb.x && mx < eb.x + eb.w &&
                my >= eb.y && my < eb.y + eb.h) {
                auto ev = ::yawn::ui::fw2::toFw2Mouse(e, eb);
                bool consumed = m_autoEnvelopeWidget.dispatchMouseDown(ev);
                if (::yawn::ui::fw2::Widget::capturedWidget() == &m_autoEnvelopeWidget) {
                    m_v2Dragging = &m_autoEnvelopeWidget;
                    captureMouse();
                }
                return consumed;
            }
        }
        // Automation target dropdown — v2 toggles directly on mouseDown
        // (same pattern as BrowserPresetsTab; v1 App loop doesn't route
        // mouseUp to v2 widgets so the gesture state machine can't fire).
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
        // Detect button — v2 FwButton. Route through the gesture SM so
        // release visual + cancel-on-pointer-out behave correctly.
        {
            const auto& b = m_detectBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
                m_detectBtn.dispatchMouseDown(ev);
                m_v2Dragging = &m_detectBtn;
                captureMouse();
                return true;
            }
        }
        // v2 knobs — drag uses the fw2 gesture SM. Forward via the
        // v1→v2 event bridge and capture v1 mouse so subsequent moves
        // route back here. Same pattern as BrowserPanel's FaBarCountKnob.
        auto tryV2Knob = [&](::yawn::ui::fw2::FwKnob& k) -> bool {
            const auto& kb = k.bounds();
            if (mx < kb.x || mx >= kb.x + kb.w) return false;
            if (my < kb.y || my >= kb.y + kb.h) return false;
            auto ev = ::yawn::ui::fw2::toFw2Mouse(e, kb);
            k.dispatchMouseDown(ev);
            m_v2Dragging = &k;
            captureMouse();
            return true;
        };
        if (tryV2Knob(m_gainKnob))      return true;
        if (tryV2Knob(m_transposeKnob)) return true;
        if (tryV2Knob(m_detuneKnob))    return true;
        if (tryV2Knob(m_bpmKnob))       return true;
        // Loop toggle — v2 FwToggle, same dispatch pattern.
        {
            const auto& b = m_loopToggleBtn.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                auto ev = ::yawn::ui::fw2::toFw2Mouse(e, b);
                m_loopToggleBtn.dispatchMouseDown(ev);
                m_v2Dragging = &m_loopToggleBtn;
                captureMouse();
                return true;
            }
        }
    }

    if (m_deviceWidgets.empty()) return false;

    if (m_scroll.onMouseDown(e)) return true;

    for (size_t i = 0; i < m_deviceWidgets.size(); ++i) {
        auto* dw = m_deviceWidgets[i];
        auto& db = dw->bounds();
        bool inside = mx >= db.x && mx < db.x + db.w && my >= db.y && my < db.y + db.h;
        LOG_INFO("UI", "DetailPanel device[%zu] bounds=(%g,%g,%g,%g) click=(%g,%g) inside=%d",
                 i, db.x, db.y, db.w, db.h, mx, my, (int)inside);
        if (inside) {
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

void DetailPanelWidget::paintAudioClipView(Renderer2D& renderer, Font& font,
                        float x, float bodyY, float w, float bodyH,
                        float hScale, UIContext& ctx) {
    if (!m_clipPtr) return;
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
    // Waveform fills available space minus control strip and effects area
    float waveY = headerY + kClipTitleRowH - 4.0f;
    float overviewExtra = WaveformWidget::kOverviewH + WaveformWidget::kOverviewGap;
    static constexpr float kControlStripH = 88.0f;   // sep(1) + pad(4) + labelH(13) + gap(6) + knobH(50) + margin
    static constexpr float kFxLabelH      = 18.0f;   // separator + "Audio Effects" label
    // Reserve space for effects: at least 220px when effects exist, 35% of panel
    float fxReserve = m_deviceWidgets.empty() ? kFxLabelH
        : std::max(220.0f, bodyH * 0.35f);
    float fixedBelow = kControlStripH + kClipSectionGap + overviewExtra + fxReserve;
    float availableForWave = bodyH - (waveY - bodyY) - fixedBelow;
    static constexpr float kMinWaveH = 60.0f;
    float waveH = std::max(kMinWaveH, availableForWave) + overviewExtra;
    m_waveformWidget.layout(Rect{sectionX, waveY, sectionW, waveH}, ctx);
    m_waveformWidget.paint(ctx);

    // Overlay automation envelope on waveform area (semi-transparent)
    if (m_clipAutoLanes && m_autoSelectedLaneIdx >= 0 &&
        m_autoSelectedLaneIdx < static_cast<int>(m_clipAutoLanes->size())) {
        syncEnvelopeFromLane();
        // Position envelope over the main waveform area (below overview bar)
        float envY = waveY + overviewExtra;
        float envH = waveH - overviewExtra;
        // m_autoEnvelopeWidget is fw2 — layout + render through the fw2 context.
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_autoEnvelopeWidget.measure(
            ::yawn::ui::fw2::Constraints::tight(sectionW, envH), v2ctx);
        m_autoEnvelopeWidget.layout(
            ::yawn::ui::fw2::Rect{sectionX, envY, sectionW, envH}, v2ctx);
        m_autoEnvelopeWidget.render(v2ctx);
    }

    // Sync widget values from clip each frame. Skip the sync on any
    // knob that's mid-drag — our writes are synchronous to the clip
    // data but the knob's own m_rawValue / m_value tracking shouldn't
    // be overwritten mid-gesture. (Also skip during edit — the buffer
    // is shown independently of m_value, so mutating m_value would
    // discard the user's typed text on commit.)
    auto syncKnob = [](::yawn::ui::fw2::FwKnob& k, float v) {
        if (!k.isDragging() && !k.isEditing()) k.setValue(v);
    };
    syncKnob(m_gainKnob,      clip.gain);
    syncKnob(m_transposeKnob, static_cast<float>(clip.transposeSemitones));
    syncKnob(m_detuneKnob,    static_cast<float>(clip.detuneCents));
    if (clip.originalBPM > 0)
        syncKnob(m_bpmKnob, static_cast<float>(clip.originalBPM));
    // Keep loop toggle's visible state in sync with the clip. setState
    // uses the Programmatic source so this doesn't fire onChange.
    m_loopToggleBtn.setLabel(clip.looping ? "On" : "Off");
    m_loopToggleBtn.setState(clip.looping);

    // ── Horizontal control strip ──
    float stripY = waveY + waveH + kClipSectionGap;
    // Separator line above controls
    renderer.drawRect(sectionX, stripY, sectionW, 1.0f, Color{50, 50, 55, 255});
    stripY += 4.0f;
    float labelScale = 10.0f / Theme::kFontSize;
    float labelH = 13.0f;
    float widgetY = stripY + labelH + 6.0f;
    float knobH = 50.0f;
    float knobW = 48.0f;
    float gap = 14.0f;
    float sectionGap = 24.0f;
    float inputH = 20.0f;
    // Vertically center non-knob inputs with knob area
    float inputCenterY = widgetY + (knobH - inputH) * 0.5f;

    float cx = sectionX;

    // Knobs are fw2 widgets — render through the global fw2 UIContext
    // (same pattern as the v2 dropdowns below).
    auto& v2ctx = ::yawn::ui::fw2::UIContext::global();

    // Gain knob
    font.drawText(renderer, "Gain", cx, stripY, labelScale, Theme::textDim);
    m_gainKnob.layout(Rect{cx, widgetY, knobW, knobH}, v2ctx);
    m_gainKnob.render(v2ctx);
    cx += knobW + sectionGap;

    // BPM knob
    font.drawText(renderer, "BPM", cx, stripY, labelScale, Theme::textDim);
    m_bpmKnob.layout(Rect{cx, widgetY, knobW, knobH}, v2ctx);
    m_bpmKnob.render(v2ctx);
    cx += knobW + sectionGap;

    // Transpose knob
    font.drawText(renderer, "Trans", cx, stripY, labelScale, Theme::textDim);
    m_transposeKnob.layout(Rect{cx, widgetY, knobW, knobH}, v2ctx);
    m_transposeKnob.render(v2ctx);
    cx += knobW + sectionGap;

    // Detune knob
    font.drawText(renderer, "Detune", cx, stripY, labelScale, Theme::textDim);
    m_detuneKnob.layout(Rect{cx, widgetY, knobW, knobH}, v2ctx);
    m_detuneKnob.render(v2ctx);
    cx += knobW + sectionGap;

    // Warp dropdown — v2 widget. Popup paints via LayerStack.
    float warpW = 90.0f;
    m_warpModeDropdown.setSelectedIndex(static_cast<int>(clip.warpMode));
    font.drawText(renderer, "Warp", cx, stripY, labelScale, Theme::textDim);
    {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_warpModeDropdown.layout(Rect{cx, inputCenterY, warpW, inputH}, v2ctx);
        m_warpModeDropdown.render(v2ctx);
    }
    cx += warpW + gap;

    // Detect button — v2 FwButton, render via fw2 UIContext.
    float detectW = 70.0f;
    m_detectBtn.layout(Rect{cx, inputCenterY, detectW, inputH}, v2ctx);
    m_detectBtn.render(v2ctx);
    cx += detectW + sectionGap;

    // Loop toggle — v2 FwToggle, render via fw2 UIContext.
    font.drawText(renderer, "Loop", cx, stripY, labelScale, Theme::textDim);
    m_loopToggleBtn.layout(Rect{cx, inputCenterY, 50.0f, inputH}, v2ctx);
    m_loopToggleBtn.render(v2ctx);
    cx += 50.0f + sectionGap;

    // Automation target dropdown (inline in control strip) — v2 widget.
    // v2 reads viewport from UIContext::global() so setScreenHeight is
    // no longer needed.
    float autoDropW = 160.0f;
    font.drawText(renderer, "Auto", cx, stripY, labelScale, Theme::textDim);
    {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_autoTargetDropdown.layout(Rect{cx, inputCenterY, autoDropW, inputH}, v2ctx);
        m_autoTargetDropdown.render(v2ctx);
    }

    // ── Effects section ──
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

void DetailPanelWidget::openDeviceMidiLearnMenu(float mx, float my,
                                                 const automation::AutomationTarget& target,
                                                 float paramMin, float paramMax,
                                                 std::function<void()> resetAction) {
    using Item = ui::ContextMenu::Item;
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

    ::yawn::ui::fw2::ContextMenu::show(
        ::yawn::ui::fw2::v1ItemsToFw2(std::move(items)),
        Point{mx, my});
}

} // namespace fw
} // namespace ui
} // namespace yawn
