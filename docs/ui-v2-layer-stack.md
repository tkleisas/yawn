# YAWN UI v2 — LayerStack & OverlayEntry Deep Dive

Supplementary reference for the layered scene-root architecture
introduced in [`ui-v2-architecture.md`](ui-v2-architecture.md). This
doc is the source of truth for the LayerStack API, OverlayEntry
lifecycle, hit-test ordering, and paint ordering.

The widget specs that consume this infrastructure:
[dropdown.md](widgets/dropdown.md), [tooltip.md](widgets/tooltip.md),
[context_menu.md](widgets/context_menu.md),
[dialog.md](widgets/dialog.md).

---

## Why layers exist

The v1 framework had a single widget tree where paint order = tree
order. That's fine for most widgets, but popups (dropdowns, context
menus, tooltips) need to escape their parent's clip rect and
z-order. v1's workaround — `paintOverlay()` called manually by each
panel — was error-prone and produced a recurring class of "dropdown
obscured by later-drawn sibling" bugs.

v2 solves this structurally: **the scene root is a stack of layers**,
each with its own widget tree. Paint happens bottom-up layer by
layer; hit-testing happens top-down. Widgets that need to escape
register an `OverlayEntry` on the appropriate layer.

Inspired by **Flutter's Overlay** + **Dear ImGui's popup command
list**. Not inspired by React portals — portals require a retained-
scene-graph reconciler; OverlayEntries are just closures + metadata,
which fits C++ better.

---

## Layer enumeration

```cpp
enum class OverlayLayer : int {
    Main    = 0,    // regular widget tree — panels, menu bar, content
    Modal   = 1,    // dialogs (with scrim)
    Overlay = 2,    // dropdowns, context menus, popovers, drag previews
    Tooltip = 3,    // hover-triggered info bubbles
    Toast   = 4,    // status banners
};
```

Per-layer behavior summary:

| Layer | Multi-entry | Hit-test | Scrim | Outside-click dismiss | Paint order |
|---|---|---|---|---|---|
| Main | — (widget tree) | tree-walk | no | n/a | first |
| Modal | yes (stacked) | topmost only | yes (shared) | no | after Main |
| Overlay | yes | topmost first | no | yes (default) | after Modal |
| Tooltip | yes (rare) | no | no | n/a | after Overlay |
| Toast | yes | no | no | n/a | last |

"Hit-test topmost only" means only the highest-insertion-order entry
on that layer receives input; lower entries on the same layer are
visible but inert. Exception: overlays with `modal = false` pass
their parent's hit-tests through to siblings on the same layer.

---

## Public API

```cpp
// src/ui/framework/v2/LayerStack.h

struct OverlayEntry;
struct OverlayHandle;

class LayerStack {
public:
    LayerStack();

    // Push an entry onto the given layer. Returns a handle for
    // later removal. Handle is movable but not copyable.
    [[nodiscard]] OverlayHandle push(OverlayLayer layer, OverlayEntry entry);

    // Remove a previously-pushed entry. Safe to call with a
    // cleared / default handle (no-op).
    void remove(OverlayHandle& handle);

    // Per-frame paint pass — called by the app after the main widget
    // tree has painted. Paints Modal → Overlay → Tooltip → Toast.
    void paintLayers(UIContext& ctx);

    // Per-frame hit-test pass — called by the app's event
    // dispatcher BEFORE the main widget tree's hit-test, top-down.
    // Returns true if any layer consumed the event.
    bool dispatchMouseDown(MouseEvent& e);
    bool dispatchMouseUp(MouseEvent& e);
    bool dispatchMouseMove(MouseMoveEvent& e);
    bool dispatchScroll(ScrollEvent& e);
    bool dispatchKey(KeyEvent& e);

    // Query
    bool hasModalActive() const;      // any Modal-layer entries?
    int  entryCount(OverlayLayer) const;

    // Debug
    void dumpState(std::ostream&) const;   // for debug overlay
};
```

### OverlayEntry struct

