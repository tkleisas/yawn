#pragma once

#include "ui/framework/Dialog.h"
#include "ui/framework/Primitives.h"
#include "ui/Theme.h"
#include "audio/AudioEngine.h"
#include "midi/MidiPort.h"
#include "midi/MidiEngine.h"
#include "audio/ClipEngine.h"
#include <cstdio>
#include <algorithm>

namespace yawn {
namespace ui {
namespace fw {

class PreferencesDialog : public Dialog {
public:
    struct State {
        int selectedOutputDevice = -1;
        int selectedInputDevice = -1;
        double sampleRate = 44100.0;
        int bufferSize = 256;
        audio::QuantizeMode defaultLaunchQuantize = audio::QuantizeMode::NextBar;
        audio::QuantizeMode defaultRecordQuantize = audio::QuantizeMode::NextBar;
        std::vector<int> enabledMidiInputs;
        std::vector<int> enabledMidiOutputs;
    };

    PreferencesDialog()
        : Dialog("Preferences", 540, 420)
    {
        m_showClose = true;
        m_showOKCancel = true;
        m_visible = false;
    }

    void open(State initialState, audio::AudioEngine* engine,
              midi::MidiEngine* midiEngine) {
        m_state = std::move(initialState);
        m_engine = engine;
        m_midiEngine = midiEngine;
        m_tab = 0;
        refreshDevices();
        m_visible = true;
    }

    bool isOpen() const { return m_visible; }
    const State& state() const { return m_state; }

    void close(DialogResult r = DialogResult::Cancel) override {
        m_visible = false;
        m_result = r;
        if (m_onResult) m_onResult(m_result);
    }

    // ─── Layout ─────────────────────────────────────────────────────────

