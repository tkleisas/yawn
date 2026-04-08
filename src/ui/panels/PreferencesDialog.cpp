// PreferencesDialog.cpp — rendering and event implementations.
// Split from PreferencesDialog.h to keep rendering code out of the header.
// This file is only compiled in the main exe build (not test builds).

#include "PreferencesDialog.h"
#include "../Renderer.h"
#include "../Font.h"

namespace yawn {
namespace ui {
namespace fw {

void PreferencesDialog::paint(UIContext& ctx) {
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;

    r.drawRect(0, 0, m_screenW, m_screenH, Color{0, 0, 0, 140});

    float dw = m_preferredW;
    float dh = m_preferredH;

    r.drawRect(m_dx, m_dy, dw, dh, Color{45, 45, 52, 255});
    r.drawRectOutline(m_dx, m_dy, dw, dh, Color{75, 75, 85, 255});

    float titleScale = 14.0f / Theme::kFontSize;
    float tabScale = 12.0f / Theme::kFontSize;
    float textScale = 11.0f / Theme::kFontSize;
    float textH = f.lineHeight(textScale);

    r.drawRect(m_dx, m_dy, dw, kTitleBarHeight, Color{55, 55, 62, 255});
    f.drawText(r, m_title.c_str(), m_dx + 12,
               m_dy + (kTitleBarHeight - f.lineHeight(titleScale)) * 0.5f,
               titleScale, Theme::textPrimary);

    auto cb = closeButtonRect();
    r.drawRect(cb.x, cb.y, cb.w, cb.h, Color{160, 50, 50, 255});
    float xScale = 10.0f / Theme::kFontSize;
    f.drawText(r, "X", cb.x + (cb.w - f.textWidth("X", xScale)) * 0.5f,
               cb.y + (cb.h - f.lineHeight(xScale)) * 0.5f,
               xScale, Theme::textPrimary);

    float tabY = m_dy + kTitleBarHeight;
    float tabH = 28.0f;
    float tabW = dw / 4.0f;
    const char* tabNames[] = {"Audio", "MIDI", "Defaults", "Metronome"};
    for (int i = 0; i < 4; ++i) {
        Color bg = (i == m_tab) ? Color{65, 65, 75, 255} : Color{50, 50, 55, 255};
        r.drawRect(m_dx + i * tabW, tabY, tabW, tabH, bg);
        r.drawRectOutline(m_dx + i * tabW, tabY, tabW, tabH, Color{70, 70, 80, 255});
        float tw = f.textWidth(tabNames[i], tabScale);
        f.drawText(r, tabNames[i],
                   m_dx + i * tabW + (tabW - tw) * 0.5f,
                   tabY + (tabH - f.lineHeight(tabScale)) * 0.5f,
                   tabScale, (i == m_tab) ? Theme::textPrimary : Theme::textSecondary);
    }

    float contentY = tabY + tabH + 8;
    float contentX = m_dx + 16;
    float contentW = dw - 32;
    float rowH = 26.0f;

    if (m_tab == 0) paintAudioTab(ctx, contentX, contentY, contentW, rowH, textScale, textH);
    else if (m_tab == 1) paintMidiTab(ctx, contentX, contentY, contentW, rowH, textScale, textH);
    else if (m_tab == 2) paintDefaultsTab(ctx, contentX, contentY, contentW, rowH, textScale, textH);
    else paintMetronomeTab(ctx, contentX, contentY, contentW, rowH, textScale, textH);

    float footerY = m_dy + dh - kFooterHeight;
    r.drawRect(m_dx, footerY, dw, kFooterHeight, Color{50, 50, 55, 255});

    auto okR = okButtonRect();
    r.drawRect(okR.x, okR.y, okR.w, okR.h, Color{50, 130, 50, 255});
    f.drawText(r, "OK", okR.x + (okR.w - f.textWidth("OK", tabScale)) * 0.5f,
               okR.y + (okR.h - f.lineHeight(tabScale)) * 0.5f, tabScale, Theme::textPrimary);

    auto cancelR = cancelButtonRect();
    r.drawRect(cancelR.x, cancelR.y, cancelR.w, cancelR.h, Color{130, 50, 50, 255});
    f.drawText(r, "Cancel", cancelR.x + (cancelR.w - f.textWidth("Cancel", tabScale)) * 0.5f,
               cancelR.y + (cancelR.h - f.lineHeight(tabScale)) * 0.5f, tabScale, Theme::textPrimary);
}

bool PreferencesDialog::onMouseDown(MouseEvent& e) {
    float mx = e.x, my = e.y;

    if (mx < m_dx || mx > m_dx + m_preferredW ||
        my < m_dy || my > m_dy + m_preferredH) {
        close(DialogResult::Cancel);
        e.consume();
        return true;
    }

    auto cb = closeButtonRect();
    if (mx >= cb.x && mx <= cb.x + cb.w && my >= cb.y && my <= cb.y + cb.h) {
        close(DialogResult::Cancel);
        e.consume();
        return true;
    }

    auto okR = okButtonRect();
    if (mx >= okR.x && mx <= okR.x + okR.w && my >= okR.y && my <= okR.y + okR.h) {
        close(DialogResult::OK);
        e.consume();
        return true;
    }

    auto cancelR = cancelButtonRect();
    if (mx >= cancelR.x && mx <= cancelR.x + cancelR.w &&
        my >= cancelR.y && my <= cancelR.y + cancelR.h) {
        close(DialogResult::Cancel);
        e.consume();
        return true;
    }

    float tabY = m_dy + kTitleBarHeight;
    float tabH = 28.0f;
    float tabW = m_preferredW / 4.0f;
    if (my >= tabY && my < tabY + tabH) {
        int newTab = static_cast<int>((mx - m_dx) / tabW);
        if (newTab >= 0 && newTab < 4) m_tab = newTab;
        e.consume();
        return true;
    }

    float contentY = tabY + tabH + 8;
    float contentX = m_dx + 16;
    float contentW = m_preferredW - 32;
    float rowH = 26.0f;
    float dropW = std::min(contentW * 0.5f, 220.0f);
    float dropX = contentX + contentW - dropW;

    if (m_tab == 0) {
        handleAudioClick(mx, my, contentX, contentY, dropX, dropW, rowH);
    } else if (m_tab == 1) {
        handleMidiClick(mx, my, contentX, contentY, contentW, rowH);
    } else if (m_tab == 2) {
        handleDefaultsClick(mx, my, contentX, contentY, dropX, dropW, rowH);
    } else {
        handleMetronomeClick(mx, my, contentX, contentY, dropX, dropW, rowH);
    }

    e.consume();
    return true;
}

void PreferencesDialog::paintAudioTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                   float ts, float th) {
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;
    float dropW = std::min(cw * 0.5f, 220.0f);
    float dropX = cx + cw - dropW;

    const char* rates[] = {"44100", "48000", "96000", "192000"};
    double rateVals[] = {44100.0, 48000.0, 96000.0, 192000.0};
    int rateSel = 0;
    for (int i = 0; i < 4; ++i)
        if (std::abs(m_state.sampleRate - rateVals[i]) < 1.0) rateSel = i;

    const char* sizes[] = {"64", "128", "256", "512", "1024", "2048"};
    int sizeVals[] = {64, 128, 256, 512, 1024, 2048};
    int sizeSel = 2;
    for (int i = 0; i < 6; ++i)
        if (m_state.bufferSize == sizeVals[i]) sizeSel = i;

    float y0 = cy, y1 = cy + rh + 6, y2 = cy + 2 * (rh + 6), y3 = cy + 3 * (rh + 6);

    // Pass 1: buttons
    drawLabel(r, f, "Audio Output Device", cx, y0, ts);
    drawDeviceDropdown(r, f, dropX, y0, dropW, rh, th, ts,
                       m_outputDevices, m_state.selectedOutputDevice, true, &m_audioOutputOpen, false);

    drawLabel(r, f, "Audio Input Device", cx, y1, ts);
    drawDeviceDropdown(r, f, dropX, y1, dropW, rh, th, ts,
                       m_outputDevices, m_state.selectedInputDevice, false, &m_audioInputOpen, false);

    drawLabel(r, f, "Sample Rate", cx, y2, ts);
    drawDropdown(r, f, dropX, y2, dropW, rh, th, ts, rates, 4, rateSel, &m_sampleRateOpen, false);

    drawLabel(r, f, "Buffer Size", cx, y3, ts);
    drawDropdown(r, f, dropX, y3, dropW, rh, th, ts, sizes, 6, sizeSel, &m_bufferSizeOpen, false);

    // Pass 2: popups
    drawDeviceDropdown(r, f, dropX, y0, dropW, rh, th, ts,
                       m_outputDevices, m_state.selectedOutputDevice, true, &m_audioOutputOpen, true);
    drawDeviceDropdown(r, f, dropX, y1, dropW, rh, th, ts,
                       m_outputDevices, m_state.selectedInputDevice, false, &m_audioInputOpen, true);
    drawDropdown(r, f, dropX, y2, dropW, rh, th, ts, rates, 4, rateSel, &m_sampleRateOpen, true);
    drawDropdown(r, f, dropX, y3, dropW, rh, th, ts, sizes, 6, sizeSel, &m_bufferSizeOpen, true);
}

void PreferencesDialog::handleAudioClick(float mx, float my, float cx, float cy,
                      float dropX, float dropW, float rh) {
    float dropH = 24.0f;

    if (handleDeviceDropdownClick(mx, my, dropX, cy, dropW, dropH,
                                   m_outputDevices, m_state.selectedOutputDevice,
                                   true, &m_audioOutputOpen)) return;
    cy += rh + 6;

    if (handleDeviceDropdownClick(mx, my, dropX, cy, dropW, dropH,
                                   m_outputDevices, m_state.selectedInputDevice,
                                   false, &m_audioInputOpen)) return;
    cy += rh + 6;

    {
        const char* rates[] = {"44100", "48000", "96000", "192000"};
        double rateVals[] = {44100.0, 48000.0, 96000.0, 192000.0};
        int sel = 0;
        for (int i = 0; i < 4; ++i)
            if (std::abs(m_state.sampleRate - rateVals[i]) < 1.0) sel = i;
        if (handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 4, sel,
                                &m_sampleRateOpen, [&](int i) {
            m_state.sampleRate = rateVals[i];
        })) return;
    }
    cy += rh + 6;

