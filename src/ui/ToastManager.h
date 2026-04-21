#pragma once

// ToastManager — top-center banner for transient status messages.
//
// Design choices:
//   • Replace-latest: a new toast overwrites the previous immediately.
//     Keeps the screen calm during bursts (e.g. spinning an encoder
//     that emits a stream of param updates).
//   • Time-based fade: 1.5s hold + 200ms fade-out by default. Fully
//     transparent past (show_time + duration + fade_out).
//   • Severity just tints the accent bar on the left edge — info/warn/
//     error pick distinct colors but the toast shape is identical.
//   • Thread-safe show(): Lua / controller callback thread can call it;
//     render() only runs on the UI thread.

#include "ui/Font.h"
#include "ui/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>

namespace yawn {
namespace ui {

class ToastManager {
public:
    enum class Severity : int {
        Info  = 0,
        Warn  = 1,
        Error = 2,
    };

    // Called from any thread. Replaces any currently visible toast.
    void show(const std::string& text,
              float durationSec = 1.5f,
              Severity severity = Severity::Info) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_text      = text;
        m_duration  = std::max(0.1f, durationSec);
        m_severity  = severity;
        m_showTime  = std::chrono::steady_clock::now();
        m_visible   = true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_visible = false;
    }

    // Render on top of everything else. UI thread only.
    // Expects Renderer2D::beginFrame() already called this frame.
    void render(Renderer2D& r, Font& font, int screenW, int /*screenH*/) {
        std::string text;
        Severity sev;
        float alpha;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_visible) return;
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - m_showTime).count();
            float total   = m_duration + kFadeOutSec;
            if (elapsed >= total) { m_visible = false; return; }
            alpha = (elapsed <= m_duration) ? 1.0f
                   : 1.0f - ((elapsed - m_duration) / kFadeOutSec);
            text = m_text;
            sev  = m_severity;
        }

        if (text.empty()) return;

        // Measure text. We use a smallish scale since this is secondary
        // info — don't dominate the screen. 0.4 * 48px atlas = ~19px.
        const float kScale = 0.4f;
        const float textW  = font.textWidth(text, kScale);
        const float textH  = font.lineHeight(kScale);

        const float padX = 14.0f;
        const float padY = 8.0f;
        const float w    = textW + padX * 2.0f;
        const float h    = textH + padY * 2.0f;
        // ~20% from top (classic toast position, doesn't obscure transport).
        const float x    = (static_cast<float>(screenW) - w) * 0.5f;
        const float y    = 36.0f;

        // Alpha-scaled colors.
        auto withAlpha = [alpha](uint8_t r8, uint8_t g8, uint8_t b8, uint8_t baseA) {
            return Color{
                r8, g8, b8,
                static_cast<uint8_t>(std::round(static_cast<float>(baseA) * alpha))
            };
        };

        // Rounded background
        r.drawRoundedRect(x, y, w, h, 6.0f,
                          withAlpha(20, 20, 24, 220));

        // Severity accent bar on the left edge — 3px wide full-height stripe
        Color accent;
        switch (sev) {
        case Severity::Warn:  accent = withAlpha(220, 180, 60,  255); break;
        case Severity::Error: accent = withAlpha(220, 70,  70,  255); break;
        default:              accent = withAlpha(120, 180, 220, 255); break;
        }
        r.drawRect(x, y + 4.0f, 3.0f, h - 8.0f, accent);

        // Text itself, centered within the padded box
        Color textColor = withAlpha(245, 245, 245, 255);
        const float tx = x + padX;
        const float ty = y + padY;
        font.drawText(r, text.c_str(), tx, ty, kScale, textColor);
    }

private:
    static constexpr float kFadeOutSec = 0.2f;

    mutable std::mutex m_mutex;
    std::string        m_text;
    Severity           m_severity = Severity::Info;
    float              m_duration = 0.0f;  // hold time in seconds
    bool               m_visible  = false;
    std::chrono::steady_clock::time_point m_showTime;
};

} // namespace ui
} // namespace yawn