    Size measure(const Constraints&, const UIContext&) override {
        return {m_screenW, m_screenH};
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        m_screenW = bounds.w;
        m_screenH = bounds.h;
        float dx = (m_screenW - m_preferredW) * 0.5f;
        float dy = (m_screenH - m_preferredH) * 0.5f;
        Dialog::layout(Rect{dx, dy, m_preferredW, m_preferredH}, ctx);
        m_dx = dx;
        m_dy = dy;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint(UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
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
        float tabW = dw / 3.0f;
        const char* tabNames[] = {"Audio", "MIDI", "Defaults"};
        for (int i = 0; i < 3; ++i) {
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
        else paintDefaultsTab(ctx, contentX, contentY, contentW, rowH, textScale, textH);

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
#else
        (void)ctx;
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
#ifndef YAWN_TEST_BUILD
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
        float tabW = m_preferredW / 3.0f;
        if (my >= tabY && my < tabY + tabH) {
            int newTab = static_cast<int>((mx - m_dx) / tabW);
            if (newTab >= 0 && newTab < 3) m_tab = newTab;
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
        } else {
            handleDefaultsClick(mx, my, contentX, contentY, dropX, dropW, rowH);
        }

        e.consume();
        return true;
#else
        (void)e;
        return true;
#endif
    }

    bool onKeyDown(KeyEvent& e) override {
        if (e.keyCode == 27) {
            close(DialogResult::Cancel);
            return true;
        }
        if (e.keyCode == 13) {
            close(DialogResult::OK);
            return true;
        }
        return true;
    }

private:
    void refreshDevices() {
        m_outputDevices = audio::AudioEngine::enumerateDevices();
        m_midiInputs.clear();
        m_midiOutputs.clear();
        int inCount = midi::MidiPort::countInputPorts();
        for (int i = 0; i < inCount; ++i)
            m_midiInputs.push_back(midi::MidiPort::inputPortName(i));
        int outCount = midi::MidiPort::countOutputPorts();
        for (int i = 0; i < outCount; ++i)
            m_midiOutputs.push_back(midi::MidiPort::outputPortName(i));
    }

    // ─── Audio Tab ──────────────────────────────────────────────────────

    void paintAudioTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                       float ts, float th) {
#ifndef YAWN_TEST_BUILD
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        float dropW = std::min(cw * 0.5f, 220.0f);
        float dropX = cx + cw - dropW;

        drawLabel(r, f, "Audio Output Device", cx, cy, ts);
        drawDeviceDropdown(r, f, dropX, cy, dropW, rh, th, ts,
                           m_outputDevices, m_state.selectedOutputDevice,
                           true, &m_audioOutputOpen);
        cy += rh + 6;

        drawLabel(r, f, "Audio Input Device", cx, cy, ts);
        drawDeviceDropdown(r, f, dropX, cy, dropW, rh, th, ts,
                           m_outputDevices, m_state.selectedInputDevice,
                           false, &m_audioInputOpen);
        cy += rh + 6;

        drawLabel(r, f, "Sample Rate", cx, cy, ts);
        {
            const char* rates[] = {"44100", "48000", "96000", "192000"};
            int sel = 0;
            double rateVals[] = {44100.0, 48000.0, 96000.0, 192000.0};
            for (int i = 0; i < 4; ++i) {
                if (std::abs(m_state.sampleRate - rateVals[i]) < 1.0) sel = i;
            }
            drawDropdown(r, f, dropX, cy, dropW, rh, th, ts,
                         rates, 4, sel, &m_sampleRateOpen);
        }
        cy += rh + 6;

        drawLabel(r, f, "Buffer Size", cx, cy, ts);
        {
            const char* sizes[] = {"64", "128", "256", "512", "1024", "2048"};
            int sizeVals[] = {64, 128, 256, 512, 1024, 2048};
            int sel = 2;
            for (int i = 0; i < 6; ++i) {
                if (m_state.bufferSize == sizeVals[i]) sel = i;
            }
            drawDropdown(r, f, dropX, cy, dropW, rh, th, ts,
                         sizes, 6, sel, &m_bufferSizeOpen);
        }
#else
        (void)ctx; (void)cx; (void)cy; (void)cw; (void)rh; (void)ts; (void)th;
#endif
    }

    void handleAudioClick(float mx, float my, float cx, float cy,
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

    // ─── MIDI Tab ───────────────────────────────────────────────────────

    void paintMidiTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                      float ts, float th) {
#ifndef YAWN_TEST_BUILD
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
#else
        (void)ctx; (void)cx; (void)cy; (void)cw; (void)rh; (void)ts; (void)th;
#endif
    }

    void handleMidiClick(float mx, float my, float cx, float cy,
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

    // ─── Defaults Tab ───────────────────────────────────────────────────

    void paintDefaultsTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                          float ts, float th) {
#ifndef YAWN_TEST_BUILD
        auto& r = *ctx.renderer;
        auto& f = *ctx.font;
        float dropW = std::min(cw * 0.5f, 220.0f);
        float dropX = cx + cw - dropW;

        drawLabel(r, f, "Default Launch Quantize", cx, cy, ts);
        {
            const char* modes[] = {"None", "Beat", "Bar"};
            int sel = static_cast<int>(m_state.defaultLaunchQuantize);
            if (sel < 0 || sel > 2) sel = 2;
            drawDropdown(r, f, dropX, cy, dropW, rh, th, ts, modes, 3, sel,
                         &m_launchQOpen);
        }
        cy += rh + 6;

        drawLabel(r, f, "Default Record Quantize", cx, cy, ts);
        {
            const char* modes[] = {"None", "Beat", "Bar"};
            int sel = static_cast<int>(m_state.defaultRecordQuantize);
            if (sel < 0 || sel > 2) sel = 2;
            drawDropdown(r, f, dropX, cy, dropW, rh, th, ts, modes, 3, sel,
                         &m_recordQOpen);
        }
#else
        (void)ctx; (void)cx; (void)cy; (void)cw; (void)rh; (void)ts; (void)th;
#endif
    }

    void handleDefaultsClick(float mx, float my, float cx, float cy,
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

    // ─── Drawing Helpers ────────────────────────────────────────────────

    void drawLabel(Renderer2D& r, Font& f, const char* text,
                   float x, float y, float scale) {
#ifndef YAWN_TEST_BUILD
        float textH = f.lineHeight(scale);
        f.drawText(r, text, x, y + (24.0f - textH) * 0.5f, scale, Theme::textSecondary);
#else
        (void)r; (void)f; (void)text; (void)x; (void)y; (void)scale;
#endif
    }

    void drawDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                      float rh, float th, float ts,
                      const char* items[], int count, int selected,
                      bool* isOpen) {
#ifndef YAWN_TEST_BUILD
        Color bg = *isOpen ? Color{65, 65, 75, 255} : Color{55, 55, 60, 255};
        r.drawRect(x, y, w, 24.0f, bg);
        r.drawRectOutline(x, y, w, 24.0f, Color{80, 80, 90, 255});
        if (selected >= 0 && selected < count) {
            f.drawText(r, items[selected], x + 8,
                       y + (24.0f - th) * 0.5f, ts, Theme::textPrimary);
        }
        f.drawText(r, "v", x + w - 16, y + (24.0f - th) * 0.5f, ts, Theme::textDim);

        if (*isOpen) {
            float popupY = y + 24.0f;
            float popupH = count * 22.0f;
            r.drawRect(x, popupY, w, popupH, Color{50, 50, 58, 255});
            r.drawRectOutline(x, popupY, w, popupH, Color{80, 80, 90, 255});
            for (int i = 0; i < count; ++i) {
                float iy = popupY + i * 22.0f;
                if (i == selected) {
                    r.drawRect(x + 2, iy, w - 4, 22.0f, Color{65, 100, 160, 255});
                } else if (m_popupHover == i) {
                    r.drawRect(x + 2, iy, w - 4, 22.0f, Color{60, 60, 70, 255});
                }
                f.drawText(r, items[i], x + 8, iy + (22.0f - th) * 0.5f, ts, Theme::textPrimary);
            }
        }
#else
        (void)r; (void)f; (void)x; (void)y; (void)w; (void)rh; (void)th; (void)ts;
        (void)items; (void)count; (void)selected; (void)isOpen;
#endif
    }

    void drawDeviceDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                            float rh, float th, float ts,
                            const std::vector<audio::AudioDevice>& devices,
                            int selected, bool outputOnly, bool* isOpen) {
#ifndef YAWN_TEST_BUILD
        Color bg = *isOpen ? Color{65, 65, 75, 255} : Color{55, 55, 60, 255};
        r.drawRect(x, y, w, 24.0f, bg);
        r.drawRectOutline(x, y, w, 24.0f, Color{80, 80, 90, 255});

        if (selected >= 0) {
            for (auto& d : devices) {
                if (d.id == selected) {
                    const char* name = d.name.c_str();
                    f.drawText(r, name, x + 8, y + (24.0f - th) * 0.5f, ts, Theme::textPrimary);
                    break;
                }
            }
        } else {
            const char* def = outputOnly ? "Default Output" : "Default Input";
            f.drawText(r, def, x + 8, y + (24.0f - th) * 0.5f, ts, Theme::textSecondary);
        }
        f.drawText(r, "v", x + w - 16, y + (24.0f - th) * 0.5f, ts, Theme::textDim);

        if (*isOpen) {
            int visibleCount = 0;
            for (auto& d : devices) {
                if (outputOnly && d.maxOutputChannels > 0) visibleCount++;
                else if (!outputOnly && d.maxInputChannels > 0) visibleCount++;
            }
            float popupY = y + 24.0f;
            float popupH = visibleCount * 22.0f;
            r.drawRect(x, popupY, w, popupH, Color{50, 50, 58, 255});
            r.drawRectOutline(x, popupY, w, popupH, Color{80, 80, 90, 255});
            int idx = 0;
            for (auto& d : devices) {
                if (outputOnly && d.maxOutputChannels == 0) continue;
                if (!outputOnly && d.maxInputChannels == 0) continue;
                float iy = popupY + idx * 22.0f;
                if (d.id == selected) {
                    r.drawRect(x + 2, iy, w - 4, 22.0f, Color{65, 100, 160, 255});
                } else if (m_popupHover == idx) {
                    r.drawRect(x + 2, iy, w - 4, 22.0f, Color{60, 60, 70, 255});
                }
                f.drawText(r, d.name.c_str(), x + 8, iy + (22.0f - th) * 0.5f, ts, Theme::textPrimary);
                idx++;
            }
        }
#else
        (void)r; (void)f; (void)x; (void)y; (void)w; (void)rh; (void)th; (void)ts;
        (void)devices; (void)selected; (void)outputOnly; (void)isOpen;
#endif
    }

