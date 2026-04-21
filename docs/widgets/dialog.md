# Dialog — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `fw::Dialog` class in `src/ui/framework/Dialog.h`.
**Architectural importance:** This is the **first Modal-layer
consumer** specified. ConfirmDialog, AboutDialog, PreferencesDialog,
ExportDialog, TextInputDialog all inherit from it. Getting Dialog
right sets the modal-layer contract for every other dialog.

---

## Intent

A **modal window** that floats above the main UI, draggable by its
title bar, dismissible via close button / Escape / Cancel button.
Provides structure (title + content + button bar) and the modal
semantics (input blocked for non-modal layers, focus trapped within
dialog, scrim visible behind).

Dialog is a **base class** — concrete dialogs (ConfirmDialog,
Preferences, Export) subclass it to populate content. Bare Dialog
can also be instantiated if the caller wants full control over the
content area.

## Non-goals

- **Non-modal floating panels.** Dialogs are strictly modal. Floating
  inspector panels or detachable tools are future widgets.
- **Multi-window dialogs.** A dialog opens its own floating surface
  within the main window. Spinning up a native OS window for each
  dialog is out of scope (desktop feel, not taken advantage of by
  our single-SDL-window model).
- **Dialog stacking with distinct scrims.** When a dialog opens
  another dialog, the scrim doesn't double up; there's one scrim
  across all Modal-layer entries at a time. Topmost dialog owns
  interaction.
- **Document-modal scope.** All dialogs are app-modal (block all
  input outside themselves). Per-document or per-window scope isn't
  a concept YAWN needs.

---

## Visual anatomy

```
    ┌──────────────────────────────────────────┐
    │                                           │  ← scrim (dim overlay across full window)
    │           ┌────────────────────────┐     │
    │           │ Title text          ☓  │     │  ← title bar
    │           ├────────────────────────┤     │
    │           │                         │    │
    │           │                         │    │
    │           │    content area         │    │  ← content (caller-populated)
    │           │                         │    │
    │           │                         │    │
    │           ├────────────────────────┤     │
    │           │        [ Cancel ] [ OK ] │    │  ← button bar
    │           └────────────────────────┘     │
    │                                           │
    └──────────────────────────────────────────┘
```

Parts, in paint order:
1. **Scrim** — full-window dim overlay, `palette.scrim`, covering
   main + overlay layers below. Painted as part of the Modal layer's
   own pass.
2. **Dialog body** — rounded rect, `palette.elevated`, with drop
   shadow.
3. **Title bar** — optional strip at top, `palette.surface`. Title
   text + close (×) button.
4. **Content** — caller-populated widget.
5. **Button bar** — optional strip at bottom. Buttons right-aligned
   by default; left-aligned "secondary actions" possible via config.
6. **Border** — 1 px `palette.border` around the body.

---

## Layer integration

Dialogs live on **Layer 1 (Modal)**. Pushing a Dialog's
OverlayEntry:

- **Scrim**: the Modal layer has its own "scrim" paint step before
  its entries' paints. Scrim covers the full window and blocks
  input from reaching Layer 0 (Main).
- **Modal flag**: entries on Modal are always modal-true — click
  outside the dialog body but inside the window hits the scrim, not
  the Main layer's widgets. Scrim consumes the click and (optionally)
  dismisses the dialog (configurable).
- **Stacking**: multiple dialogs stack by insertion order. Only the
  topmost receives input; lower dialogs appear "below" visually but
  inert.

### Dismissal

| Trigger | Result |
|---|---|
| Click × button | Fires `onClose(Reason::UserClose)`, removes the entry |
| Click outside dialog (on scrim) | Fires `onClose(Reason::OutsideClick)` IF `setDismissOnScrimClick(true)` (default false) |
| Escape key | Fires `onClose(Reason::Escape)`, removes entry |
| Primary button (Enter key) | Fires `onSubmit`, removes entry (unless onSubmit calls `keepOpen()`) |
| Cancel button | Fires `onClose(Reason::Cancel)`, removes entry |
| Programmatic `close()` | Fires `onClose(Reason::Programmatic)`, removes entry |
| Widget destructor | Removes entry; no callback |

