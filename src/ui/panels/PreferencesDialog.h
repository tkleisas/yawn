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

        // Metronome
        float metronomeVolume = 0.7f;
        int metronomeMode = 0;    // 0=Always, 1=RecordOnly, 2=PlayOnly, 3=Off
        int countInBars = 0;      // 0, 1, 2, or 4
    };

    PreferencesDialog()
        : Dialog("Preferences", 540, 460)
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

#ifdef YAWN_TEST_BUILD
    void paint(UIContext&) override {}
#else
    void paint(UIContext& ctx) override;
#endif

    // ─── Events ─────────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    bool onMouseDown(MouseEvent&) override { return true; }
#else
    bool onMouseDown(MouseEvent& e) override;
#endif

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
        m_midiInputs  = midi::MidiPort::enumerateInputPorts();
        m_midiOutputs = midi::MidiPort::enumerateOutputPorts();
    }

    // ─── Audio Tab ──────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintAudioTab(UIContext&, float, float, float, float,
                       float, float) {}
#else
    void paintAudioTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                       float ts, float th);
#endif

#ifdef YAWN_TEST_BUILD
    void handleAudioClick(float, float, float, float,
                          float, float, float) {}
#else
    void handleAudioClick(float mx, float my, float cx, float cy,
                          float dropX, float dropW, float rh);
#endif

    // ─── MIDI Tab ───────────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintMidiTab(UIContext&, float, float, float, float,
                      float, float) {}
#else
    void paintMidiTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                      float ts, float th);
#endif

#ifdef YAWN_TEST_BUILD
    void handleMidiClick(float, float, float, float,
                         float, float) {}
#else
    void handleMidiClick(float mx, float my, float cx, float cy,
                         float cw, float rh);
#endif

    // ─── Defaults Tab ───────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintDefaultsTab(UIContext&, float, float, float, float,
                          float, float) {}
#else
    void paintDefaultsTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                          float ts, float th);
#endif

#ifdef YAWN_TEST_BUILD
    void handleDefaultsClick(float, float, float, float,
                             float, float, float) {}
#else
    void handleDefaultsClick(float mx, float my, float cx, float cy,
                             float dropX, float dropW, float rh);
#endif

    // ─── Metronome Tab ─────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void paintMetronomeTab(UIContext&, float, float, float, float,
                           float, float) {}
#else
    void paintMetronomeTab(UIContext& ctx, float cx, float cy, float cw, float rh,
                           float ts, float th);
#endif

#ifdef YAWN_TEST_BUILD
    void handleMetronomeClick(float, float, float, float,
                              float, float, float) {}
#else
    void handleMetronomeClick(float mx, float my, float cx, float cy,
                              float dropX, float dropW, float rh);
#endif

    // ─── Drawing Helpers ────────────────────────────────────────────────

#ifdef YAWN_TEST_BUILD
    void drawLabel(Renderer2D&, Font&, const char*,
                   float, float, float) {}
#else
    void drawLabel(Renderer2D& r, Font& f, const char* text,
                   float x, float y, float scale);
#endif

#ifdef YAWN_TEST_BUILD
    void drawDropdown(Renderer2D&, Font&, float, float, float,
                      float, float, float,
                      const char*[], int, int,
                      bool*, bool = false) {}
#else
    void drawDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                      float rh, float th, float ts,
                      const char* items[], int count, int selected,
                      bool* isOpen, bool overlayPass = false);
#endif

#ifdef YAWN_TEST_BUILD
    void drawDeviceDropdown(Renderer2D&, Font&, float, float, float,
                            float, float, float,
                            const std::vector<audio::AudioDevice>&,
                            int, bool, bool*, bool = false) {}
#else
    void drawDeviceDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                            float rh, float th, float ts,
                            const std::vector<audio::AudioDevice>& devices,
                            int selected, bool outputOnly, bool* isOpen,
                            bool overlayPass = false);
#endif

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
    bool m_metroModeOpen = false;
    bool m_metroVolumeOpen = false;
    bool m_countInOpen = false;
};

} // namespace fw
} // namespace ui
} // namespace yawn
