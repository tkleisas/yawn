// ReturnMasterPanel.cpp — rendering and event implementations.
// Split from ReturnMasterPanel.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "ReturnMasterPanel.h"
#include "../Renderer.h"
#include "../Font.h"
#include "ui/framework/v2/V1MenuBridge.h"

namespace yawn {
namespace ui {
namespace fw {

void ReturnMasterPanel::paint(UIContext& ctx) {
    if (!m_engine) return;
    auto& r = *ctx.renderer;

    float x = m_bounds.x, y = m_bounds.y;
    float w = m_bounds.w, h = m_bounds.h;

    r.drawRect(x, y, w, h, Theme::panelBg);
    r.drawRect(x, y, w, 1, Theme::clipSlotBorder);

    float stripY = y + 2;
    float stripH = h - 4;

    float curX = x + 4;
    if (m_showReturns) {
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            if (curX + kRetStripW > x + w) break;
            paintReturnStrip(ctx, b, curX, stripY, kRetStripW, stripH);
            curX += kRetStripW + kStripPadding;
        }
        r.drawRect(curX, y + 4, kSeparatorWidth, h - 8, Theme::clipSlotBorder);
        curX += kSeparatorWidth + 4;
    }

    if (curX + kRetStripW <= x + w)
        paintMasterStrip(ctx, curX, stripY, kRetStripW, stripH);

