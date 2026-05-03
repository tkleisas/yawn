#pragma once

// UI v2 — DragManager.
//
// Cross-panel drag-and-drop for in-app payloads (audio clips today,
// MIDI clips / colours / text in the future). Lives on the
// LayerStack::Tooltip layer so the ghost visual rides above
// everything but doesn't intercept clicks.
//
// Architectural shape:
//
//   * Sources call `start(payload, sx, sy)` when they detect a drag
//     gesture (long-press OR Alt+drag in our convention; see source
//     panels). The manager pushes an overlay that paints a small
//     ghost following the cursor, and a label like "Audio Clip •
//     bass-loop.wav".
//
//   * App's mouse-move handler funnels every motion event into
//     `updatePos(x, y)` so the ghost tracks the cursor.
//
//   * Drop targets check `active()` + `payload()` from their own
//     paint code (to highlight valid drop zones) and from their
//     mouse-up handler (to actually consume the drop).
//
//   * App's mouse-up handler calls `cancel()` AFTER giving the
//     target panels a chance to consume the drop. If a target
//     called `finish()` first the manager has already torn down;
//     `cancel()` becomes a no-op in that case.
//
// We deliberately do NOT centralise the drop target list in the
// manager — there's no single registry of drop zones. Each panel
// knows its own geometry; querying the manager from inside the
// panel's existing mouse-up handler keeps the wiring local.

#include "Widget.h"          // Rect
#include "LayerStack.h"      // OverlayHandle

#include <memory>
#include <string>

namespace yawn {
namespace audio { class AudioBuffer; }

namespace ui {
namespace fw2 {

// What's being dragged. Today only audio clips; the kind enum makes
// it easy to add MIDI clips / browser-file paths / colour swatches /
// etc. without retrofitting the drop target callsites.
struct DragPayload {
    enum class Kind {
        None,
        AudioClip,   // shared_ptr<AudioBuffer> in audioBuffer; label set
    };

    Kind kind = Kind::None;

    // Audio-clip payload fields.
    std::shared_ptr<audio::AudioBuffer> audioBuffer;
    std::string label;          // shown in ghost ("bass-loop.wav")

    // Source coordinates so a drop target can ignore drops onto its
    // own origin (e.g. if the user drags a session cell to its own
    // track's detail panel — no-op rather than self-load).
    int  sourceTrack = -1;
    int  sourceScene = -1;
    bool sourceFromArrangement = false;
};

class DragManager {
public:
    static DragManager& instance();

    DragManager(const DragManager&)            = delete;
    DragManager& operator=(const DragManager&) = delete;

    // Start a drag — pushes the ghost overlay. Replaces any active
    // drag silently (sources should never start a new one without
    // the previous one finishing, but this keeps state consistent).
    void start(DragPayload payload, float sx, float sy);

    // Cursor moved during an active drag. No-op if not active.
    void updatePos(float sx, float sy);

    // Tear down the active drag without notifying any target. App
    // mouse-up handlers call this when no drop target consumed the
    // gesture (e.g. release over empty space).
    void cancel();

    // Tear down because a target consumed the drop. Identical to
    // cancel() in effect — exists so callsites can express intent.
    void finish() { cancel(); }

    // Status accessors.
    bool active() const                  { return m_active; }
    const DragPayload& payload() const   { return m_payload; }
    float currentX() const               { return m_curX; }
    float currentY() const               { return m_curY; }

    // Modifier tracking — App pumps the live Ctrl state on every
    // mouse-move event so the ghost painter can show a "+" badge
    // while the user holds Ctrl (= clone semantics). Drop targets
    // can also read this to differentiate copy-vs-move on release.
    void  setCtrlHeld(bool on)           { m_ctrlHeld = on; }
    bool  ctrlHeld() const               { return m_ctrlHeld; }

    // Convenience: only-true when a drag is active AND its payload
    // matches the requested kind. Saves drop targets a two-line
    // check.
    bool isDraggingAudioClip() const {
        return m_active && m_payload.kind == DragPayload::Kind::AudioClip;
    }

    // Test-only — wipe state. Used by fixtures.
    void _testResetAll();

    // Painter hook — main exe installs (Fw2Painters.cpp); tests skip
    // and the ghost simply doesn't paint. Mirrors Tooltip / Dialog.
    using GhostPaintFn = void(*)(const DragPayload& payload,
                                  float cursorX, float cursorY,
                                  UIContext& ctx);
    static void         setPainter(GhostPaintFn fn);
    static GhostPaintFn painter();

    // Drop-target highlight helper. Drop-receiving panels call this
    // at the END of their render(). When an audio-clip drag is in
    // progress and the cursor is inside `panelBounds`, paints a
    // subtle accent overlay (translucent fill + bright border) over
    // those bounds — telling the user "release here will land".
    // No-op when no drag is active or cursor is elsewhere, so it's
    // safe to call unconditionally per render tick.
    static void renderDropHighlight(const Rect& panelBounds,
                                     UIContext& ctx);

private:
    DragManager() = default;

    void pushOverlay();

    bool          m_active = false;
    DragPayload   m_payload;
    float         m_curX = 0.0f;
    float         m_curY = 0.0f;
    bool          m_ctrlHeld = false;
    OverlayHandle m_handle;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
