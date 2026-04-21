#pragma once

// UI v2 — LayerStack.
//
// The scene root for floating UI: modals, dropdowns, tooltips, toasts.
// The main widget tree (OverlayLayer::Main) still owns normal content;
// everything that escapes the tree — dialogs with scrim, dropdowns that
// must draw above their parent's clip, tooltips, toasts — goes through
// here.
//
// Design goals:
//   • Paint bottom-up (Main drawn by widget tree; LayerStack adds the
//     rest on top in enum-value order).
//   • Hit-test top-down (Overlay newest-first, then Modal; Main handled
//     by the widget-tree dispatcher).
//   • Dismissal rules uniform across overlays: outside-click dismiss,
//     Escape dismiss; callers override via entry flags + callbacks.
//   • Move-only RAII handles so an owning widget can hold the handle
//     and its destructor guarantees cleanup.
//
// Tests live in tests/test_fw2_LayerStack.cpp — no GL needed because
// entries are closures and LayerStack never touches the renderer
// beyond calling ctx.renderer->drawRect() for the modal scrim (a
// null-check is fine for test harnesses).
//
// See docs/ui-v2-layer-stack.md for the full spec.

#include "Widget.h"   // MouseEvent, KeyEvent, ScrollEvent, Rect

#include <array>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// OverlayLayer — the five z-bands LayerStack manages.
//   Main    = normal widget tree, NOT managed by LayerStack. Listed
//             here for completeness; push(Main, ...) is rejected.
//   Modal   = dialogs with shared scrim. Blocks everything below.
//   Overlay = dropdowns / context menus / popovers. Non-blocking by
//             default; outside-click dismisses.
//   Tooltip = hover-triggered info bubbles. Visual only — never
//             participates in hit-test.
//   Toast   = status banners. Visual only — never participates in
//             hit-test.
// ───────────────────────────────────────────────────────────────────
enum class OverlayLayer : int {
    Main    = 0,
    Modal   = 1,
    Overlay = 2,
    Tooltip = 3,
    Toast   = 4,
};

// ───────────────────────────────────────────────────────────────────
// OverlayEntry — the data + behavior that LayerStack stores per
// floating element. Closures capture the presenting widget; LayerStack
// does NOT own the widget.
// ───────────────────────────────────────────────────────────────────
struct OverlayEntry {
    // Debug / accessibility
    std::string debugName;
    std::string ariaLabel;

    // Paint closure. Invoked during paintLayers(); bounds is already
    // in screen coords (no parent transform applied). Clip stack is
    // restored to the full viewport before invocation — overlays
    // always paint above whatever the Main tree clipped to.
    std::function<void(UIContext&)> paint;

    // Screen-space bounds used for the default hit-test. Overlays
    // that are non-rectangular can provide customHitTest instead.
    Rect bounds{};

    // Optional: custom hit-test. Returns true if the point (ex, ey,
    // in screen coords) is considered "inside" the overlay. When null,
    // bounds.contains(ex, ey) is used.
    std::function<bool(float /*sx*/, float /*sy*/)> customHitTest;

    // Input handlers. Each returns true to consume the event. Null
    // means "not handled" (the layer's default rules still apply —
    // e.g. a Modal entry without a mouseDown handler still consumes
    // the click because modals block).
    std::function<bool(MouseEvent&)>     onMouseDown;
    std::function<bool(MouseEvent&)>     onMouseUp;
    std::function<bool(MouseMoveEvent&)> onMouseMove;
    std::function<bool(ScrollEvent&)>    onScroll;
    std::function<bool(KeyEvent&)>       onKey;

    // Flags
    //   modal: this entry swallows events that don't hit it. Used by
    //   Modal dialogs (shared scrim is separate — see LayerStack
    //   paintLayers). Overlays can opt-in by setting this true
    //   (rare — think "must-confirm" popover).
    bool modal                 = false;

    //   dismissOnOutsideClick: true by default for Overlay/Modal; a
    //   mouse-down outside the entry's hit region removes the entry
    //   (via the onDismiss callback + Handle). Set false for stuck-
    //   open overlays (e.g. a tour step waiting on an explicit user
    //   action).
    bool dismissOnOutsideClick = true;

    // Fires once when the entry is removed — explicit remove(),
    // outside-click dismiss, Escape, or Handle destruction all route
    // through here. Idempotent (LayerStack guarantees single fire).
    std::function<void()> onDismiss;

    // Escape-key handler. Defaults to "remove this entry" when the
    // LayerStack is wired up — callers can replace to validate input
    // or suppress dismiss.
    std::function<void()> onEscape;

    // Populated by LayerStack — caller never sets these.
    std::uint64_t id              = 0;
    int           insertionOrder  = 0;
};

// ───────────────────────────────────────────────────────────────────
// OverlayHandle — move-only RAII handle returned by push(). Destroy
// the handle (or call remove()) to dismiss the entry.
//
// Holding a handle while the LayerStack is destroyed is UB — owning
// widgets are responsible for outliving the LayerStack, which in
// practice means the App. This is the same rule v1 has for Widget*
// parents.
// ───────────────────────────────────────────────────────────────────
class LayerStack;

