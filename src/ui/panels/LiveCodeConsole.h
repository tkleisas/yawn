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
#include "ui/panels/LiveCodeEditor.h"
#include "ui/panels/LivePianoStrip.h"

#include <algorithm>
#include <functional>
#include <string>

namespace yawn {

class Project;
namespace audio { class AudioEngine; }
namespace livecode { class LiveCodeManager; }

namespace ui {

class LiveCodeConsole {
public:
    LiveCodeConsole();
    ~LiveCodeConsole() = default;

    void init(livecode::LiveCodeManager* mgr, Project* project,
              audio::AudioEngine* engine) {
        m_mgr = mgr; m_project = project; m_engine = engine;
    }

    void toggle(fw2::UIContext& ctx);   // `~` key
    bool isOpen() const { return m_handle.active(); }
    void close();

    // ─── Clipboard bridging (editor is SDL-free) ──────────────────
    // The app wires onClipboardOut/onClipboardIn to the SDL clipboard
    // (SDL_SetClipboardText / SDL_GetClipboardText). Unwired, an
    // internal fallback string keeps Ctrl+X → Ctrl+V working.
    std::function<void(const std::string&)> onClipboardOut;
    std::function<std::string()>            onClipboardIn;

    // ─── Editor font size (A- / A+ in the header) ─────────────────
    // Scale over the theme's fontSizeSmall, clamped 0.75..2.0. The
    // app persists it via onEditorFontScale (AppSettings).
    float editorFontScale() const { return m_editorFontScale; }
    void  setEditorFontScale(float s) {
        m_editorFontScale = std::clamp(s, 0.75f, 2.0f);
        m_editor.setFontScale(m_editorFontScale);
        if (onEditorFontScale) onEditorFontScale(m_editorFontScale);
    }
    std::function<void(float)> onEditorFontScale;

    // Per-frame housekeeping (code-lens throttle while visible).
    void tick();

    // ─── Editor routing (Edit tab) ────────────────────────────────
    // True when the Edit tab is active and keystrokes should be
    // captured before the app's global shortcuts (the App's key +
    // TEXT_INPUT handlers call these first while the console is open).
    bool wantsKeys() const { return isOpen() && m_tab == 2; }
    bool keyEvent(fw2::Key key, bool ctrl, bool shift);   // consumed?
    void forwardTextInput(const std::string& t);
    // Write the editor buffer to the script file (Save button).
    void saveBuffer();
    // Notation keyboard: insert a token at the editor caret.
    void insertAtCaret(const std::string& tok);
    // MIDI monitor feeding the notation keyboard's capture toggle.
    void setPianoMonitor(midi::MidiMonitorBuffer* buf) {
        m_piano.setMidiMonitor(buf);
    }
    // Patch the script file from project state — preserving comments
    // and layout (template-preserving round-trip, §5.4 stage 2).
    void syncScript();

private:
    void pushOverlay(fw2::UIContext& ctx);
    void paintBody(fw2::UIContext& ctx, const ::yawn::ui::fw::Rect& panel);
    bool handleMouseDown(fw2::MouseEvent& e, const ::yawn::ui::fw::Rect& panel);

    void refreshCodeLens();
    void loadEditorBuffer();

    // Cached from the last paint (click hit-testing needs text metrics;
    // the overlay handlers don't carry UIContext).
    float m_lastLineH = 0.0f;
    const fw2::TextMetrics* m_lastMet = nullptr;

    // Scrollbar drag state (editor scrollbar + console/code list bars).
    int   m_dragBarTab = -1;      // tab whose scrollbar is being dragged
                                  // (int: -1 sentinel; a bool would make -1
                                  // just `true` and drag on every move)
    float m_dragOffset = 0.0f;    // pointer offset inside the thumb (px)
    // Resizable console: bottom-right grip drags the panel size (which
    // persists across toggles).
    bool  m_resizing = false;
    bool  m_dragSelecting = false;   // sweep-select in the editor
    float m_panelW = 0.0f, m_panelH = 0.0f;   // 0 = defaults
    float m_lastViewportW = 0.0f, m_lastViewportH = 0.0f;
    void  resizeTo(float winX, float winY);
    size_t tabContentLines(int tab) const;
    ::yawn::ui::fw::Rect drawScrollbar(fw2::UIContext& ctx,
                                       const ::yawn::ui::fw::Rect& panel,
                                       const ::yawn::ui::fw::Rect& content,
                                       float contentH);
    void  dragMaybeStart(const ::yawn::ui::fw::Rect& panel, float lx, float ly);
    void  dragMove(const ::yawn::ui::fw::Rect& panel, float ly);

    livecode::LiveCodeManager* m_mgr = nullptr;
    Project* m_project = nullptr;
    audio::AudioEngine* m_engine = nullptr;

    fw2::OverlayHandle m_handle;
    ::yawn::ui::fw::Rect m_panel{};
    float m_scroll = 0.0f;      // >0 = scrolled back into history (px)
    int   m_tab = 0;            // 0=Console, 1=Code, 2=Edit
    bool  m_editorLoaded = false;
    LiveCodeEditor m_editor;
    fw::LivePianoStrip m_piano;         // Edit-tab notation keyboard
    std::string m_clipFallback;         // internal clipboard when the
                                        // app doesn't bridge SDL
    float m_editorFontScale = 1.0f;     // Edit-tab buffer zoom
    ::yawn::ui::fw::Rect m_pianoRect{}; // cached for hit-testing
    std::string m_codeLens;
    double m_codeLensAge = 0.0; // seconds since last regen
    bool  m_codeLensDirty = true;
};

} // namespace ui
} // namespace yawn