```cpp
struct OverlayEntry {
    // Owner widget — tracked so we can cascade-remove on destructor.
    Widget* owner = nullptr;

    // Paint callback. Receives UIContext; draws whatever the entry
    // needs. Runs in the layer's paint pass, with clip state reset.
    std::function<void(UIContext&)> paint;

    // Hit-test bounds in screen coordinates. Used by the default
    // hit-test implementation. Override via customHitTest for
    // non-rectangular hit regions.
    Rect bounds;

    // Optional custom hit test. If null, uses bounds.contains(event).
    // Return true = entry consumes the event; false = passes through.
    std::function<bool(const Event&)> customHitTest;

    // Per-event dispatch (called if customHitTest returns true or
    // default bounds test matches).
    std::function<bool(MouseEvent&)> onMouseDown;
    std::function<bool(MouseEvent&)> onMouseUp;
    std::function<bool(MouseMoveEvent&)> onMouseMove;
    std::function<bool(ScrollEvent&)> onScroll;
    std::function<bool(KeyEvent&)> onKey;

    // Modal behavior — blocks input below on the same layer.
    // Default true for Modal layer; default false for Overlay layer.
    bool modal = false;

    // Outside-click dismissal policy.
    //   • true  = clicking outside this entry's bounds dismisses it.
    //   • false = the outside click falls through to lower layers.
    // Default true for Overlay + Tooltip; false for Modal (modal
    // dialogs need explicit dismissal via buttons / Escape).
    bool dismissOnOutsideClick = true;

    // Called when the entry is removed (dismiss, destructor, explicit
    // remove(). Not called during app shutdown).
    std::function<void()> onDismiss;

    // Called when Escape key is pressed and this entry is on top of
    // its layer. Defaults to calling `onDismiss` via `handle.remove()`.
    // Callers can override to run validation / confirmation first.
    std::function<void()> onEscape;

    // Accessibility label (forwarded to ARIA tree; not rendered).
    std::string ariaLabel;

    // Debugging
    std::string debugName;

    // Insertion order (set by LayerStack on push; don't set manually)
    int insertionOrder = 0;
};
```

### OverlayHandle

```cpp
class OverlayHandle {
public:
    OverlayHandle() = default;
    ~OverlayHandle();     // if valid, calls LayerStack::remove

    OverlayHandle(OverlayHandle&&) noexcept;
    OverlayHandle& operator=(OverlayHandle&&) noexcept;

    OverlayHandle(const OverlayHandle&) = delete;
    OverlayHandle& operator=(const OverlayHandle&) = delete;

    // Check + clear
    bool valid() const;
    void clear();         // removes entry without destructor
    void cancel();        // clear() + don't invoke onDismiss

    // Internal — used by LayerStack::remove
    int entryId() const;
    OverlayLayer layer() const;

private:
    friend class LayerStack;
    LayerStack* m_stack = nullptr;
    OverlayLayer m_layer = OverlayLayer::Main;
    int m_entryId = -1;
};
```

Handle is **move-only**. Assignment transfers ownership; destructor
auto-removes if still valid. Pattern matches `std::unique_ptr` —
widgets store `OverlayHandle m_overlay;` as a member, and when the
widget is destroyed the entry is auto-cleaned up without manual
lifecycle code.

---

## Lifecycle

### Pushing an entry

```cpp
// In FwDropDown::onClick:
OverlayEntry entry;
entry.owner = this;
entry.bounds = computePopupRect();
entry.paint = [this](UIContext& ctx) { paintPopup(ctx); };
entry.onMouseDown = [this](MouseEvent& e) { return handlePopupMouseDown(e); };
entry.onKey = [this](KeyEvent& e) { return handlePopupKey(e); };
entry.onDismiss = [this]() { m_open = false; invalidate(); };
entry.debugName = "FwDropDown@" + std::to_string(reinterpret_cast<uintptr_t>(this));

m_overlayHandle = ctx.layerStack().push(OverlayLayer::Overlay, std::move(entry));
m_open = true;
```

`push` returns a handle; the widget stores it as a member. Push is
O(1) amortized (vector append + id assignment).

