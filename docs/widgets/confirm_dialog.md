# ConfirmDialog — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `fw::ConfirmDialog` in
`src/ui/framework/ConfirmDialogWidget.h`.
**Related:** [dialog.md](dialog.md) — ConfirmDialog is a thin
subclass adding the standard "yes/no confirmation" pattern.

---

## Intent

A **yes/no confirmation dialog** with a message, two buttons
(Cancel / Confirm), and a boolean callback. The canonical pattern for
destructive-action confirmations ("Delete track?", "Overwrite
preset?", "Close unsaved project?").

ConfirmDialog is a convenience subclass — the caller gets a single-
line API:

```cpp
ConfirmDialog::show("Delete track 3?",
                     "This action cannot be undone.",
                     [](bool confirmed) {
    if (confirmed) deleteTrack(3);
});
```

Everything else (Dialog lifecycle, button bar, Enter=confirm,
Escape=cancel, focus trap, scrim) is inherited from Dialog.

## Non-goals

- **Custom button labels and counts.** ConfirmDialog has exactly
  two buttons (Cancel, Confirm) with configurable labels but not
  configurable count. For more complex flows (3-button "Save / Don't
  Save / Cancel"), use base Dialog directly.
- **Checkbox "don't ask again".** Useful for repeated confirmations
  but out of scope for v2.0 — compose into base Dialog if needed.
- **Custom content (inputs, form fields).** That's TextInputDialog or
  a bespoke Dialog subclass, not ConfirmDialog.
- **Async confirmation (dialog stays open until server replies).**
  Dialog closes on button click by default. `keepOpen()` from base
  Dialog's callback is the escape hatch.

---

## Visual anatomy

```
    ┌───────────────────────────────┐
    │ Delete track?          ☓      │  ← title from show()
    ├───────────────────────────────┤
    │                                │
    │   This action cannot be       │
    │   undone.                     │  ← message body (wrapping Label)
    │                                │
    ├───────────────────────────────┤
    │          [ Cancel ] [ Delete ] │  ← standard button bar
    └───────────────────────────────┘
```

Structure inherited from Dialog — this widget just populates content
(the message) and adds the two buttons.

---

## Behavior

### Confirmation flow

```
User opens dialog → two outcomes:

  Cancel path (close without action):
    • User clicks "Cancel" button
    • User presses Escape
    • User clicks × close button
    • User clicks outside (if dismissOnScrimClick configured)
    → callback(false) fires, dialog closes

  Confirm path (action committed):
    • User clicks "Confirm" button
    • User presses Enter (Confirm button is primary-styled)
    → callback(true) fires, dialog closes
```

### Destructive styling

For destructive actions, the Confirm button uses `Destructive` style
(red fill from `palette.error`). This is the default when
`setDestructive(true)` is called. Matches OS conventions where
"Delete" / "Discard" are visually flagged.

```cpp
ConfirmDialog::show("Discard unsaved changes?",
                     "Your edits since last save will be lost.",
                     [](bool confirmed) { ... })
    .destructive()
    .confirmLabel("Discard")
    .cancelLabel("Keep editing");
```

Fluent builder pattern on the returned handle for pre-show
configuration.

---

## Public API

```cpp
class ConfirmDialogHandle {
public:
    ConfirmDialogHandle& title(std::string);
    ConfirmDialogHandle& message(std::string);
    ConfirmDialogHandle& confirmLabel(std::string);    // default "OK"
    ConfirmDialogHandle& cancelLabel(std::string);     // default "Cancel"
    ConfirmDialogHandle& destructive(bool = true);
    ConfirmDialogHandle& defaultAction(bool confirmed); // which side Enter/Esc prefer
    // default: Enter = Confirm, Escape = Cancel

    // Callback — if not set via show(cb), set here.
    ConfirmDialogHandle& onResult(std::function<void(bool confirmed)>);

    // Imperative close
    void close();
    bool isOpen() const;
};

class ConfirmDialog {
public:
    // One-liner convenience:
    static ConfirmDialogHandle show(std::string title,
                                     std::string message,
                                     std::function<void(bool confirmed)> cb);

    // For more control — pre-configure, then show():
    static ConfirmDialogHandle build();    // handle starts not-shown
};
```

Using the fluent builder:

```cpp
ConfirmDialog::build()
    .title("Remove plugin?")
    .message("Remove 'Serum' from Track 3 effect chain?")
    .confirmLabel("Remove")
    .destructive()
    .onResult([this](bool ok) { if (ok) removePlugin(3, "Serum"); })
    .show();   // pushes on Modal layer
```

---

## Gestures & keyboard

All inherited from Dialog:

- Click Confirm button → callback(true), close.
- Click Cancel button → callback(false), close.
- Escape → callback(false), close.
- Enter → callback(true), close (primary button activated).
- Click × → callback(false), close.

---

## Public API firing rules

- Callback fires **exactly once** per ConfirmDialog show cycle.
- Fires with `true` only via Confirm button or Enter.
- Fires with `false` via Cancel button, ×, Escape, outside-click
  (if enabled), programmatic close.

---

## Layout contract

Inherits from Dialog. Specific behavior:

### Content pane

The content pane is a single wrapping `Label` for the message,
with theme padding. Preferred content width: 400 logical px (matches
Dialog's default 480 dialog width minus padding).

```cpp
void ConfirmDialog::initContent() {
    auto* msg = new Label(m_message);
    msg->setWrap(Label::WrapMode::WordWrap);
    msg->setMaxWidth(420);
    msg->setPadding({20, 16, 20, 16});
    msg->setAlign(TextAlign::Start, VerticalAlign::Middle);
    setContent(msg);
}
```

### Button bar

Two DialogButtons:
- Cancel — Default style, `closesDialog = true`, `onClick` fires
  callback(false) then close is implicit.
- Confirm — Primary (or Destructive if flagged) style, `closesDialog
  = true`, `onClick` fires callback(true).

### Size policy

Dialog's defaults apply. ConfirmDialog typically sizes to:
- Width: 480 logical px.
- Height: ~140 + wrapped message lines.

Neither is user-resizable.

---

## Theme tokens consumed

Inherited from Dialog. Additionally:
- `palette.error` — Destructive confirm button fill (when
  `setDestructive(true)`).

---

## Events fired

- `onResult(bool)` — exactly once per dialog cycle.

---

## Invalidation triggers

Same as Dialog. Message changes invalidate layout (wrap recalc).
Button label changes invalidate layout (button bar measure).

---

## Focus behavior

- Initial focus on Cancel button (preference for conservative default
  — the user should press Enter explicitly to commit). Opt-in to
  "focus confirm by default" via `defaultAction(true)` builder if the
  action isn't destructive.
- Tab cycles between Cancel and Confirm within the dialog.

---

## Accessibility

- Role: `alertdialog` (ARIA — distinct from `dialog`; signals
  "important decision required").
- Message via `aria-describedby`.

---

## Animation

Inherits Dialog's show / close animations.

---

## Test surface

Unit tests in `tests/test_fw2_ConfirmDialog.cpp`:

1. `ShowOpensOnModalLayer` — `ConfirmDialog::show(...)` pushes
   entry.
2. `ConfirmFiresTrue` — click Confirm → callback(true).
3. `CancelFiresFalse` — click Cancel → callback(false).
4. `EscapeFiresFalse` — Escape → callback(false).
5. `EnterFiresTrue` — Enter → callback(true).
6. `CloseButtonFiresFalse` — × → callback(false).
7. `CallbackFiresOnce` — each show lifecycle fires exactly one
   callback.
8. `DestructiveChangesButtonColor` — setDestructive(true) renders
   Confirm with error color.
9. `CustomLabels` — confirmLabel("Remove") and cancelLabel("Keep")
   render correctly.
10. `DefaultActionConfirmFocusesConfirm` — with
    defaultAction(true), focus opens on Confirm button.
11. `DefaultActionCancelFocusesCancel` — default behavior.
12. `MessageWrapsLongText` — long message wraps within max width.
13. `DialogCenteredByDefault` — position is window-centered.
14. `CannotClickOutsideWhenNotConfigured` — scrim clicks blocked
    by default.
15. `FluentBuilderWorks` — build() → title() → message() → show()
    produces a shown dialog.
16. `CancelKeyboardShortcut` — accessShortcut registered for
    Cancel is "Esc".
17. `ConfirmKeyboardShortcut` — "Enter" for Confirm.
18. `CallbackNotFiredUntilShow` — build() without show() doesn't
    fire callback.

---

## Migration from v1

v1 `fw::ConfirmDialog` in
`src/ui/framework/ConfirmDialogWidget.h`:

```cpp
ConfirmDialog d;
d.setMessage("Delete track?");
d.setOnConfirm([]{...});
d.setOnCancel([]{...});
d.show();
```

v2 API is cleaner with the boolean-callback pattern:

```cpp
ConfirmDialog::show("Delete track?", "", [](bool ok){
    if (ok) deleteTrack();
});
```

Or the fluent builder for more config.

Migration:
1. Find all `new ConfirmDialog` call sites (~5–10 in the codebase).
2. Replace with static `show` or builder pattern.
3. Two-callback (`onConfirm` + `onCancel`) call sites collapse to
   single-callback `onResult`.

---

## Open questions

1. **"Don't ask again" checkbox?** Common for repeated
   confirmations. Adds state (persist preference somewhere).
   Compose into base Dialog for now; graduate to ConfirmDialog
   feature if the pattern becomes common.

2. **Three-button variants** (Save/Don't Save/Cancel)? Explicitly
   out of scope for ConfirmDialog — use base Dialog or a future
   ThreeButtonDialog.

3. **Icon in message (warning / error / info)?** Could prepend an
   icon in the content pane. Simple addition; add when visual
   design calls for it.

4. **Auto-focus Confirm for non-destructive actions?** Spec has
   defaultAction(false) default. Could auto-flip when destructive
   is false; makes Enter feel natural. Trade-off: risk of
   accidental confirm. Current spec is conservative.
