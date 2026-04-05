#pragma once

#include "ui/framework/Dialog.h"
#include "ui/Theme.h"
#include "audio/OfflineRenderer.h"
#include "util/FileIO.h"
#include <string>
#include <atomic>
#include <memory>

namespace yawn {
namespace ui {
namespace fw {

class ExportDialog : public Dialog {
public:
    ExportDialog()
        : Dialog("Export Audio", 420, 340)
    {
        m_showClose = true;
        m_showOKCancel = true;
        m_okLabel = "Export";
        m_visible = false;
    }

    // ─── State ──────────────────────────────────────────────────────────

    struct Config {
        util::ExportFormat format = util::ExportFormat::WAV;
        util::BitDepth bitDepth = util::BitDepth::Float32;
        int sampleRate = 44100;
        int scope = 0;  // 0 = full arrangement, 1 = loop region
        double arrangementLengthBeats = 64.0;
        double loopStartBeats = 0.0;
        double loopEndBeats = 0.0;
        bool loopEnabled = false;
    };

    void open(const Config& config) {
        m_config = config;
        m_rendering = false;
        m_progress.fraction.store(0.0f);
        m_progress.done.store(false);
        m_progress.cancelled.store(false);
        m_progress.failed.store(false);

        // If loop is enabled, default to loop region scope
        if (config.loopEnabled && config.loopEndBeats > config.loopStartBeats) {
            m_config.scope = 1;
        } else {
            m_config.scope = 0;
        }

        m_visible = true;
    }

    bool isOpen() const { return m_visible; }
    const Config& config() const { return m_config; }
    audio::RenderProgress& progress() { return m_progress; }
    bool isRendering() const { return m_rendering; }
    void setRendering(bool r) { m_rendering = r; }

    void close(DialogResult r = DialogResult::Cancel) override {
        if (m_rendering) {
            m_progress.cancelled.store(true);
            return;  // Don't close yet — wait for render thread to finish
        }
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
        m_dx = (m_screenW - m_preferredW) * 0.5f;
        m_dy = (m_screenH - m_preferredH) * 0.5f;
        Dialog::layout(Rect{m_dx, m_dy, m_preferredW, m_preferredH}, ctx);
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
        if (m_rendering) return true;  // Block input during render
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

    // Force close (called when render completes or is cancelled)
    void forceClose() {
        m_rendering = false;
        m_visible = false;
    }

private:
    Config m_config;
    audio::RenderProgress m_progress;
    bool m_rendering = false;
    float m_screenW = 0, m_screenH = 0;
    float m_dx = 0, m_dy = 0;

    // Dropdown open states
    bool m_formatOpen = false;
    bool m_bitDepthOpen = false;
    bool m_sampleRateOpen = false;
    bool m_scopeOpen = false;
    int m_popupHover = -1;

    // Drawing helpers (same pattern as PreferencesDialog)
    void drawLabel(Renderer2D& r, Font& f, const char* text,
                   float x, float y, float scale);
    void drawDropdown(Renderer2D& r, Font& f, float x, float y, float w,
                      float rh, float th, float ts,
                      const char* items[], int count, int selected,
                      bool* isOpen, bool overlayPass = false);
    bool handleDropdownClick(float mx, float my, float x, float y,
                             float w, float h, int count,
                             bool* isOpen, std::function<void(int)> onSelect);
};

} // namespace fw
} // namespace ui
} // namespace yawn