### Removing an entry

Three ways:

1. **Explicit**: `layerStack.remove(m_overlayHandle);` — synchronous,
   fires `onDismiss` before removal.
2. **Handle destruction**: `m_overlayHandle.~OverlayHandle();` —
   happens automatically when the owning widget is destroyed.
3. **Outside-click / Escape**: LayerStack auto-removes + fires
   `onDismiss` when dismissal conditions match the entry's policy.

All three paths call `onDismiss` if set. `cancel()` skips it — used
when an action has already completed and the widget wants to tear
down the overlay without re-notifying.

### Destructor order

```cpp
FwDropDown::~FwDropDown() {
    // m_overlayHandle destructor auto-runs; calls layerStack.remove
    // which calls onDismiss → m_open = false + invalidate on a
    // partially-destroyed widget.
    //
    // The onDismiss callback captured `this`, so destruction order
    // matters. Option 1: explicit close() at start of dtor.
    // Option 2: cancel() to skip onDismiss.
    m_overlayHandle.cancel();
}
```

Recommended pattern: explicit `close()` in widget destructor so
onDismiss can access a valid `this`. Or `cancel()` if the widget's
own tear-down logic suffices.

---

## Hit-test dispatch

The app's main event loop calls `layerStack.dispatchXXX(...)` BEFORE
walking the widget tree:

```cpp
void App::handleMouseDown(MouseEvent& e) {
    if (m_layerStack.dispatchMouseDown(e)) return;   // consumed by layer
    m_rootWidget.dispatchMouseDown(e);                // fall through to Main
}
```

### Per-layer algorithm

```cpp
bool LayerStack::dispatchMouseDown(MouseEvent& e) {
    // Walk layers top-down.
    for (int L = TopLayer; L >= FirstLayer; --L) {
        auto& layerEntries = m_entries[L];

        // Walk entries top-to-bottom within the layer.
        for (auto it = layerEntries.rbegin(); it != layerEntries.rend(); ++it) {
            OverlayEntry& entry = *it;

            // Outside-click handling for dismiss-on-outside.
            if (entry.dismissOnOutsideClick && !hitTestEntry(entry, e)) {
                // Outside click on a dismissable entry.
                if (isTopmostOfLayer(it)) {
                    remove(entry);   // dismiss topmost
                    if (entry.modal) return true;    // modal consumed the click
                    continue;         // non-modal: let others try or fall through
                }
                continue;
            }

            // Inside entry bounds.
            if (hitTestEntry(entry, e)) {
                bool consumed = entry.onMouseDown && entry.onMouseDown(e);
                if (consumed) return true;
            }

            // Modal blocks everything below, whether consumed or not.
            if (entry.modal) return true;
        }

        // If any entry on this layer is modal, don't fall through to
        // lower layers.
        if (anyModalOnLayer(L)) return true;
    }
    return false;   // no layer consumed; main tree gets it
}
```

### Modal propagation

A Modal-flag entry on any layer blocks hit-test propagation to
everything below — both to lower entries on the same layer and to
all lower layers including Main.

The Modal layer's entries are typically all `modal = true` (dialogs
block the UI below). Overlay layer entries default to `modal =
false` (dropdowns are interactive but don't block the main UI from
receiving keyboard shortcuts, scroll, etc.).

### Outside-click dismissal

When a user clicks outside the topmost dismissable entry on a layer,
that entry is auto-removed. The click itself:
- Consumed if `modal = true` (click ate by scrim-like effect).
- Not consumed if `modal = false` (click falls through to lower
  layers, so the user can click a sibling widget in the main UI and
  have their click register while the dropdown dismisses).

---

## Paint dispatch

After the app paints the main widget tree, it calls
`layerStack.paintLayers(ctx)`:

```cpp
void LayerStack::paintLayers(UIContext& ctx) {
    for (int L = Modal; L <= Toast; ++L) {
        // Pre-layer render: scrim on Modal layer.
        if (L == Modal && !m_entries[L].empty()) {
            drawScrim(ctx);
        }

        // Entries in insertion order (bottom up within the layer).
        for (auto& entry : m_entries[L]) {
            if (entry.paint) {
                ctx.renderer->resetClip();   // each entry starts clean
                entry.paint(ctx);
            }
        }
    }
}
```

Each entry's paint runs with:
- Clip stack reset (entry paints over anything, clipped only by the
  window).
- No depth test (order is purely insertion-order layering).
- Theme access (entries can call `theme().palette....`).

Entries are responsible for their own clipping within their bounds if
needed (popup lists push/pop a clip to prevent item text overflow).

---

## Z-order within a layer

Insertion order. The last-pushed entry paints on top and is hit-
tested first. No z-index property — reorder by removing + re-pushing.

Example: a ContextMenu opened over a DropDown both live on Overlay
layer. The ContextMenu pushes later, so it's drawn above and
consumes input first. Closing it pops back to the DropDown below
unchanged.

Nested dropdowns (a DropDown opening a submenu, or a ContextMenu
opening its submenu) use the same insertion-order rule.

---

## The Modal layer's scrim

A single scrim (dark semi-transparent overlay covering the full
window) is painted once when any entries exist on the Modal layer.
Not per-entry — stacked dialogs share one scrim, rendered with
`palette.scrim` color.

Scrim is drawn **below** modal dialogs but **above** Main and
Overlay layers. Clicks on the scrim (outside any dialog body) either:
- Get consumed by scrim (modal block) if no dialog consumes the click
  first.
- Fire the topmost dialog's outside-click handler if enabled.

Because all modals share one scrim, the visual effect of "two stacked
dialogs" is not doubly-darkened — they sit on one shared darkened
background.

---

## Focus integration

### Focus trap

When a Modal-layer entry is active:
- Tab / Shift+Tab cycles focus within descendants of the entry
  (typically Dialog + its content tree).
- Focus cannot escape to Main layer.
- On entry push: framework captures the pre-push focused widget and
  stores it on the entry. On removal: focus is restored.

Overlay layer entries do NOT trap focus by default — dropdowns let
Tab key keep cycling through the main widget tree underneath. If a
dropdown wants to trap focus while open, it sets an entry-level flag
(`trapsFocus = true`) checked by the focus dispatcher.

### Keyboard routing

Key events go to:
1. Focused widget (if any), via its `onKeyDown`.
2. If unconsumed: LayerStack's topmost key-handling entry (usually
   the topmost Overlay / Modal).
3. If still unconsumed: MenuBar's accelerator dispatch.
4. If still unconsumed: application-level shortcuts.

This order means a focused text input inside a dialog gets first
shot at every key, including Escape — which it uses for its own
cancel semantics. If it returns unconsumed, Dialog's onEscape fires
to close the dialog.

---

## Performance

- **push / remove**: O(1) amortized.
- **dispatchXXX**: O(total entries across all layers). Usually < 10
  entries; cheap.