    bool handleDropdownClick(float mx, float my, float x, float y,
                             float w, float h, int count, int /*selected*/,
                             bool* isOpen, std::function<void(int)> onSelect) {
        if (mx >= x && mx <= x + w && my >= y && my < y + h) {
            *isOpen = !*isOpen;
            m_popupHover = -1;
            return true;
        }
        if (*isOpen) {
            float popupY = y + h;
            for (int i = 0; i < count; ++i) {
                float iy = popupY + i * 22.0f;
                if (mx >= x && mx <= x + w && my >= iy && my < iy + 22.0f) {
                    onSelect(i);
                    *isOpen = false;
                    return true;
                }
            }
            *isOpen = false;
            return true;
        }
        return false;
    }

    bool handleDeviceDropdownClick(float mx, float my, float x, float y,
                                    float w, float h,
                                    const std::vector<audio::AudioDevice>& devices,
                                    int& selected, bool outputOnly, bool* isOpen) {
        if (mx >= x && mx <= x + w && my >= y && my < y + h) {
            *isOpen = !*isOpen;
            m_popupHover = -1;
            return true;
        }
        if (*isOpen) {
            float popupY = y + h;
            int idx = 0;
            for (auto& d : devices) {
                if (outputOnly && d.maxOutputChannels == 0) continue;
                if (!outputOnly && d.maxInputChannels == 0) continue;
                float iy = popupY + idx * 22.0f;
                if (mx >= x && mx <= x + w && my >= iy && my < iy + 22.0f) {
                    selected = d.id;
                    *isOpen = false;
                    return true;
                }
                idx++;
            }
            *isOpen = false;
            return true;
        }
        return false;
    }

    // ─── Data ───────────────────────────────────────────────────────────

    State m_state;
    audio::AudioEngine* m_engine = nullptr;
    midi::MidiEngine* m_midiEngine = nullptr;
    std::vector<audio::AudioDevice> m_outputDevices;
    std::vector<std::string> m_midiInputs;
    std::vector<std::string> m_midiOutputs;

    int m_tab = 0;
    float m_screenW = 0, m_screenH = 0;
    float m_dx = 0, m_dy = 0;
    int m_popupHover = -1;

    bool m_audioOutputOpen = false;
    bool m_audioInputOpen = false;
    bool m_sampleRateOpen = false;
    bool m_bufferSizeOpen = false;
    bool m_launchQOpen = false;
    bool m_recordQOpen = false;
};

} // namespace fw
} // namespace ui
} // namespace yawn