    {
        const char* sizes[] = {"64", "128", "256", "512", "1024", "2048"};
        int sizeVals[] = {64, 128, 256, 512, 1024, 2048};
        int sel = 2;
        for (int i = 0; i < 6; ++i)
            if (m_state.bufferSize == sizeVals[i]) sel = i;
        handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 6, sel,
                            &m_bufferSizeOpen, [&](int i) {
            m_state.bufferSize = sizeVals[i];
        });
    }
}

void PreferencesDialog::paintMidiTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                  float ts, float th) {
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;

    drawLabel(r, f, "MIDI Inputs (click to toggle)", cx, cy, ts);
    cy += rh;

    for (int i = 0; i < (int)m_midiInputs.size(); ++i) {
        bool enabled = std::find(m_state.enabledMidiInputs.begin(),
                                 m_state.enabledMidiInputs.end(), i)
                       != m_state.enabledMidiInputs.end();
        Color bg = enabled ? Color{50, 120, 50, 255} : Color{60, 60, 65, 255};
        r.drawRect(cx, cy, cw, rh - 4, bg);
        r.drawRectOutline(cx, cy, cw, rh - 4, Color{75, 75, 80, 255});
        float tw = f.textWidth(m_midiInputs[i].c_str(), ts);
        f.drawText(r, m_midiInputs[i].c_str(), cx + 10,
                   cy + (rh - 4 - th) * 0.5f, ts, Theme::textPrimary);
        if (enabled) {
            const char* chk = "[X]";
            f.drawText(r, chk, cx + cw - 30,
                       cy + (rh - 4 - th) * 0.5f, ts, Theme::textPrimary);
        }
        cy += rh;
    }

    cy += 8;
    drawLabel(r, f, "MIDI Outputs (click to toggle)", cx, cy, ts);
    cy += rh;

    for (int i = 0; i < (int)m_midiOutputs.size(); ++i) {
        bool enabled = std::find(m_state.enabledMidiOutputs.begin(),
                                 m_state.enabledMidiOutputs.end(), i)
                       != m_state.enabledMidiOutputs.end();
        Color bg = enabled ? Color{50, 120, 50, 255} : Color{60, 60, 65, 255};
        r.drawRect(cx, cy, cw, rh - 4, bg);
        r.drawRectOutline(cx, cy, cw, rh - 4, Color{75, 75, 80, 255});
        float tw = f.textWidth(m_midiOutputs[i].c_str(), ts);
        f.drawText(r, m_midiOutputs[i].c_str(), cx + 10,
                   cy + (rh - 4 - th) * 0.5f, ts, Theme::textPrimary);
        if (enabled) {
            const char* chk = "[X]";
            f.drawText(r, chk, cx + cw - 30,
                       cy + (rh - 4 - th) * 0.5f, ts, Theme::textPrimary);
        }
        cy += rh;
    }
}