- **paintLayers**: O(Σ entries' paint complexity).
- **Memory**: per entry ≈ 200 bytes (function objects + metadata).
  For 20 active entries: 4 kB.

### Tooltip layer efficiency

Tooltips are the most frequent push/remove churn (every hover
in/out in warm mode). To avoid O(n) std::vector shifts, the
Tooltip layer uses a slot-recycle vector with a free list — dropped
entries just mark their slot free, next push reuses.

---

## Debug overlay support

The framework's debug overlay (Ctrl+Shift+D) renders an inspector
of LayerStack state:

- Per-layer entry count with insertion-order numbers.
- Each entry's bounds outlined + debugName shown.
- Highlighted entry under pointer (hit-test preview).
- Scrim boundary visualization.

Useful for catching "dropdown doesn't appear" or "click fell through
the wrong way" bugs during development.

---

## Common patterns

### A widget owning one overlay

```cpp
class FwDropDown : public Widget {
public:
    void toggle() {
        if (m_overlay.valid()) close();
        else                    open();
    }

private:
    void open() {
        OverlayEntry e;
        e.owner = this;
        e.bounds = computePopupRect();
        e.paint = [this](UIContext& ctx) { paintPopup(ctx); };
        // ... other callbacks ...
        e.onDismiss = [this]() { /* close-cleanup */ };
        m_overlay = ctx.layerStack().push(OverlayLayer::Overlay, std::move(e));
    }
    void close() { m_overlay.cancel(); /* manual cleanup */ }

    OverlayHandle m_overlay;
};
```

### A widget spawning multiple overlays

```cpp
class ContextMenu {
public:
    void showSubmenu(...) {
        auto submenuHandle = ctx.layerStack().push(OverlayLayer::Overlay, ...);
        m_submenuHandles.push_back(std::move(submenuHandle));
    }
    // On parent dismiss: all submenus auto-remove (their handles in the vector)
};
```

### Cascading close on outside click

Default behavior: outside click on topmost entry dismisses it. This
means clicking outside a submenu closes the submenu (top), then
clicking outside again closes the parent menu. Users don't usually
need to do this — one outside click cascades closing all submenus
because each one's onDismiss removes the next-outer too, but that
requires explicit wiring.

Cleaner: the parent ContextMenu's onDismiss iterates and removes any
child submenu handles it's tracking. Keeps the parent in control of
the full menu chain's lifetime.

---

## Testing

Unit tests for LayerStack live in `tests/test_fw2_LayerStack.cpp`:

1. `PushIncreasesCount` — push → entryCount bumps.
2. `RemoveDecrementsCount` — remove → entryCount drops.
3. `HandleMoveTransfersOwnership` — moving a handle doesn't double-
   remove.
4. `HandleDestructorRemoves` — handle going out of scope removes
   entry.
5. `TopmostHitFirst` — two entries on same layer: topmost wins
   hit-test.
6. `ModalBlocksBelow` — entry with modal=true: lower entries don't
   receive events.
7. `OutsideClickDismisses` — click outside dismissable entry with
   modal=false: entry removed, click falls through.
8. `EscapeDismissesTopmost` — Escape → topmost entry's onEscape
   fires (defaults to dismiss).
9. `ModalScrimShared` — multiple modals → one scrim paint.
10. `CascadedDismissFiresInOrder` — dismissing parent that holds
    child handles correctly removes children first.
11. `PaintOrderBottomUp` — entries paint in insertion order within
    layer; layers in Modal → Toast order.
12. `TooltipLayerSlotRecycle` — repeated tooltip push/remove reuses
    slots without growth.
13. `FocusTrapOnModal` — modal active: Tab cycles within, not out.
14. `FocusRestoredOnRemove` — remove modal: pre-push focus restored.

Plus widget-level tests that exercise each overlay consumer.

---

## Migration notes

v1's `paintOverlay()` pattern is fully replaced. Ports per widget
spec — every v1 widget that drew popups rewrites to use OverlayEntry.

v1's modal-dialog paint hack (ad-hoc scrim drawn per panel) is
replaced by the shared Modal-layer scrim.

No backward-compat; v1 and v2 framework don't mix. During
transition, the app runs entirely on v1 or entirely on v2 — there's
no partial migration that doubles up infrastructure.

---

## Open questions

1. **Per-frame rate limits?** If a toast spams one push/remove per
   frame (buggy caller), the layer could OOM over time. Add a
   debug-assertion at >100 entries per layer?

2. **Entry prioritization within a layer?** Two popups open at once,
   one should always be on top (e.g., error dialog over ongoing
   confirm). Currently pure insertion order; a `priority` field
   could override. Wait for real use case.

3. **Async dismissal?** Some UIs fade out before actually removing.
   Currently entry is painted until `remove()` is called; callers
   handle fade-out via their own paint animation + delayed-remove.
   Works; might deserve an explicit `fadeOutMs` field later.

4. **Stacking limit?** Theoretically unbounded but practically 5+
   stacked dialogs indicates design trouble. Soft warn in debug
   builds when layer depth exceeds threshold.