---

## Focus trap

When a dialog opens:

1. The Modal layer's hit-test / key dispatch only routes to the
   dialog (and its scrim for outside clicks).
2. Tab from the last focusable widget in the dialog wraps to the
   first (and Shift+Tab wraps the other way).
3. Arrows, space, etc. work normally within the dialog.
4. When the dialog closes, focus returns to the widget that held it
   before open — tracked via `returnFocusTo` in show options, or
   auto-captured at open if not specified.

---

## Public API

```cpp
struct DialogOptions {
    Widget* returnFocusTo = nullptr;   // captured at show() if nullptr
    bool dismissOnScrimClick = false;
    bool showCloseButton = true;
    bool draggable = true;              // drag-by-title-bar
    float initialWidth = 480.0f;
    float initialHeight = 0.0f;          // 0 = content-driven
    std::optional<Point> initialPosition;// screen coords; nullopt = centered
};

enum class DialogCloseReason {
    UserClose,      // × button
    OutsideClick,
    Escape,
    Cancel,         // Cancel button
    Submit,         // primary button / Enter
    Programmatic,   // close() called
};

struct DialogButton {
    enum class Style { Default, Primary, Destructive };
    std::string label;
    std::function<void()> onClick;
    Style style = Style::Default;
    bool  closesDialog = true;          // if false, onClick must dismiss manually
    std::string accessShortcut;          // optional, e.g. "Enter", "Esc"
    bool  enabled = true;
};

class Dialog : public Widget {
public:
    Dialog();
    virtual ~Dialog();

    // Construction
    void setTitle(std::string);
    void setContent(Widget* content);   // takes ownership; content sized inside body

    // Buttons
    void addButton(DialogButton btn);
    void clearButtons();
    void setButtonBarEnabled(bool);      // default true

    // Show / close
    void show(const DialogOptions& opts = {});
    void close(DialogCloseReason reason = DialogCloseReason::Programmatic);
    bool isOpen() const;

    // Positioning
    void setSize(Size s);               // ignored until re-shown or live-adjusted
    void setPosition(Point p);          // screen coords
    void center();                       // re-centers in window

    // Callbacks
    void setOnShow(std::function<void()>);
    void setOnClose(std::function<void(DialogCloseReason)>);
    void setOnSubmit(std::function<void()>);    // Enter key or primary-button click

    // Flow control within onSubmit / onClose
    void keepOpen();                     // cancel pending close (e.g. validation failed)

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Convenience construction

```cpp
auto dlg = std::make_unique<Dialog>();
dlg->setTitle("Rename Track");
dlg->setContent(renameFormFlexBox);
dlg->addButton({ "Cancel", []{}, DialogButton::Style::Default });
dlg->addButton({ "Rename", [&]{ applyRename(); }, DialogButton::Style::Primary });
dlg->show();
```

For common patterns (confirm, text input), specialized subclasses
(ConfirmDialog, TextInputDialog) provide one-liner APIs.

---

## Drag-to-move

When `draggable = true`:

- Left-button drag on the title bar moves the dialog. Drag delta
  applied directly to `position`.
- Dialog clamps to stay at least half within the window (user can't
  drag it entirely off-screen).
- Drag tracking uses Widget capture — the dialog gets all mouse
  events until release.

---

## Gestures

### Pointer

| Gesture | Result |
|---|---|
| Left-drag on title bar | Move dialog. |
| Click × | Close (UserClose). |
| Click on scrim (outside body) | Close (OutsideClick) if configured. |
| Any click inside body | Passes through to the specific widget hit. |

### Keyboard

| Key | Action |
|---|---|
| `Escape` | Close (Escape reason). |
| `Enter` | If focused widget doesn't consume Enter: activate the Primary-style button. Fires `onSubmit`. |
| `Tab` / `Shift+Tab` | Cycle focus within dialog (trapped). |
| Button's `accessShortcut` | Activate that button's `onClick`. |

---

## Layout contract

Dialog has a peculiar layout situation because it lives on the Modal
layer, not the widget tree. Its own `onMeasure` / `onLayout` are
used internally when the Modal layer invokes them during its paint
pass.

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size Dialog::onMeasure(Constraints c, UIContext& ctx) {
    // Measure title bar
    float titleH = m_showTitleBar ? theme().metrics.controlHeight + 4 : 0;

    // Measure button bar
    float buttonBarH = 0;
    if (!m_buttons.empty() && m_buttonBarEnabled) {
        buttonBarH = theme().metrics.controlHeight + 2 * theme().metrics.baseUnit;
    }

    // Content measured with constraints minus titlebar and buttonbar.
    Constraints cc;
    cc.minW = cc.maxW = m_initialWidth;     // tight width (caller-specified)
    cc.minH = 0; cc.maxH = c.maxH - titleH - buttonBarH - 40;   // scrim margin
    Size contentS = m_content ? m_content->measure(cc, ctx) : Size::zero();

    float w = m_initialWidth;
    float h = titleH + contentS.h + buttonBarH;
    if (m_initialHeight > 0) h = m_initialHeight;

    return {w, h};
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

Standard flow: title bar at top, content in middle, button bar at
bottom. Button bar lays out its buttons right-aligned.

### Size policy

Dialog sizes itself. Parent is the Modal layer, which doesn't
constrain.

### Relayout boundary

**Yes** — dialog is its own world; content changes don't bubble out.

### Caching

Measure cache key: `(initialWidth, initialHeight, titleH, buttonBarH,
content version)`. Typical usage: dialog opens, measures once, caches;
stays valid until closed.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.elevated` | Dialog body fill |
| `palette.surface` | Title bar fill |
| `palette.scrim` | Scrim (full-window dim) |
| `palette.border` | Body outline |
| `palette.dropShadow` | Shadow |
| `palette.textPrimary` | Title text |
| `palette.textSecondary` | × close button glyph |
| `palette.accent` | Primary button fill |
| `palette.error` | Destructive button fill |
| `palette.controlBg` | Default button fill |
| `metrics.controlHeight` | Button / title bar heights |
| `metrics.cornerRadius` | Body corners |
| `metrics.baseUnit` | Internal padding |