class OverlayHandle {
public:
    OverlayHandle() = default;
    OverlayHandle(LayerStack* owner, OverlayLayer layer, std::uint64_t id)
        : m_owner(owner), m_layer(layer), m_id(id) {}

    OverlayHandle(const OverlayHandle&) = delete;
    OverlayHandle& operator=(const OverlayHandle&) = delete;

    OverlayHandle(OverlayHandle&& other) noexcept
        : m_owner(other.m_owner), m_layer(other.m_layer), m_id(other.m_id) {
        other.m_owner = nullptr;
        other.m_id    = 0;
    }
    OverlayHandle& operator=(OverlayHandle&& other) noexcept {
        if (this != &other) {
            remove();
            m_owner = other.m_owner;
            m_layer = other.m_layer;
            m_id    = other.m_id;
            other.m_owner = nullptr;
            other.m_id    = 0;
        }
        return *this;
    }

    ~OverlayHandle() { remove(); }

    // Idempotent: safe to call multiple times; first call dismisses,
    // subsequent calls are no-ops.
    void remove();

    bool          active() const { return m_owner && m_id != 0; }
    OverlayLayer  layer()  const { return m_layer; }
    std::uint64_t id()     const { return m_id; }

    // Needed for the friend-less remove() path: LayerStack::remove
    // clears the handle after internal cleanup.
    void detach_noRemove() { m_owner = nullptr; m_id = 0; }

private:
    LayerStack*    m_owner = nullptr;
    OverlayLayer   m_layer = OverlayLayer::Overlay;
    std::uint64_t  m_id    = 0;
};

// ───────────────────────────────────────────────────────────────────
// LayerStack — the manager itself.
// ───────────────────────────────────────────────────────────────────
class LayerStack {
public:
    LayerStack() = default;
    ~LayerStack();

    LayerStack(const LayerStack&)            = delete;
    LayerStack& operator=(const LayerStack&) = delete;

    // ─── Presentation ──────────────────────────────────────────────
    // Push an entry onto the given layer. Returns a move-only handle
    // whose destructor dismisses the entry (RAII).
    //   NOTE: layer must NOT be OverlayLayer::Main — that layer is the
    //   regular widget tree and is not managed here. Attempts return
    //   an inactive handle and log a warning.
    [[nodiscard]] OverlayHandle push(OverlayLayer layer, OverlayEntry entry);

    // Dismiss an entry now. Fires its onDismiss and invalidates the
    // handle. Safe to call with an already-dismissed or default-
    // constructed handle.
    void remove(OverlayHandle& h);

    // ─── Per-frame paint ──────────────────────────────────────────
    // Paint all layers in enum order (Modal first, Toast last). Call
    // AFTER the main widget tree has painted. `viewport` is the full
    // window in screen coords — used to size the modal scrim.
    //
    // Rendering walks through ctx.renderer; if ctx.renderer is null
    // (e.g. tests), only per-entry paint closures are invoked (caller
    // closures may themselves null-check if they use the renderer).
    void paintLayers(UIContext& ctx, Rect viewport);

    // ─── Per-frame event dispatch ─────────────────────────────────
    // Call BEFORE dispatching to the main widget tree. If the returned
    // bool is true, the event was consumed — do not forward.
    //
    // Dispatch rules (see class header comment):
    //   • Overlay layer first (topmost entry), newest → oldest
    //   • Modal layer next (topmost only, since modals stack but only
    //     the topmost is interactive)
    //   • Outside-click on a dismissable entry → remove; modal=true
    //     consumes, modal=false falls through to the next layer
    bool dispatchMouseDown(MouseEvent& e);
    bool dispatchMouseUp(MouseEvent& e);
    bool dispatchMouseMove(MouseMoveEvent& e);
    bool dispatchScroll(ScrollEvent& e);
    bool dispatchKey(KeyEvent& e);

    // ─── Query ─────────────────────────────────────────────────────
    bool hasModalActive() const;
    int  entryCount(OverlayLayer layer) const;
    int  totalEntryCount() const;

    // ─── Debug ─────────────────────────────────────────────────────
    // Emit a human-readable dump (one line per entry, grouped by
    // layer). Useful from test failures and ad-hoc diagnostics.
    void dumpState(std::ostream& os) const;

private:
    friend class OverlayHandle;

    // Layer storage. Index by static_cast<int>(OverlayLayer). Main
    // slot unused.
    std::array<std::vector<OverlayEntry>, 5> m_layers;

    std::uint64_t m_nextId             = 1;
    int           m_nextInsertionOrder = 0;

    // Internal remove by id — does NOT touch the handle; handle
    // path calls this then detach_noRemove on itself.
    //   fireDismiss: pass false to suppress onDismiss (used during
    //   LayerStack destruction when handlers might reference freed
    //   state).
    void removeById(OverlayLayer layer, std::uint64_t id, bool fireDismiss);

    // Helpers
    bool entryContains(const OverlayEntry& e, float sx, float sy) const;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