void PreferencesDialog::handleMidiClick(float mx, float my, float cx, float cy,
                     float cw, float rh) {
    float headerH = rh;
    cy += headerH;

    for (int i = 0; i < (int)m_midiInputs.size(); ++i) {
        if (mx >= cx && mx <= cx + cw && my >= cy && my < cy + rh - 4) {
            auto it = std::find(m_state.enabledMidiInputs.begin(),
                                m_state.enabledMidiInputs.end(), i);
            if (it != m_state.enabledMidiInputs.end())
                m_state.enabledMidiInputs.erase(it);
            else
                m_state.enabledMidiInputs.push_back(i);
            return;
        }
        cy += rh;
    }

    cy += 8 + rh;

    for (int i = 0; i < (int)m_midiOutputs.size(); ++i) {
        if (mx >= cx && mx <= cx + cw && my >= cy && my < cy + rh - 4) {
            auto it = std::find(m_state.enabledMidiOutputs.begin(),
                                m_state.enabledMidiOutputs.end(), i);
            if (it != m_state.enabledMidiOutputs.end())
                m_state.enabledMidiOutputs.erase(it);
            else
                m_state.enabledMidiOutputs.push_back(i);
            return;
        }
        cy += rh;
    }
}

void PreferencesDialog::paintDefaultsTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                      float ts, float th) {
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;
    float dropW = std::min(cw * 0.5f, 220.0f);
    float dropX = cx + cw - dropW;

    const char* launchModes[] = {"None", "Beat", "Bar"};
    int launchSel = static_cast<int>(m_state.defaultLaunchQuantize);
    if (launchSel < 0 || launchSel > 2) launchSel = 2;

    const char* recModes[] = {"None", "Beat", "Bar"};
    int recSel = static_cast<int>(m_state.defaultRecordQuantize);
    if (recSel < 0 || recSel > 2) recSel = 2;

    float y0 = cy, y1 = cy + rh + 6;

    // Pass 1: buttons
    drawLabel(r, f, "Default Launch Quantize", cx, y0, ts);
    drawDropdown(r, f, dropX, y0, dropW, rh, th, ts, launchModes, 3, launchSel, &m_launchQOpen, false);

    drawLabel(r, f, "Default Record Quantize", cx, y1, ts);
    drawDropdown(r, f, dropX, y1, dropW, rh, th, ts, recModes, 3, recSel, &m_recordQOpen, false);

    // Pass 2: popups
    drawDropdown(r, f, dropX, y0, dropW, rh, th, ts, launchModes, 3, launchSel, &m_launchQOpen, true);
    drawDropdown(r, f, dropX, y1, dropW, rh, th, ts, recModes, 3, recSel, &m_recordQOpen, true);
}