---

## Events fired

- `onShow()` — after dialog is visible on Modal layer.
- `onClose(reason)` — before dismissal; caller can call `keepOpen()`
  to cancel.
- `onSubmit()` — Enter key or primary button clicked.
- FocusEvent for dialog itself (gains/loses focus).

---

## Invalidation triggers

### Measure-invalidating

- `setTitle` (title bar height unchanged, but text width might be
  checked for truncation; paint-only actually)
- `setContent`
- `addButton`, `clearButtons`, `setButtonBarEnabled`
- `setSize`
- DPI / theme / font (global)

### Paint-only

- `setPosition` (dialog moves, no measure change)
- Focus / hover / press states
- Button enabled/disabled changes

---

## Focus behavior

- **Focus trap** — Tab within the dialog cycles through focusable
  children and buttons, never leaves the dialog until it closes.
- **Initial focus** — first focusable descendant gains focus on
  show. Caller can override by calling `requestFocus` on a specific
  widget post-show.
- **Return focus** — tracked via DialogOptions.returnFocusTo (or
  auto-captured at show).

---

## Accessibility (reserved)

- Role: `dialog` (ARIA).
- `aria-modal="true"`.
- `aria-labelledby` pointing at the title text.
- `aria-describedby` for content if caller provides an ID.
- Scrim has `aria-hidden="true"` — it's purely visual.

---

## Animation

- **Show**: 120 ms opacity fade in + slight scale (0.97 → 1.0) for
  "poof" effect.
- **Close**: 80 ms opacity fade out.
- **Scrim**: 150 ms opacity fade in on show, 80 ms fade out on
  close.
- **Drag**: no animation; 1:1 tracking.

---

## Test surface

Unit tests in `tests/test_fw2_Dialog.cpp`:

