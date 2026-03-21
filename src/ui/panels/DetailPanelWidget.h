#pragma once
// DetailPanelWidget — Framework widget replacement for DetailPanel.
//
// fw::Widget that renders the device chain detail panel (MIDI effects →
// Instrument → Audio effects) with expandable device sub-panels, knob
// grids, and visualiser displays.  Handles mouse interaction for knob
// dragging, bypass toggling, expand/collapse, and device removal.
//
// Right-click is still forwarded directly from App.cpp because the widget
// tree doesn't dispatch right-click events yet.

#include "ui/framework/Widget.h"
#include "ui/Theme.h"
#include "instruments/Instrument.h"
#include "effects/AudioEffect.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffect.h"
#include "midi/MidiEffectChain.h"
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace yawn {
namespace ui {
namespace fw {

class DetailPanelWidget : public Widget {
public:
    // Layout constants
    static constexpr float kPanelHeight      = 220.0f;
    static constexpr float kCollapsedHeight  = 28.0f;
    static constexpr float kHeaderHeight     = 28.0f;   // main panel header
    static constexpr float kDeviceHeaderH    = 24.0f;   // per-device header
    static constexpr float kKnobSize         = 48.0f;
    static constexpr float kKnobSpacing      = 16.0f;
    static constexpr int   kMaxKnobRows      = 2;
    static constexpr float kCollapsedDeviceW = 60.0f;
    static constexpr float kMinExpandedW     = 180.0f;
    static constexpr float kScrollBtnW       = 28.0f;
    static constexpr float kBtnSize          = 16.0f;
    static constexpr float kDeviceGap        = 4.0f;
    static constexpr float kVisualizerMinW   = 300.0f;

    enum class DeviceType { MidiFx, Instrument, AudioFx };

    // --- Public API ---

    bool  isOpen() const { return m_open; }
    void  setOpen(bool open) { m_open = open; }
    void  toggle() { m_open = !m_open; }
    float height() const { return m_open ? kPanelHeight : kCollapsedHeight; }

    bool isFocused() const { return m_panelFocused; }
    void setFocused(bool f) { m_panelFocused = f; }

    void setOnRemoveDevice(std::function<void(DeviceType, int)> cb) {
        m_onRemoveDevice = std::move(cb);
    }

    // Rebuild the device chain from current track state.
    // Called each frame from updateDetailForSelectedTrack().
    void setDeviceChain(midi::MidiEffectChain* midiChain,
                        instruments::Instrument* inst,
                        effects::EffectChain* fxChain) {
        int midiCount = midiChain ? midiChain->count() : 0;
        int fxCount   = fxChain   ? fxChain->count()   : 0;

        // Quick fingerprint — skip rebuild if nothing changed
        if (midiChain == m_lastMidiChain && inst == m_lastInst &&
            fxChain == m_lastFxChain &&
            midiCount == m_lastMidiCount && fxCount == m_lastFxCount)
            return;

        std::vector<DevicePanel> newDevices;

        // MIDI effects
        if (midiChain) {
            for (int i = 0; i < midiChain->count(); ++i) {
                auto* fx = midiChain->effect(i);
                if (!fx) continue;
                DevicePanel dp;
                dp.type = DeviceType::MidiFx;
                dp.midiEffect = fx;
                dp.chainIndex = i;
                dp.expanded = findPrevExpanded(fx);
                dp.rebuildParams();
                newDevices.push_back(std::move(dp));
            }
        }

        // Instrument
        if (inst) {
            DevicePanel dp;
            dp.type = DeviceType::Instrument;
            dp.instrument = inst;
            dp.chainIndex = 0;
            dp.expanded = findPrevExpandedInst(inst);
            dp.rebuildParams();
            newDevices.push_back(std::move(dp));
        }

        // Audio effects
        if (fxChain) {
            for (int i = 0; i < fxChain->count(); ++i) {
                auto* fx = fxChain->effectAt(i);
                if (!fx) continue;
                DevicePanel dp;
                dp.type = DeviceType::AudioFx;
                dp.audioEffect = fx;
                dp.chainIndex = i;
                dp.expanded = findPrevExpanded(fx);
                dp.rebuildParams();
                newDevices.push_back(std::move(dp));
            }
        }

        m_devices = std::move(newDevices);
        m_lastMidiChain = midiChain;
        m_lastInst      = inst;
        m_lastFxChain   = fxChain;
        m_lastMidiCount = midiCount;
        m_lastFxCount   = fxCount;

        m_dragging     = false;
        m_activeDevice = -1;
        m_activeParam  = -1;

        // Auto-open when devices are populated
        if (!m_devices.empty()) m_open = true;
    }

    void clear() {
        m_devices.clear();
        m_lastMidiChain = nullptr;
        m_lastInst      = nullptr;
        m_lastFxChain   = nullptr;
        m_lastMidiCount = 0;
        m_lastFxCount   = 0;
        m_scrollX       = 0;
        m_dragging      = false;
        m_activeDevice  = -1;
        m_activeParam   = -1;
    }

    const char* title() const {
        if (m_devices.empty()) return "Detail";
        return "Detail";
    }

    bool isDragging() const { return m_dragging; }

    // Snap-scroll left (previous device)
    void scrollLeft() {
        auto pos = deviceStartPositions();
        for (int i = (int)pos.size() - 2; i >= 0; --i) {
            if (pos[i] < m_scrollX - 0.5f) {
                m_scrollX = pos[i];
                return;
            }
        }
        m_scrollX = 0;
    }

    // Snap-scroll right (next device)
    void scrollRight() {
        auto pos = deviceStartPositions();
        for (int i = 0; i < (int)pos.size() - 1; ++i) {
            if (pos[i] > m_scrollX + 0.5f) {
                m_scrollX = pos[i];
                return;
            }
        }
    }

    // Keyboard handler (Left/Right arrows)
    void handleKeyDown([[maybe_unused]] int key) {
#ifndef YAWN_TEST_BUILD
        if (!m_open || !m_panelFocused) return;
        if (key == 0x40000050 /*SDL LEFT*/)  scrollLeft();
        if (key == 0x4000004F /*SDL RIGHT*/) scrollRight();
#endif
    }

    // Mouse wheel
    void handleScroll(float dx, [[maybe_unused]] float dy) {
        m_scrollX -= dx * 30.0f;
        if (std::abs(dx) < 0.01f)
            m_scrollX -= dy * 30.0f;
        m_scrollX = std::max(0.0f, m_scrollX);
    }

    // Right-click — kept as public method (App.cpp calls directly)
    bool handleRightClick(float mx, float my) {
        float panelX = m_bounds.x, panelY = m_bounds.y, panelW = m_bounds.w;

        if (!m_open || my < panelY + kHeaderHeight) return false;
        if (mx < panelX || mx > panelX + panelW) return false;

        float bodyY = panelY + kHeaderHeight;
        float bodyH = height() - kHeaderHeight;
        float totalW = totalDeviceWidth();
        bool canScroll = totalW > panelW;
        float deviceAreaX = canScroll ? panelX + kScrollBtnW : panelX;

        float dx = deviceAreaX - m_scrollX;
        for (int di = 0; di < (int)m_devices.size(); ++di) {
            auto& dev = m_devices[di];
            float dw = dev.deviceWidth();
            if (mx >= dx && mx < dx + dw && my >= bodyY && my < bodyY + bodyH) {
                if (!dev.expanded) return true;
                return handleKnobRightClick(dev, mx, my, dx, bodyY, dw, bodyH);
            }
            dx += dw + kDeviceGap;
        }
        return false;
    }

    // ─── Measure / Layout ───────────────────────────────────────────────

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, height()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    // ─── Rendering ──────────────────────────────────────────────────────

    void paint([[maybe_unused]] UIContext& ctx) override {
#ifndef YAWN_TEST_BUILD
        auto& renderer = *ctx.renderer;
        auto& font = *ctx.font;
        float x = m_bounds.x, y = m_bounds.y, width = m_bounds.w;

        float h = height();

        // Header bar
        renderer.drawRect(x, y, width, kHeaderHeight, Color{35, 35, 40, 255});
        renderer.drawRect(x, y, width, 1, Color{55, 55, 60, 255});
        float hScale = 14.0f / Theme::kFontSize;
        const char* arrow = m_open ? "\xe2\x96\xbc" : "\xe2\x96\xb6";
        font.drawText(renderer, arrow, x + 8, y + 6, hScale, Theme::textSecondary);
        font.drawText(renderer, title(), x + 26, y + 6, hScale, Theme::textPrimary);

        if (!m_open) return;

        float bodyY = y + kHeaderHeight;
        float bodyH = h - kHeaderHeight;
        renderer.drawRect(x, bodyY, width, bodyH, Color{28, 28, 32, 255});

        if (m_devices.empty()) {
            font.drawText(renderer, "No devices on track",
                          x + 20, bodyY + 20, hScale, Theme::textDim);
            return;
        }

        float totalW = totalDeviceWidth();
        bool canScroll = totalW > width;
        float maxScroll = std::max(0.0f, totalW - (canScroll ? width - 2*kScrollBtnW : width));
        m_scrollX = std::clamp(m_scrollX, 0.0f, maxScroll);

        float deviceAreaX = canScroll ? x + kScrollBtnW : x;
        float deviceAreaW = canScroll ? width - 2*kScrollBtnW : width;

        // < > scroll buttons
        if (canScroll) {
            Color lc = m_scrollX > 0 ? Color{50, 50, 60, 255} : Color{35, 35, 40, 255};
            renderer.drawRect(x, bodyY, kScrollBtnW, bodyH, lc);
            Color lt = m_scrollX > 0 ? Theme::textPrimary : Theme::textDim;
            font.drawText(renderer, "<", x + 9, bodyY + bodyH*0.5f - 8, hScale, lt);

            float rx = x + width - kScrollBtnW;
            Color rc = m_scrollX < maxScroll ? Color{50, 50, 60, 255} : Color{35, 35, 40, 255};
            renderer.drawRect(rx, bodyY, kScrollBtnW, bodyH, rc);
            Color rt = m_scrollX < maxScroll ? Theme::textPrimary : Theme::textDim;
            font.drawText(renderer, ">", rx + 9, bodyY + bodyH*0.5f - 8, hScale, rt);
        }

        renderer.pushClip(deviceAreaX, bodyY, deviceAreaW, bodyH);

        float dx = deviceAreaX - m_scrollX;
        for (int di = 0; di < (int)m_devices.size(); ++di) {
            auto& dev = m_devices[di];
            float dw = dev.deviceWidth();

            if (dx + dw >= deviceAreaX && dx <= deviceAreaX + deviceAreaW) {
                renderDevice(renderer, font, dev, di, dx, bodyY, dw, bodyH);
            }
            dx += dw + kDeviceGap;
        }

        renderer.popClip();
#endif
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        float mx = e.x, my = e.y;
        float panelX = m_bounds.x, panelY = m_bounds.y, panelW = m_bounds.w;

        if (my < panelY || my > panelY + height()) return false;
        if (mx < panelX || mx > panelX + panelW) return false;

        m_panelFocused = true;

        // Header bar toggles panel
        if (my < panelY + kHeaderHeight) {
            toggle();
            return true;
        }

        if (!m_open || m_devices.empty()) return false;

        float bodyY = panelY + kHeaderHeight;
        float bodyH = height() - kHeaderHeight;
        float totalW = totalDeviceWidth();
        bool canScroll = totalW > panelW;
        float deviceAreaX = canScroll ? panelX + kScrollBtnW : panelX;
        float deviceAreaW = canScroll ? panelW - 2 * kScrollBtnW : panelW;
        (void)deviceAreaW;

        // < button
        if (canScroll && mx < panelX + kScrollBtnW) {
            scrollLeft();
            return true;
        }
        // > button
        if (canScroll && mx >= panelX + panelW - kScrollBtnW) {
            scrollRight();
            return true;
        }

        // Find device
        float dx = deviceAreaX - m_scrollX;
        for (int di = 0; di < (int)m_devices.size(); ++di) {
            float dw = m_devices[di].deviceWidth();
            if (mx >= dx && mx < dx + dw && my >= bodyY && my < bodyY + bodyH) {
                return handleDeviceClick(m_devices[di], di, mx, my, dx, bodyY, dw, bodyH);
            }
            dx += dw + kDeviceGap;
        }
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        float my = e.y;

        if (!m_dragging || m_activeDevice < 0 ||
            m_activeDevice >= (int)m_devices.size())
            return false;

        auto& dev = m_devices[m_activeDevice];
        auto it = std::find_if(dev.params.begin(), dev.params.end(),
            [this](const ParamEntry& p) { return p.index == m_activeParam; });
        if (it == dev.params.end()) return false;

        float range = it->maxVal - it->minVal;
        float deltaY = m_dragStartY - my;
        float sensitivity = range / 150.0f;
        float newVal = std::clamp(m_dragStartValue + deltaY * sensitivity,
                                  it->minVal, it->maxVal);
        dev.setParam(m_activeParam, newVal);
        return true;
    }

    bool onMouseUp(MouseEvent&) override {
        if (!m_dragging) return false;
        m_dragging     = false;
        m_activeDevice = -1;
        m_activeParam  = -1;
        releaseMouse();
        return true;
    }

private:
    // ── Param entry (normalized from all 3 device param types) ──
    struct ParamEntry {
        int index;
        std::string name;
        std::string unit;
        float minVal, maxVal, defaultVal;
        bool isBoolean;
    };

    // ── Device panel ──
    struct DevicePanel {
        DeviceType type = DeviceType::AudioFx;
        midi::MidiEffect*        midiEffect  = nullptr;
        instruments::Instrument*  instrument  = nullptr;
        effects::AudioEffect*     audioEffect = nullptr;
        int  chainIndex = -1;
        bool expanded   = true;
        std::vector<ParamEntry> params;

        const char* deviceName() const {
            if (midiEffect)  return midiEffect->name();
            if (instrument)  return instrument->name();
            if (audioEffect) return audioEffect->name();
            return "?";
        }

        float getParam(int i) const {
            if (midiEffect)  return midiEffect->getParameter(i);
            if (instrument)  return instrument->getParameter(i);
            if (audioEffect) return audioEffect->getParameter(i);
            return 0.0f;
        }
        void setParam(int i, float v) {
            if (midiEffect)       midiEffect->setParameter(i, v);
            else if (instrument)  instrument->setParameter(i, v);
            else if (audioEffect) audioEffect->setParameter(i, v);
        }

        bool isBypassed() const {
            if (midiEffect)  return midiEffect->bypassed();
            if (instrument)  return instrument->bypassed();
            if (audioEffect) return audioEffect->bypassed();
            return false;
        }
        void toggleBypass() {
            if (midiEffect)       midiEffect->setBypassed(!midiEffect->bypassed());
            else if (instrument)  instrument->setBypassed(!instrument->bypassed());
            else if (audioEffect) audioEffect->setBypassed(!audioEffect->bypassed());
        }

        bool isRemovable() const { return type != DeviceType::Instrument; }
        bool isVisualizer() const { return audioEffect && audioEffect->isVisualizer(); }

        void rebuildParams() {
            params.clear();
            int count = 0;
            if (midiEffect)       count = midiEffect->parameterCount();
            else if (instrument)  count = instrument->parameterCount();
            else if (audioEffect) count = audioEffect->parameterCount();

            for (int i = 0; i < count; ++i) {
                ParamEntry e;
                e.index = i;
                if (midiEffect) {
                    auto& info = midiEffect->parameterInfo(i);
                    e.name = info.name; e.unit = info.unit;
                    e.minVal = info.minValue; e.maxVal = info.maxValue;
                    e.defaultVal = info.defaultValue; e.isBoolean = info.isBoolean;
                } else if (instrument) {
                    auto& info = instrument->parameterInfo(i);
                    e.name = info.name; e.unit = info.unit;
                    e.minVal = info.minValue; e.maxVal = info.maxValue;
                    e.defaultVal = info.defaultValue; e.isBoolean = info.isBoolean;
                } else if (audioEffect) {
                    auto& info = audioEffect->parameterInfo(i);
                    e.name = info.name; e.unit = info.unit;
                    e.minVal = info.minValue; e.maxVal = info.maxValue;
                    e.defaultVal = info.defaultValue; e.isBoolean = info.isBoolean;
                }
                params.push_back(std::move(e));
            }
        }

        float deviceWidth() const {
            if (!expanded) return kCollapsedDeviceW;
            if (isVisualizer()) {
                int cols = params.empty() ? 0
                    : (int)std::ceil((float)params.size() / 1.0f);
                float knobW = cols * (kKnobSize + kKnobSpacing) + 16.0f;
                return std::max(kVisualizerMinW, knobW);
            }
            int cols = params.empty() ? 1
                : (int)std::ceil((float)params.size() / (float)kMaxKnobRows);
            float w = cols * (kKnobSize + kKnobSpacing) + 24.0f;
            return std::max(kMinExpandedW, w);
        }
    };

    // ── Color helpers ──
    static Color deviceColor(DeviceType t) {
        switch (t) {
            case DeviceType::MidiFx:     return Color{140, 80, 200, 255};
            case DeviceType::Instrument: return Color{60, 120, 200, 255};
            case DeviceType::AudioFx:    return Color{60, 180, 100, 255};
        }
        return Color{100, 100, 100, 255};
    }
    static Color deviceColorDim(DeviceType t) {
        auto c = deviceColor(t);
        return Color{(uint8_t)(c.r/2), (uint8_t)(c.g/2), (uint8_t)(c.b/2), 200};
    }

    // ── Preserve expanded state across rebuilds ──
    bool findPrevExpanded(midi::MidiEffect* ptr) const {
        if (!ptr) return true;
        for (auto& d : m_devices) {
            if (d.midiEffect == ptr) return d.expanded;
        }
        return true;
    }
    bool findPrevExpanded(effects::AudioEffect* ptr) const {
        if (!ptr) return true;
        for (auto& d : m_devices) {
            if (d.audioEffect == ptr) return d.expanded;
        }
        return true;
    }
    bool findPrevExpandedInst(instruments::Instrument* ptr) const {
        for (auto& d : m_devices) {
            if (d.instrument == ptr) return d.expanded;
        }
        return true;
    }

    // ── Device start positions (for snap scroll) ──
    std::vector<float> deviceStartPositions() const {
        std::vector<float> pos;
        float x = 0;
        for (auto& d : m_devices) {
            pos.push_back(x);
            x += d.deviceWidth() + kDeviceGap;
        }
        pos.push_back(x);
        return pos;
    }

    float totalDeviceWidth() const {
        float w = 0;
        for (auto& d : m_devices) w += d.deviceWidth() + kDeviceGap;
        return w;
    }

    // ── Render helpers ──

    void renderDevice([[maybe_unused]] Renderer2D& renderer,
                      [[maybe_unused]] Font& font,
                      [[maybe_unused]] DevicePanel& dev,
                      [[maybe_unused]] int deviceIdx,
                      [[maybe_unused]] float x, [[maybe_unused]] float y,
                      [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        // Background
        Color bg = dev.isBypassed() ? Color{30, 30, 34, 255} : Color{36, 36, 42, 255};
        renderer.drawRect(x, y, w, h, bg);

        // Color stripe at top
        Color stripe = dev.isBypassed() ? deviceColorDim(dev.type) : deviceColor(dev.type);
        renderer.drawRect(x, y, w, 3, stripe);

        float sml = 10.0f / Theme::kFontSize;
        float med = 12.0f / Theme::kFontSize;
        float hy  = y + 5;

        // ▶/▼ toggle
        const char* tog = dev.expanded ? "\xe2\x96\xbc" : "\xe2\x96\xb6";
        font.drawText(renderer, tog, x + 4, hy, sml, Theme::textSecondary);

        // On/Off bypass indicator
        float bpX = x + 18;
        Color bpBg  = dev.isBypassed() ? Color{100, 40, 40, 255} : Color{40, 100, 40, 255};
        Color bpTxt = dev.isBypassed() ? Color{220, 120, 120, 255} : Color{120, 220, 120, 255};
        renderer.drawRect(bpX, hy, kBtnSize, kBtnSize, bpBg);
        font.drawText(renderer, dev.isBypassed() ? "Off" : "On",
                      bpX + 1, hy + 3, 9.0f / Theme::kFontSize, bpTxt);

        // Device name
        float nameX = bpX + kBtnSize + 4;
        Color nameC = dev.isBypassed() ? Theme::textDim : Theme::textPrimary;
        font.drawText(renderer, dev.deviceName(), nameX, hy, med, nameC);

        // ✕ remove button
        if (dev.isRemovable()) {
            float rmX = x + w - kBtnSize - 4;
            renderer.drawRect(rmX, hy, kBtnSize, kBtnSize, Color{60, 30, 30, 255});
            font.drawText(renderer, "X", rmX + 4, hy + 3, sml, Color{200, 100, 100, 255});
        }

        if (!dev.expanded) {
            // Show truncated name vertically in collapsed body
            float ty = y + kDeviceHeaderH + 4;
            const char* nm = dev.deviceName();
            char buf[2] = {0, 0};
            for (int ci = 0; nm[ci] && ty < y + h - 10; ++ci) {
                buf[0] = nm[ci];
                font.drawText(renderer, buf, x + w*0.5f - 3, ty, sml, Theme::textDim);
                ty += 12;
            }
            return;
        }

        // Body
        float bodyY = y + kDeviceHeaderH;
        float bodyH = h - kDeviceHeaderH;

        if (dev.isVisualizer()) {
            renderVisualizerBody(renderer, font, dev, deviceIdx, x, bodyY, w, bodyH);
        } else {
            renderKnobGrid(renderer, font, dev, deviceIdx,
                           x + 8, bodyY + 4, w - 16, bodyH - 8, kMaxKnobRows);
        }
#endif
    }

    void renderVisualizerBody([[maybe_unused]] Renderer2D& renderer,
                              [[maybe_unused]] Font& font,
                              [[maybe_unused]] DevicePanel& dev,
                              [[maybe_unused]] int deviceIdx,
                              [[maybe_unused]] float x, [[maybe_unused]] float y,
                              [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        float knobRowH = 68.0f;
        float vizH = h - knobRowH;

        float vizX = x + 4, vizY = y + 2, vizW = w - 8;
        renderer.drawRect(vizX, vizY, vizW, vizH - 4, Color{18, 18, 22, 255});
        renderer.drawRectOutline(vizX, vizY, vizW, vizH - 4, Color{50, 50, 60, 255});

        const float* data = dev.audioEffect->displayData();
        int dataSize = dev.audioEffect->displaySize();
        if (data && dataSize > 0) {
            const char* vt = dev.audioEffect->visualizerType();
            if (std::strcmp(vt, "oscilloscope") == 0)
                renderOscilloscope(renderer, data, dataSize,
                                   vizX+2, vizY+2, vizW-4, vizH-8);
            else if (std::strcmp(vt, "spectrum") == 0)
                renderSpectrum(renderer, data, dataSize,
                               vizX+2, vizY+2, vizW-4, vizH-8);
        }

        // Single row of knobs below
        renderKnobGrid(renderer, font, dev, deviceIdx,
                        x + 8, y + vizH + 2, w - 16, knobRowH - 4, 1);
#endif
    }

    void renderKnobGrid([[maybe_unused]] Renderer2D& renderer,
                        [[maybe_unused]] Font& font,
                        [[maybe_unused]] DevicePanel& dev,
                        [[maybe_unused]] int deviceIdx,
                        [[maybe_unused]] float x, [[maybe_unused]] float y,
                        [[maybe_unused]] float w, [[maybe_unused]] float h,
                        [[maybe_unused]] int maxRows) {
#ifndef YAWN_TEST_BUILD
        if (dev.params.empty()) return;

        float cellW = kKnobSize + kKnobSpacing;
        float cellH = kKnobSize + 22.0f;
        int cols = (int)std::ceil((float)dev.params.size() / (float)maxRows);

        for (int i = 0; i < (int)dev.params.size(); ++i) {
            auto& p = dev.params[i];
            int row = i / cols;
            int col = i % cols;
            float kx = x + col * cellW;
            float ky = y + row * cellH;

            if (kx + kKnobSize > x + w || ky + cellH > y + h) continue;

            float value = dev.getParam(p.index);
            float norm = (value - p.minVal) / (p.maxVal - p.minVal);
            norm = std::clamp(norm, 0.0f, 1.0f);

            float cx = kx + kKnobSize * 0.5f;
            float cy = ky + kKnobSize * 0.4f;
            float r  = kKnobSize * 0.35f;

            renderArc(renderer, cx, cy, r, 0.0f, 1.0f, Color{50, 50, 55, 255});

            bool isActive = (deviceIdx == m_activeDevice && p.index == m_activeParam);
            Color arcCol = isActive ? Color{120, 200, 255, 255}
                                    : Color{80, 160, 210, 255};
            if (p.isBoolean) {
                arcCol = value > 0.5f ? Color{80, 200, 80, 255}
                                      : Color{80, 80, 85, 255};
                norm = value > 0.5f ? 1.0f : 0.0f;
            }
            renderArc(renderer, cx, cy, r, 0.0f, norm, arcCol);

            float angle = (float)(-M_PI * 0.75 + norm * M_PI * 1.5);
            float dotX = cx + std::cos(angle) * r * 0.65f - 2.0f;
            float dotY = cy + std::sin(angle) * r * 0.65f - 2.0f;
            renderer.drawRect(dotX, dotY, 4, 4, Theme::textPrimary);

            char valBuf[32];
            formatValue(p, value, valBuf, sizeof(valBuf));
            float valScale = 10.0f / Theme::kFontSize;
            float valW = font.textWidth(valBuf, valScale);
            font.drawText(renderer, valBuf,
                          kx + (kKnobSize - valW) * 0.5f,
                          ky + kKnobSize * 0.72f, valScale, Theme::textSecondary);

            float nameScale = 10.0f / Theme::kFontSize;
            float nameW = font.textWidth(p.name.c_str(), nameScale);
            float maxLabelW = cellW - 2.0f;
            font.drawText(renderer, p.name.c_str(),
                          kx + (kKnobSize - std::min(nameW, maxLabelW)) * 0.5f,
                          ky + kKnobSize + 2, nameScale, Theme::textDim);
        }
#endif
    }

    // ── Click helpers ──

    bool handleDeviceClick(DevicePanel& dev, int deviceIdx,
                           float mx, float my,
                           float dx, float dy, float dw, float dh) {
        float hy = dy + 5;

        // Toggle expand/collapse
        if (mx >= dx && mx < dx + 18 && my >= hy && my < hy + kBtnSize) {
            dev.expanded = !dev.expanded;
            return true;
        }
        // Bypass toggle
        float bpX = dx + 18;
        if (mx >= bpX && mx < bpX + kBtnSize && my >= hy && my < hy + kBtnSize) {
            dev.toggleBypass();
            return true;
        }
        // Remove
        if (dev.isRemovable()) {
            float rmX = dx + dw - kBtnSize - 4;
            if (mx >= rmX && mx < rmX + kBtnSize && my >= hy && my < hy + kBtnSize) {
                if (m_onRemoveDevice) m_onRemoveDevice(dev.type, dev.chainIndex);
                return true;
            }
        }

        if (!dev.expanded) return true;

        // Knob clicks in body
        float bodyY = dy + kDeviceHeaderH;
        float bodyH = dh - kDeviceHeaderH;

        if (dev.isVisualizer()) {
            float knobRowH = 68.0f;
            float vizH = bodyH - knobRowH;
            return handleKnobClick(dev, deviceIdx, mx, my,
                                   dx + 8, bodyY + vizH + 2, dw - 16, knobRowH - 4, 1);
        }
        return handleKnobClick(dev, deviceIdx, mx, my,
                               dx + 8, bodyY + 4, dw - 16, bodyH - 8, kMaxKnobRows);
    }

    bool handleKnobClick(DevicePanel& dev, int deviceIdx,
                         float mx, float my,
                         float x, float y, float w, float h,
                         int maxRows) {
        if (dev.params.empty()) return true;

        float cellW = kKnobSize + kKnobSpacing;
        float cellH = kKnobSize + 22.0f;
        int cols = (int)std::ceil((float)dev.params.size() / (float)maxRows);

        for (int i = 0; i < (int)dev.params.size(); ++i) {
            auto& p = dev.params[i];
            int row = i / cols;
            int col = i % cols;
            float kx = x + col * cellW;
            float ky = y + row * cellH;

            if (mx >= kx && mx < kx + kKnobSize &&
                my >= ky && my < ky + cellH) {
                if (p.isBoolean) {
                    float cur = dev.getParam(p.index);
                    dev.setParam(p.index, cur > 0.5f ? 0.0f : 1.0f);
                } else {
                    m_dragging      = true;
                    m_activeDevice  = deviceIdx;
                    m_activeParam   = p.index;
                    m_dragStartY    = my;
                    m_dragStartValue = dev.getParam(p.index);
                    captureMouse();
                }
                return true;
            }
        }
        return true;
    }

    bool handleKnobRightClick(DevicePanel& dev,
                              float mx, float my,
                              float dx, float dy, float dw, float dh) {
        float bodyY = dy + kDeviceHeaderH;
        float bodyH = dh - kDeviceHeaderH;
        int maxRows = kMaxKnobRows;
        float kx = dx + 8, ky = bodyY + 4, kw = dw - 16, kh = bodyH - 8;
        if (dev.isVisualizer()) {
            float knobRowH = 68.0f;
            float vizH = bodyH - knobRowH;
            maxRows = 1;
            ky = bodyY + vizH + 2;
            kh = knobRowH - 4;
        }

        if (dev.params.empty()) return true;
        float cellW = kKnobSize + kKnobSpacing;
        float cellH = kKnobSize + 22.0f;
        int cols = (int)std::ceil((float)dev.params.size() / (float)maxRows);

        for (int i = 0; i < (int)dev.params.size(); ++i) {
            auto& p = dev.params[i];
            int row = i / cols;
            int col = i % cols;
            float px = kx + col * cellW;
            float py = ky + row * cellH;

            if (mx >= px && mx < px + kKnobSize && my >= py && my < py + cellH) {
                dev.setParam(p.index, p.defaultVal);
                return true;
            }
        }
        return false;
    }

    // ── Utility ──

    void formatValue(const ParamEntry& p, float value, char* buf, int bufSize) const {
        if (p.isBoolean) {
            std::snprintf(buf, bufSize, "%s", value > 0.5f ? "On" : "Off");
        } else if (p.unit == "Hz") {
            if (value >= 1000.0f) std::snprintf(buf, bufSize, "%.1fk", value / 1000.0f);
            else std::snprintf(buf, bufSize, "%.0f", value);
        } else if (p.unit == "dB") {
            std::snprintf(buf, bufSize, "%.1f", value);
        } else if (p.unit == "ms") {
            std::snprintf(buf, bufSize, "%.0f", value);
        } else if (p.unit == "%") {
            std::snprintf(buf, bufSize, "%.0f%%", value * 100.0f);
        } else {
            std::snprintf(buf, bufSize, "%.2f", value);
        }
    }

    void renderArc([[maybe_unused]] Renderer2D& renderer,
                   [[maybe_unused]] float cx, [[maybe_unused]] float cy,
                   [[maybe_unused]] float r,
                   [[maybe_unused]] float startN, [[maybe_unused]] float endN,
                   [[maybe_unused]] Color color) {
#ifndef YAWN_TEST_BUILD
        const int segs = 24;
        float sa = (float)(-M_PI * 0.75 + startN * M_PI * 1.5);
        float ea = (float)(-M_PI * 0.75 + endN   * M_PI * 1.5);
        float step = (ea - sa) / segs;
        for (int i = 0; i < segs; ++i) {
            float a = sa + step * (i + 0.5f);
            float px = cx + std::cos(a) * r - 2.0f;
            float py = cy + std::sin(a) * r - 2.0f;
            renderer.drawRect(px, py, 4, 4, color);
        }
#endif
    }

    void renderOscilloscope([[maybe_unused]] Renderer2D& renderer,
                            [[maybe_unused]] const float* data,
                            [[maybe_unused]] int dataSize,
                            [[maybe_unused]] float x, [[maybe_unused]] float y,
                            [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        float midY = y + h * 0.5f;
        renderer.drawRect(x, midY, w, 1, Color{40, 40, 50, 255});
        renderer.drawRect(x, midY - h * 0.25f, w, 1, Color{30, 30, 38, 255});
        renderer.drawRect(x, midY + h * 0.25f, w, 1, Color{30, 30, 38, 255});

        Color waveColor{80, 220, 120, 255};
        float xStep = w / (float)dataSize;
        for (int i = 0; i < dataSize; ++i) {
            float sample = std::clamp(data[i], -1.0f, 1.0f);
            float px = x + i * xStep;
            float py = midY - sample * (h * 0.45f);
            renderer.drawRect(px, py - 1, std::max(xStep, 1.0f), 2, waveColor);
        }
#endif
    }

    void renderSpectrum([[maybe_unused]] Renderer2D& renderer,
                        [[maybe_unused]] const float* data,
                        [[maybe_unused]] int dataSize,
                        [[maybe_unused]] float x, [[maybe_unused]] float y,
                        [[maybe_unused]] float w, [[maybe_unused]] float h) {
#ifndef YAWN_TEST_BUILD
        for (int db = 1; db <= 3; ++db) {
            float gy = y + h * (1.0f - (float)(96 - db * 24) / 96.0f);
            renderer.drawRect(x, gy, w, 1, Color{30, 30, 38, 255});
        }
        int numBars = std::min((int)w, dataSize);
        float barW = std::max(w / (float)numBars, 1.0f);
        for (int i = 0; i < numBars; ++i) {
            float t = (float)i / (float)numBars;
            float logIdx = std::pow(t, 2.0f) * (dataSize - 1);
            int idx = std::clamp((int)logIdx, 0, dataSize - 1);
            float mag = data[idx];
            float barH = mag * (h - 4);
            if (barH < 1.0f) continue;
            float bx = x + i * barW;
            float by = y + h - barH - 2;
            uint8_t r = (uint8_t)(60 + t * 180);
            uint8_t g = (uint8_t)(200 - t * 160);
            uint8_t b = 220;
            renderer.drawRect(bx, by, std::max(barW - 1.0f, 1.0f), barH,
                              Color{r, g, b, 230});
        }
#endif
    }

    // ── State ──
    std::vector<DevicePanel> m_devices;
    float m_scrollX = 0;
    bool  m_panelFocused = false;
    bool  m_open    = false;

    // Knob drag
    bool  m_dragging      = false;
    int   m_activeDevice  = -1;
    int   m_activeParam   = -1;
    float m_dragStartY    = 0;
    float m_dragStartValue = 0;

    // Remove callback
    std::function<void(DeviceType, int)> m_onRemoveDevice;

    // Fingerprint cache for setDeviceChain
    midi::MidiEffectChain*       m_lastMidiChain = nullptr;
    instruments::Instrument*     m_lastInst      = nullptr;
    effects::EffectChain*        m_lastFxChain   = nullptr;
    int m_lastMidiCount = 0;
    int m_lastFxCount   = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
