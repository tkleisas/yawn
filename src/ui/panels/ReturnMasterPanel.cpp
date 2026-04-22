// ReturnMasterPanel.cpp — UI v2 rendering + event implementations.

#include "ReturnMasterPanel.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/V1MenuBridge.h"
#include "ui/framework/v2/Theme.h"

namespace yawn {
namespace ui {
namespace fw2 {

void ReturnMasterPanel::render(UIContext& ctx) {
    if (!m_visible) return;
    if (!ctx.renderer) return;
    if (!m_engine) return;
    auto& r = *ctx.renderer;

    const float x = m_bounds.x, y = m_bounds.y;
    const float w = m_bounds.w, h = m_bounds.h;

    r.drawRect(x, y, w, h, ::yawn::ui::Theme::panelBg);
    r.drawRect(x, y, w, 1, ::yawn::ui::Theme::clipSlotBorder);

    const float stripY = y + 2;
    const float stripH = h - 4;

    float curX = x + 4;
    if (m_showReturns) {
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            if (curX + kRetStripW > x + w) break;
            paintReturnStrip(ctx, b, curX, stripY, kRetStripW, stripH);
            curX += kRetStripW + kStripPadding;
        }
        r.drawRect(curX, y + 4, kSeparatorWidth, h - 8, ::yawn::ui::Theme::clipSlotBorder);
        curX += kSeparatorWidth + 4;
    }

    if (curX + kRetStripW <= x + w)
        paintMasterStrip(ctx, curX, stripY, kRetStripW, stripH);
}

bool ReturnMasterPanel::onMouseDown(MouseEvent& e) {
    if (!m_engine) return false;
    const float mx = e.x, my = e.y;
    const bool rightClick = (e.button == MouseButton::Right);
    const float x = m_bounds.x;

    auto hitChild = [&](Widget& w) -> bool {
        const auto& b = w.bounds();
        if (mx < b.x || mx >= b.x + b.w) return false;
        if (my < b.y || my >= b.y + b.h) return false;
        MouseEvent ev = e;
        ev.lx = mx - b.x;
        ev.ly = my - b.y;
        w.dispatchMouseDown(ev);
        return true;
    };

    float curX = x + 4;
    if (m_showReturns) {
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            const float rx = curX + b * (kRetStripW + kStripPadding);
            if (mx < rx || mx >= rx + kRetStripW) continue;

            auto& rs = m_returnStrips[b];

            if (!rightClick) {
                if (hitChild(rs.muteBtn)) return true;
            }

            // Pan — right-click opens MIDI Learn menu, left-click drags.
            {
                const auto& pb = rs.pan.bounds();
                if (mx >= pb.x && mx < pb.x + pb.w &&
                    my >= pb.y && my < pb.y + pb.h) {
                    if (rightClick) {
                        openMidiLearnMenu(mx, my,
                            automation::AutomationTarget::mixer(-(b + 2), automation::MixerParam::Pan),
                            -1.0f, 1.0f,
                            [this, b]() {
                                m_returnStrips[b].pan.setValue(0.0f);
                                m_engine->sendCommand(audio::SetReturnPanMsg{b, 0.0f});
                            });
                        return true;
                    }
                    return hitChild(rs.pan);
                }
            }
            // Fader
            {
                const auto& fb = rs.fader.bounds();
                if (mx >= fb.x && mx < fb.x + fb.w &&
                    my >= fb.y && my < fb.y + fb.h) {
                    if (rightClick) {
                        openMidiLearnMenu(mx, my,
                            automation::AutomationTarget::mixer(-(b + 2), automation::MixerParam::Volume),
                            0.0f, 2.0f,
                            [this, b]() {
                                m_engine->sendCommand(audio::SetReturnVolumeMsg{b, 1.0f});
                            });
                        return true;
                    }
                    return hitChild(rs.fader);
                }
            }

            if (rightClick && m_onReturnRightClick) {
                m_onReturnRightClick(b, mx, my);
                return true;
            }
            if (!rightClick && m_onReturnClick) {
                m_onReturnClick(b);
                return true;
            }
        }
        curX += kMaxReturnBuses * (kRetStripW + kStripPadding) + kSeparatorWidth + 4;
    }