void PreferencesDialog::handleDefaultsClick(float mx, float my, float cx, float cy,
                         float dropX, float dropW, float rh) {
    float dropH = 24.0f;

    {
        const char* modes[] = {"None", "Beat", "Bar"};
        int sel = static_cast<int>(m_state.defaultLaunchQuantize);
        if (sel < 0 || sel > 2) sel = 2;
        handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 3, sel,
                            &m_launchQOpen, [&](int i) {
            m_state.defaultLaunchQuantize = static_cast<audio::QuantizeMode>(i);
        });
    }
    cy += rh + 6;

    {
        const char* modes[] = {"None", "Beat", "Bar"};
        int sel = static_cast<int>(m_state.defaultRecordQuantize);
        if (sel < 0 || sel > 2) sel = 2;
        handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 3, sel,
                            &m_recordQOpen, [&](int i) {
            m_state.defaultRecordQuantize = static_cast<audio::QuantizeMode>(i);
        });
    }
}

void PreferencesDialog::drawLabel(Renderer2D& r, Font& f, const char* text,
               float x, float y, float scale) {
    float textH = f.lineHeight(scale);
    f.drawText(r, text, x, y + (24.0f - textH) * 0.5f, scale, Theme::textSecondary);
}

void PreferencesDialog::drawDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                  float rh, float th, float ts,
                  const char* items[], int count, int selected,
                   bool* isOpen, bool overlayPass) {
    if (!overlayPass) {
    Color bg = *isOpen ? Color{55, 55, 60, 255} : Color{42, 42, 46, 255};
    Color border = *isOpen ? Color{100, 160, 220, 255} : Color{70, 70, 75, 255};
    r.drawRect(x, y, w, 24.0f, bg);
    r.drawRectOutline(x, y, w, 24.0f, border);

    float arrowZone = 14.0f;
    if (selected >= 0 && selected < count) {
        r.pushClip(x + 1, y, w - 6 - arrowZone + 5, 24.0f);
        f.drawText(r, items[selected], x + 8,
                   y + (24.0f - th) * 0.5f, ts, Theme::textPrimary);
        r.popClip();
    }

    // Triangle arrow
    float triSize = 4.0f;
    float triCx = x + w - arrowZone * 0.5f;
    float triCy = y + 12.0f;
    if (*isOpen) {
        r.drawTriangle(triCx, triCy - triSize * 0.5f,
                       triCx - triSize, triCy + triSize * 0.5f,
                       triCx + triSize, triCy + triSize * 0.5f,
                       Theme::textSecondary);
    } else {
        r.drawTriangle(triCx - triSize, triCy - triSize * 0.5f,
                       triCx + triSize, triCy - triSize * 0.5f,
                       triCx, triCy + triSize * 0.5f,
                       Theme::textSecondary);
    }

    return;
    }

    // Overlay pass: draw popup
    if (*isOpen) {
        float popupY = y + 24.0f;
        float popupH = count * 22.0f;
        r.drawRect(x, popupY, w, popupH, Color{30, 30, 34, 255});
        r.drawRectOutline(x, popupY, w, popupH, Color{90, 140, 200, 255});
        for (int i = 0; i < count; ++i) {
            float iy = popupY + i * 22.0f;
            Color itemBg, textCol;
            if (m_popupHover == i) {
                itemBg = Color{200, 200, 210, 255};
                textCol = Color{15, 15, 20, 255};
            } else if (i == selected) {
                itemBg = Color{70, 130, 200, 255};
                textCol = Color{255, 255, 255, 255};
            } else {
                itemBg = Color{30, 30, 34, 255};
                textCol = Theme::textPrimary;
            }
            r.drawRect(x + 1, iy, w - 2, 22.0f, itemBg);
            f.drawText(r, items[i], x + 8, iy + (22.0f - th) * 0.5f, ts, textCol);
        }
    }
}