### Lifecycle

1. `ShowPushesModalEntry` — `show()` adds entry to Modal layer.
2. `CloseRemovesEntry` — `close()` removes.
3. `IsOpenTracks` — reflects open/close state.
4. `DestructorCleansUp` — destroying open dialog removes its entry.

### Buttons

5. `AddButtonAppearsInBar` — button rendered in button bar.
6. `PrimaryStyleRendersAccent` — Primary style uses accent color.
7. `DestructiveStyleRendersError` — error color.
8. `ClickButtonFiresCallback` — `onClick` fires.
9. `DefaultClosesDialog` — default button click dismisses
   (closesDialog=true is default).
10. `KeepOpenSuppressesClose` — button with closesDialog=true but
    onClick calls `keepOpen()` → dialog stays.

### Escape / Enter

11. `EscapeCloses` — Escape key fires onClose(Escape).
12. `EnterActivatesPrimary` — Enter fires primary button's onClick.
13. `EnterWhenTextInputFocusedPassesThrough` — focused TextInput
    consumes Enter normally; dialog doesn't steal.

### Drag

14. `DragTitleBarMoves` — drag on title bar moves dialog.
15. `DragClampedToWindow` — dragging partially off-screen clamps.
16. `DraggableFalseNoMovement` — setDraggable(false) ignores drag.

### Scrim click

17. `ScrimClickClosesWhenEnabled` — dismissOnScrimClick=true closes
    on outside click.
18. `ScrimClickBlockedByDefault` — default false → click ignored.

### Focus trap

19. `TabCyclesWithinDialog` — Tab from last focusable wraps to
    first; never leaves.
20. `FocusReturnsToCallerOnClose` — close → focus returns to
    returnFocusTo widget.

### Scrim behavior

21. `MainLayerBlocked` — while dialog open, clicks on Layer 0
    widgets are blocked by scrim.
22. `StackedDialogsOnlyTopReceivesInput` — two dialogs open: only
    topmost responds to input.

### Callbacks

23. `OnShowFires` — once on show.
24. `OnCloseFiresWithReason` — fires with correct reason enum.
25. `OnSubmitFiresOnEnter` — Enter fires onSubmit.

---

## Migration from v1

v1 `fw::Dialog` in `src/ui/framework/Dialog.h` exists and is used
by AboutDialog, ConfirmDialog, ExportDialog, PreferencesDialog,
TextInputDialog.

v1 dialogs paint themselves via paintOverlay() called by the panel
tree — no layer abstraction. v2 moves to the Modal layer.

Migration:
1. v2 Dialog replaces v1. All five existing subclasses migrate.
2. Preferences and Export have significant content; those migrations
   are the biggest.
3. TextInputDialog and ConfirmDialog are near-trivial refactors;
   ConfirmDialog has its own v2 spec (next file).

Breaking changes: the `show()` / `close()` paths are different —
v1 uses a boolean `m_open` flag checked each frame; v2 pushes/
removes OverlayEntry explicitly. Subclass paint code can be
copy-pasted with minor adjustments (theme tokens, focus handling).

---

## Open questions

1. **Multi-step wizards?** Dialog with Next / Back / Finish buttons
   and paginated content. Compose: Dialog with content = TabView with
   no tab strip + custom navigation. Or build a dedicated
   `FwWizardDialog`. Deferred.

2. **Resizable dialogs?** Export dialog might want to be resizable
   (progress log area fills extra space). Add `setResizable(bool)`
   with corner drag handle. v2.1 extension.

3. **Modal scoping?** Strict app-modal only. No document-modal or
   widget-modal.

4. **Animation alt:** slide-from-top vs fade-scale. Currently spec'd
   as fade-scale. If designer wants slide-from-top for Windows 11
   style, add `setShowAnimation(ShowAnim::Fade / Slide)` option.

5. **Scrim dismissible via Esc even when dialog focused text input
   consumes Esc?** Edge case. Currently: text input's Esc takes
   priority (unfocus / cancel). Next Esc closes dialog. Acceptable.