    // v1 context menu retired — fw2::ContextMenu paints via LayerStack.
}

bool ReturnMasterPanel::onMouseDown(MouseEvent& e) {
    if (!m_engine) return false;
    float mx = e.x, my = e.y;
    bool rightClick = (e.button == MouseButton::Right);
    float x = m_bounds.x, y = m_bounds.y;

    // v1 context menu retired — LayerStack intercepts clicks upstream.

    float curX = x + 4;
    if (m_showReturns) {
        for (int b = 0; b < kMaxReturnBuses; ++b) {
            float rx = curX + b * (kRetStripW + kStripPadding);
            if (mx < rx || mx >= rx + kRetStripW) continue;

            auto& rs = m_returnStrips[b];

            if (!rightClick) {
                const auto& mb = rs.muteBtn.bounds();
                if (mx >= mb.x && mx <= mb.x + mb.w &&
                    my >= mb.y && my <= mb.y + mb.h) {
                    auto ev = ::yawn::ui::fw2::toFw2Mouse(e, mb);
                    rs.muteBtn.dispatchMouseDown(ev);
                    m_v2Dragging = &rs.muteBtn;
                    captureMouse();
                    return true;
                }
            }
            if (hitWidget(rs.pan, mx, my)) {
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
                return rs.pan.onMouseDown(e);
            }
            if (hitWidget(rs.fader, mx, my)) {
                if (rightClick) {
                    openMidiLearnMenu(mx, my,
                        automation::AutomationTarget::mixer(-(b + 2), automation::MixerParam::Volume),
                        0.0f, 2.0f,
                        [this, b]() {
                            m_engine->sendCommand(audio::SetReturnVolumeMsg{b, 1.0f});
                        });
                    return true;
                }
                return rs.fader.onMouseDown(e);
            }

            // Right-click on strip background → show "Add Effect" context menu
            if (rightClick && m_onReturnRightClick) {
                m_onReturnRightClick(b, mx, my);
                return true;
            }

            // Click on strip background/name area → open detail panel for this return bus
            if (!rightClick && m_onReturnClick) {
                m_onReturnClick(b);
                return true;
            }
        }
        curX += kMaxReturnBuses * (kRetStripW + kStripPadding) + kSeparatorWidth + 4;
    }

    float masterX = curX;

    if (mx >= masterX && mx < masterX + kRetStripW) {
        if (!rightClick) {
            const auto& sb = m_stopAllBtn.bounds();
            if (mx >= sb.x && mx <= sb.x + sb.w &&
                my >= sb.y && my <= sb.y + sb.h) {
                auto ev = ::yawn::ui::fw2::toFw2Mouse(e, sb);
                m_stopAllBtn.dispatchMouseDown(ev);
                m_v2Dragging = &m_stopAllBtn;
                captureMouse();
                return true;
            }
        }
        if (hitWidget(m_masterStrip.fader, mx, my)) {
            if (rightClick) {
                openMidiLearnMenu(mx, my,
                    automation::AutomationTarget::mixer(-1, automation::MixerParam::Volume),
                    0.0f, 2.0f,
                    [this]() {
                        m_engine->sendCommand(audio::SetMasterVolumeMsg{1.0f});
                    });
                return true;
            }
            return m_masterStrip.fader.onMouseDown(e);
        }

        // Right-click on master strip background → show "Add Effect" context menu
        if (rightClick && m_onMasterRightClick) {
            m_onMasterRightClick(mx, my);
            return true;
        }

        // Click on master strip background/name → open detail panel for master
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
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;
    float pixH = f.pixelHeight();
    float scale = (pixH < 1.0f) ? Theme::kSmallFontSize
                 : Theme::kSmallFontSize / pixH;
    float smallScale = scale * 0.8f;

    sw.nameLabel.setFontScale(scale);
    sw.nameLabel.layout(Rect{x + 4, y + 5, w - 8, 14}, ctx);
    sw.nameLabel.paint(ctx);

    float curY = y + 26;

    // Mute button — v2 FwToggle. setState drives the accent-fill-on
    // appearance; accent color was configured at construction.
    {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        sw.muteBtn.setState(muted);
        sw.muteBtn.layout(Rect{x + 4, curY, kButtonWidth, kButtonHeight}, v2ctx);
        sw.muteBtn.render(v2ctx);
    }

    curY += kButtonHeight + 6;
    sw.pan.layout(Rect{x + 4, curY, w - 8, 16}, ctx);
    sw.pan.paint(ctx);

    curY += 16 + 8;
    float faderBottom = y + h - 22;
    float faderH = std::max(20.0f, faderBottom - curY);

    sw.fader.setValue(volume);
    sw.fader.layout(Rect{x + 4, curY, kFaderWidth, faderH}, ctx);
    sw.fader.paint(ctx);

    sw.meter.setPeak(peakL, peakR);
    sw.meter.layout(Rect{x + 4 + kFaderWidth + 3, curY,
                         kMeterWidth * 2, faderH}, ctx);
    sw.meter.paint(ctx);

    float db = volume > 0.001f ? 20.0f * std::log10(volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    sw.dbLabel.setText(dbText);
    sw.dbLabel.setColor(Theme::textDim);
    sw.dbLabel.setFontScale(smallScale);
    sw.dbLabel.layout(Rect{x + 4, y + h - 18, w - 8, 14}, ctx);
    sw.dbLabel.paint(ctx);
}

void ReturnMasterPanel::paintReturnStrip(UIContext& ctx, int idx, float x, float y,
                       float w, float h) {
    auto& r = *ctx.renderer;
    Color busCol{100, 180, 255};
    auto& rs = m_returnStrips[idx];
    const auto& rb = m_engine->mixer().returnBus(idx);

    r.drawRect(x, y, w, h, Theme::background);
    r.drawRect(x, y, w, 3, busCol);

    rs.pan.setValue(rb.pan);
    rs.fader.setTrackColor(busCol);

    paintStripCommon(ctx, rs, nullptr, x, y, w, h, busCol,
                     rb.volume,
                     m_returnMeters[idx].peakL,
                     m_returnMeters[idx].peakR,
                     rb.muted);

    // CC labels for return bus volume/pan
    if (m_learnManager) {
        float ccScale = 7.0f / Theme::kFontSize;
        Color ccCol{100, 180, 255};
        int tIdx = -(idx + 2);
        auto volTarget = automation::AutomationTarget::mixer(tIdx, automation::MixerParam::Volume);
        auto* volMap = m_learnManager->findByTarget(volTarget);
        if (volMap) {
            auto lbl = volMap->label();
            ctx.font->drawText(*ctx.renderer, lbl.c_str(), x + 4, y + h - 32, ccScale, ccCol);
        }
    }
}

void ReturnMasterPanel::paintMasterStrip(UIContext& ctx, float x, float y,
                       float w, float h) {
    auto& r = *ctx.renderer;
    Color masterCol = Theme::transportAccent;
    const auto& master = m_engine->mixer().master();

    r.drawRect(x, y, w, h, Color{35, 35, 40});
    r.drawRect(x, y, w, 3, masterCol);

    m_masterStrip.fader.setValue(master.volume);

    m_masterStrip.nameLabel.layout(Rect{x + 4, y + 5, w - 8, 14}, ctx);
    m_masterStrip.nameLabel.paint(ctx);

    // Stop-all button — v2 FwButton with an overlaid stop-icon square.
    {
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_stopAllBtn.layout(Rect{x + 4, y + 22, w - 8, kButtonHeight}, v2ctx);
        m_stopAllBtn.render(v2ctx);
    }
    {
        auto& sb = m_stopAllBtn.bounds();
        float iconSize = 8.0f;
        float iconX = sb.x + (sb.w - iconSize) * 0.5f;
        float iconY = sb.y + (sb.h - iconSize) * 0.5f;
        r.drawRect(iconX, iconY, iconSize, iconSize, Theme::textSecondary);
    }

    float curY = y + 22 + kButtonHeight + 4;
    float faderBottom = y + h - 22;
    float faderH = std::max(20.0f, faderBottom - curY);

    m_masterStrip.fader.layout(Rect{x + 4, curY, kFaderWidth + 2, faderH}, ctx);
    m_masterStrip.fader.paint(ctx);

    m_masterStrip.meter.setPeak(m_masterMeter.peakL, m_masterMeter.peakR);
    m_masterStrip.meter.layout(Rect{x + kFaderWidth + 10, curY,
                                    kMeterWidth * 2 + 2, faderH}, ctx);
    m_masterStrip.meter.paint(ctx);

    float smallScale = Theme::kSmallFontSize / Theme::kFontSize * 0.6f;
    float db = master.volume > 0.001f ? 20.0f * std::log10(master.volume) : -60.0f;
    char dbText[16];
    if (db <= -60.0f) std::snprintf(dbText, sizeof(dbText), "-inf");
    else std::snprintf(dbText, sizeof(dbText), "%.1f", db);
    m_masterStrip.dbLabel.setText(dbText);
    m_masterStrip.dbLabel.setColor(Theme::textDim);
    m_masterStrip.dbLabel.setFontScale(smallScale);
    m_masterStrip.dbLabel.layout(Rect{x + 4, y + h - 18, w - 8, 14}, ctx);
    m_masterStrip.dbLabel.paint(ctx);

    // CC label for master volume
    if (m_learnManager) {
        auto target = automation::AutomationTarget::mixer(-1, automation::MixerParam::Volume);
        auto* mapping = m_learnManager->findByTarget(target);
        if (mapping) {
            auto lbl = mapping->label();
            float ccScale = 8.0f / Theme::kFontSize;
            ctx.font->drawText(r, lbl.c_str(), x + 4, y + h - 32, ccScale, Color{100, 180, 255});
        }
    }
}

void ReturnMasterPanel::openMidiLearnMenu(float mx, float my,
                                           const automation::AutomationTarget& target,
                                           float paramMin, float paramMax,
                                           std::function<void()> resetAction) {
    using Item = ContextMenu::Item;
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