    const float masterX = curX;
    if (mx >= masterX && mx < masterX + kRetStripW) {
        if (!rightClick) {
            if (hitChild(m_stopAllBtn)) return true;
        }
        // Master fader
        {
            const auto& fb = m_masterStrip.fader.bounds();
            if (mx >= fb.x && mx < fb.x + fb.w &&
                my >= fb.y && my < fb.y + fb.h) {
                if (rightClick) {
                    openMidiLearnMenu(mx, my,
                        automation::AutomationTarget::mixer(-1, automation::MixerParam::Volume),
                        0.0f, 2.0f,
                        [this]() {
                            m_engine->sendCommand(audio::SetMasterVolumeMsg{1.0f});
                        });
                    return true;
                }
                return hitChild(m_masterStrip.fader);
            }
        }

        if (rightClick && m_onMasterRightClick) {
            m_onMasterRightClick(mx, my);
            return true;
        }
        if (!rightClick && m_onMasterClick) {
            m_onMasterClick();
            return true;
        }
    }

    return false;
}

void ReturnMasterPanel::paintStripCommon(UIContext& ctx, StripWidgets& sw,
                                          const char* name, float x, float y,
                                          float w, float h, Color col,
                                          float volume, float peakL, float peakR,
                                          bool muted) {
    (void)col;
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    const ThemeMetrics& met = theme().metrics;
    const float nameSize  = met.fontSizeSmall;
    const float smallSize = met.fontSizeSmall;

    if (name) {
        const float lh = tm.lineHeight(nameSize);
        tm.drawText(r, name, x + 4, y + 5 + (14.0f - lh) * 0.5f,
                    nameSize, ::yawn::ui::Theme::textPrimary);
    }

    float curY = y + 26;

    // Mute toggle. Engine-state sync uses Automation source so
    // onChange stays silent — otherwise the async command queue
    // (user click → sendCommand → engine still reports old state
    // next frame → setState re-fires onChange → reverts click at
    // audio thread) pings-pongs the button. Same pattern as
    // TransportPanel's BPM sync.
    sw.muteBtn.setState(muted, ValueChangeSource::Automation);
    sw.muteBtn.measure(Constraints::tight(kButtonWidth, kButtonHeight), ctx);
    sw.muteBtn.layout(Rect{x + 4, curY, kButtonWidth, kButtonHeight}, ctx);
    sw.muteBtn.render(ctx);

    curY += kButtonHeight + 6;
    sw.pan.measure(Constraints::tight(w - 8, 16), ctx);
    sw.pan.layout(Rect{x + 4, curY, w - 8, 16}, ctx);
    sw.pan.render(ctx);

    curY += 16 + 8;
    const float faderBottom = y + h - 22;
    const float faderH = std::max(20.0f, faderBottom - curY);

    // Skip sync during drag so the engine's lagging value doesn't
    // rubber-band the thumb the user is holding.
    if (!sw.fader.isDragging())
        sw.fader.setValue(volume, ValueChangeSource::Automation);
    sw.fader.measure(Constraints::tight(kFaderWidth, faderH), ctx);
    sw.fader.layout(Rect{x + 4, curY, kFaderWidth, faderH}, ctx);
    sw.fader.render(ctx);

    sw.meter.setPeak(peakL, peakR);
    sw.meter.measure(Constraints::tight(kMeterWidth * 2, faderH), ctx);
    sw.meter.layout(Rect{x + 4 + kFaderWidth + 3, curY,
                          kMeterWidth * 2, faderH}, ctx);
    sw.meter.render(ctx);

    // dB readout under the fader.
    const float db = (volume > 0.001f) ? 20.0f * std::log10(volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else              std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    const float dbLh = tm.lineHeight(smallSize);
    tm.drawText(r, dbText, x + 4,
                y + h - 18 + (14.0f - dbLh) * 0.5f,
                smallSize, ::yawn::ui::Theme::textSecondary);
}

void ReturnMasterPanel::paintReturnStrip(UIContext& ctx, int idx, float x, float y,
                                          float w, float h) {
    auto& r = *ctx.renderer;
    const Color busCol{100, 180, 255, 255};
    auto& rs = m_returnStrips[idx];
    const auto& rb = m_engine->mixer().returnBus(idx);

    r.drawRect(x, y, w, h, ::yawn::ui::Theme::background);
    r.drawRect(x, y, w, 3, busCol);

    if (!rs.pan.isDragging())
        rs.pan.setValue(rb.pan, ValueChangeSource::Automation);
    rs.fader.setTrackColor(busCol);

    paintStripCommon(ctx, rs, m_returnNames[idx].c_str(),
                     x, y, w, h, busCol,
                     rb.volume,
                     m_returnMeters[idx].peakL,
                     m_returnMeters[idx].peakR,
                     rb.muted);

    // CC label for volume (above dB readout).
    if (m_learnManager) {
        const float ccSize = 7.0f;
        const Color ccCol{100, 180, 255, 255};
        const int tIdx = -(idx + 2);
        const auto volTarget =
            automation::AutomationTarget::mixer(tIdx, automation::MixerParam::Volume);
        if (auto* volMap = m_learnManager->findByTarget(volTarget)) {
            const auto lbl = volMap->label();
            ctx.textMetrics->drawText(r, lbl, x + 4, y + h - 32, ccSize, ccCol);
        }
    }
}

void ReturnMasterPanel::paintMasterStrip(UIContext& ctx, float x, float y,
                                          float w, float h) {
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;
    const Color masterCol = ::yawn::ui::Theme::transportAccent;
    const auto& master = m_engine->mixer().master();

    r.drawRect(x, y, w, h, Color{35, 35, 40, 255});
    r.drawRect(x, y, w, 3, masterCol);

    if (!m_masterStrip.fader.isDragging())
        m_masterStrip.fader.setValue(master.volume, ValueChangeSource::Automation);

    const ThemeMetrics& met = theme().metrics;
    const float nameSize = met.fontSizeSmall;
    {
        const float lh = tm.lineHeight(nameSize);
        tm.drawText(r, "MASTER", x + 4, y + 5 + (14.0f - lh) * 0.5f,
                    nameSize, ::yawn::ui::Theme::textPrimary);
    }

    // Stop-all button with overlaid square icon.
    m_stopAllBtn.measure(Constraints::tight(w - 8, kButtonHeight), ctx);
    m_stopAllBtn.layout(Rect{x + 4, y + 22, w - 8, kButtonHeight}, ctx);
    m_stopAllBtn.render(ctx);
    {
        const auto& sb = m_stopAllBtn.bounds();
        const float iconSize = 8.0f;
        const float iconX = sb.x + (sb.w - iconSize) * 0.5f;
        const float iconY = sb.y + (sb.h - iconSize) * 0.5f;
        r.drawRect(iconX, iconY, iconSize, iconSize,
                    ::yawn::ui::Theme::textSecondary);
    }

    const float curY = y + 22 + kButtonHeight + 4;
    const float faderBottom = y + h - 22;
    const float faderH = std::max(20.0f, faderBottom - curY);

    m_masterStrip.fader.measure(Constraints::tight(kFaderWidth + 2, faderH), ctx);
    m_masterStrip.fader.layout(Rect{x + 4, curY, kFaderWidth + 2, faderH}, ctx);
    m_masterStrip.fader.render(ctx);

    m_masterStrip.meter.setPeak(m_masterMeter.peakL, m_masterMeter.peakR);
    m_masterStrip.meter.measure(Constraints::tight(kMeterWidth * 2 + 2, faderH), ctx);
    m_masterStrip.meter.layout(Rect{x + kFaderWidth + 10, curY,
                                      kMeterWidth * 2 + 2, faderH}, ctx);
    m_masterStrip.meter.render(ctx);

    const float smallSize = met.fontSizeSmall;
    const float db = (master.volume > 0.001f) ? 20.0f * std::log10(master.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else              std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    const float dbLh = tm.lineHeight(smallSize);
    tm.drawText(r, dbText, x + 4,
                y + h - 18 + (14.0f - dbLh) * 0.5f,
                smallSize, ::yawn::ui::Theme::textSecondary);

    // CC label for master volume.
    if (m_learnManager) {
        const auto target =
            automation::AutomationTarget::mixer(-1, automation::MixerParam::Volume);
        if (auto* mapping = m_learnManager->findByTarget(target)) {
            const auto lbl = mapping->label();
            const float ccSize = 8.0f;
            tm.drawText(r, lbl, x + 4, y + h - 32, ccSize,
                        Color{100, 180, 255, 255});
        }
    }
}

void ReturnMasterPanel::openMidiLearnMenu(float mx, float my,
                                            const automation::AutomationTarget& target,
                                            float paramMin, float paramMax,
                                            std::function<void()> resetAction) {
    using Item = ::yawn::ui::ContextMenu::Item;
    std::vector<Item> items;

    const bool hasMapping = m_learnManager && m_learnManager->findByTarget(target) != nullptr;
    const bool isLearning = m_learnManager && m_learnManager->isLearning() &&
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

    ContextMenu::show(v1ItemsToFw2(std::move(items)),
                      Point{mx, my});
}

} // namespace fw2
} // namespace ui
} // namespace yawn