void PreferencesDialog::drawDeviceDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                        float rh, float th, float ts,
                        const std::vector<audio::AudioDevice>& devices,
                        int selected, bool outputOnly, bool* isOpen,
                        bool overlayPass) {
    if (!overlayPass) {
    Color bg = *isOpen ? Color{55, 55, 60, 255} : Color{42, 42, 46, 255};
    Color border = *isOpen ? Color{100, 160, 220, 255} : Color{70, 70, 75, 255};
    r.drawRect(x, y, w, 24.0f, bg);
    r.drawRectOutline(x, y, w, 24.0f, border);

    float arrowZone = 14.0f;
    r.pushClip(x + 1, y, w - 6 - arrowZone + 5, 24.0f);
    if (selected >= 0) {
        for (auto& d : devices) {
            if (d.id == selected) {
                f.drawText(r, d.name.c_str(), x + 8, y + (24.0f - th) * 0.5f, ts, Theme::textPrimary);
                break;
            }
        }
    } else {
        const char* def = outputOnly ? "Default Output" : "Default Input";
        f.drawText(r, def, x + 8, y + (24.0f - th) * 0.5f, ts, Theme::textSecondary);
    }
    r.popClip();

    // Triangle arrow
    float triSize = 4.0f;
    float triCx = x + w - arrowZone * 0.5f;
    float triCy = y + 12.0f;
    if (*isOpen) {
        r.drawTriangle(triCx, triCy - triSize * 0.5f,
                       triCx - triSize, triCy + triSize * 0.5f,
                       triCx + triSize, triCy + triSize * 0.5f,
                       Theme::textSecondary);
    } else {
        r.drawTriangle(triCx - triSize, triCy - triSize * 0.5f,
                       triCx + triSize, triCy - triSize * 0.5f,
                       triCx, triCy + triSize * 0.5f,
                       Theme::textSecondary);
    }

    return;
    }

    // Overlay pass: draw popup
    if (*isOpen) {
        int visibleCount = 0;
        for (auto& d : devices) {
            if (outputOnly && d.maxOutputChannels > 0) visibleCount++;
            else if (!outputOnly && d.maxInputChannels > 0) visibleCount++;
        }
        float popupY = y + 24.0f;
        float popupH = visibleCount * 22.0f;
        r.drawRect(x, popupY, w, popupH, Color{30, 30, 34, 255});
        r.drawRectOutline(x, popupY, w, popupH, Color{90, 140, 200, 255});
        int idx = 0;
        for (auto& d : devices) {
            if (outputOnly && d.maxOutputChannels == 0) continue;
            if (!outputOnly && d.maxInputChannels == 0) continue;
            float iy = popupY + idx * 22.0f;
            Color itemBg, textCol;
            if (m_popupHover == idx) {
                itemBg = Color{200, 200, 210, 255};
                textCol = Color{15, 15, 20, 255};
            } else if (d.id == selected) {
                itemBg = Color{70, 130, 200, 255};
                textCol = Color{255, 255, 255, 255};
            } else {
                itemBg = Color{30, 30, 34, 255};
                textCol = Theme::textPrimary;
            }
            r.drawRect(x + 1, iy, w - 2, 22.0f, itemBg);
            f.drawText(r, d.name.c_str(), x + 8, iy + (22.0f - th) * 0.5f, ts, textCol);
            idx++;
        }
    }
}

