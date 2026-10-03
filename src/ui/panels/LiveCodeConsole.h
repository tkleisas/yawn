#pragma once

// LiveCodeConsole — the `~` drop-down console for the live-coding layer
// (phase 6). Quake-style top overlay on the fw2 LayerStack (Overlay
// layer, non-modal, stays open until toggled again). Shows:
//
//   [Console] [Code]              ← tab row
//   Console tab: the manager's console ring, severity-colored, wheel-
//                scrollable, newest at bottom.
//   Code tab:    the code lens — generateSongSource() regenerated from
//                the live project (throttled ~1 Hz while visible).
//   Header: status (gen N running/stopped, pending entries, prerender
//           jobs) + Run/Stop / Reload / Clear buttons.
//
// Owns no audio state; reads everything through LiveCodeManager and the
// Project snapshot. Main-exe code (paints via Renderer2D directly).

#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Widget.h"

#include <functional>
#include <string>

namespace yawn {

class Project;
namespace audio { class AudioEngine; }
namespace livecode { class LiveCodeManager; }

namespace ui {

class LiveCodeConsole {
public:
    LiveCodeConsole() = default;
    ~LiveCodeConsole() = default;

    void init(livecode::LiveCodeManager* mgr, Project* project,
              audio::AudioEngine* engine) {
        m_mgr = mgr; m_project = project; m_engine = engine;
    }

    void toggle(fw2::UIContext& ctx);   // `~` key
    bool isOpen() const { return m_handle.active(); }
    void close();

    // Per-frame housekeeping (code-lens throttle while visible).
    void tick();

private:
    void pushOverlay(fw2::UIContext& ctx);
    void paintBody(fw2::UIContext& ctx, const ::yawn::ui::fw::Rect& panel);
    bool handleMouseDown(fw2::MouseEvent& e, const ::yawn::ui::fw::Rect& panel);

    void refreshCodeLens();

    livecode::LiveCodeManager* m_mgr = nullptr;
    Project* m_project = nullptr;
    audio::AudioEngine* m_engine = nullptr;

    fw2::OverlayHandle m_handle;
    ::yawn::ui::fw::Rect m_panel{};
    float m_scroll = 0.0f;      // >0 = scrolled back into history (px)
    bool  m_tabCode = false;
    std::string m_codeLens;
    double m_codeLensAge = 0.0; // seconds since last regen
    bool  m_codeLensDirty = true;
};

} // namespace ui
} // namespace yawn