// ─── Metronome Tab ──────────────────────────────────────────────────

void PreferencesDialog::paintMetronomeTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                       float ts, float th) {
    auto& r = *ctx.renderer;
    auto& f = *ctx.font;
    float dropW = std::min(cw * 0.5f, 220.0f);
    float dropX = cx + cw - dropW;

    const char* modes[] = {"Always", "Record Only", "Playback Only", "Off"};
    int modeSel = std::clamp(m_state.metronomeMode, 0, 3);

    const char* bars[] = {"None", "1 Bar", "2 Bars", "4 Bars"};
    int barVals[] = {0, 1, 2, 4};
    int barSel = 0;
    for (int i = 0; i < 4; ++i)
        if (m_state.countInBars == barVals[i]) barSel = i;

    const char* vols[] = {"0% (visual only)", "10%", "20%", "30%", "40%", "50%",
                           "60%", "70%", "80%", "90%", "100%"};
    int volSel = std::clamp(static_cast<int>(m_state.metronomeVolume * 10.0f + 0.5f), 0, 10);

    const char* vizStyles[] = {"Dots", "Beat Number"};
    int vizSel = std::clamp(m_state.metronomeVisualStyle, 0, 1);

    float y0 = cy, y1 = cy + rh + 6, y2 = cy + 2 * (rh + 6), y3 = cy + 3 * (rh + 6);

    // Pass 1: draw all buttons
    drawLabel(r, f, "Metronome Mode", cx, y0, ts);
    drawDropdown(r, f, dropX, y0, dropW, rh, th, ts, modes, 4, modeSel, &m_metroModeOpen, false);

    drawLabel(r, f, "Count-in Bars", cx, y1, ts);
    drawDropdown(r, f, dropX, y1, dropW, rh, th, ts, bars, 4, barSel, &m_countInOpen, false);

    drawLabel(r, f, "Metronome Volume", cx, y2, ts);
    drawDropdown(r, f, dropX, y2, dropW, rh, th, ts, vols, 11, volSel, &m_metroVolumeOpen, false);

    drawLabel(r, f, "Visual Style", cx, y3, ts);
    drawDropdown(r, f, dropX, y3, dropW, rh, th, ts, vizStyles, 2, vizSel, &m_vizStyleOpen, false);

    // Pass 2: draw open popup on top
    drawDropdown(r, f, dropX, y0, dropW, rh, th, ts, modes, 4, modeSel, &m_metroModeOpen, true);
    drawDropdown(r, f, dropX, y1, dropW, rh, th, ts, bars, 4, barSel, &m_countInOpen, true);
    drawDropdown(r, f, dropX, y2, dropW, rh, th, ts, vols, 11, volSel, &m_metroVolumeOpen, true);
    drawDropdown(r, f, dropX, y3, dropW, rh, th, ts, vizStyles, 2, vizSel, &m_vizStyleOpen, true);
}

void PreferencesDialog::handleMetronomeClick(float mx, float my, float cx, float cy,
                          float dropX, float dropW, float rh) {
    float dropH = 24.0f;

    {
        const char* modes[] = {"Always", "Record Only", "Playback Only", "Off"};
        int sel = std::clamp(m_state.metronomeMode, 0, 3);
        if (handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 4, sel,
                                &m_metroModeOpen, [&](int i) {
            m_state.metronomeMode = i;
        })) return;
    }
    cy += rh + 6;

    {
        int barVals[] = {0, 1, 2, 4};
        int sel = 0;
        for (int i = 0; i < 4; ++i)
            if (m_state.countInBars == barVals[i]) sel = i;
        if (handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 4, sel,
                                &m_countInOpen, [&](int i) {
            m_state.countInBars = barVals[i];
        })) return;
    }
    cy += rh + 6;

    {
        int sel = std::clamp(static_cast<int>(m_state.metronomeVolume * 10.0f + 0.5f), 0, 10);
        if (handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 11, sel,
                            &m_metroVolumeOpen, [&](int i) {
            m_state.metronomeVolume = i * 0.1f;
        })) return;
    }
    cy += rh + 6;

    {
        int sel = std::clamp(m_state.metronomeVisualStyle, 0, 1);
        handleDropdownClick(mx, my, dropX, cy, dropW, dropH, 2, sel,
                            &m_vizStyleOpen, [&](int i) {
            m_state.metronomeVisualStyle = i;
        });
    }
}

} // namespace fw
} // namespace ui
} // namespace yawn
