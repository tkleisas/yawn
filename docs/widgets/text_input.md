# FwTextInput — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the text-input logic embedded in
`src/ui/framework/TextInputDialogWidget.h` and ad-hoc text edit code
scattered across v1 panels.

---

## Intent

A **single-line editable text field** with a blinking caret, character
selection, and clipboard support. The base of every text-entry
interaction in YAWN: project name, track name, clip name, search
filter, preset name, user-defined text in visual clips, inline knob
value entry.

FwTextInput is deliberately focused on *single-line* text. Multi-line
editing (for scripts, notes, code) is a future `FwTextArea` widget
that will share much of the same plumbing but adds word-wrap and
vertical navigation.

## Non-goals

- **Multi-line.** That's FwTextArea later.
- **Rich text / syntax highlighting.** Plain text only. Custom shader
  editors and script editors need dedicated widgets or external tools.
- **Undo/redo history.** Single-step undo (Ctrl+Z undoes the most
  recent edit) is planned; full history with branching is out of
  scope for v2.0.
- **Autocomplete / suggestions.** A composite `FwCompleteInput`
  decoration can wrap FwTextInput with a suggestion dropdown when
  needed; the base widget stays focused on editing.
- **Built-in validation / formatting.** FwTextInput accepts any
  characters from the IME. `FwNumberInput` subclasses to add numeric
  clamping; other typed inputs (email, path, regex) would similarly
  wrap this.
- **Full BiDi / RTL.** The widget handles LTR text and the basic
  keyboard navigation of LTR. RTL support, cursor shaping, and
  combining-character handling are framework-wide concerns flagged
  for a future v3 pass.

---

## Visual anatomy

```
    ┌──────────────────────────────────┐ ←── bounds
    │                                   │
    │  Track 1|                         │     caret at "|" position
    │                                   │
    └──────────────────────────────────┘
       └─ padding

    ┌──────────────────────────────────┐
    │                                   │
    │  Track [█1█]                     │     selection highlighted
    │                                   │
    └──────────────────────────────────┘

    ┌──────────────────────────────────┐
    │                                   │
    │  Enter track name…                │     placeholder (dim) shown when empty + unfocused
    │                                   │
    └──────────────────────────────────┘
```

Parts, in paint order:

1. **Background fill** — rounded rect, `controlBg` (focused variants
   swap to `elevated`).
2. **Border** — 1 px, `border` (focused: `accent`, 2 px).
3. **Selection highlight** — filled rect behind selected text, drawn
   ONLY when selection exists. `accent.withAlpha(80)`.
4. **Text** — `textPrimary` when focused or non-empty, `textDim` for
   placeholder. Font from theme metrics.
5. **Caret** — 1 px vertical line at insertion position. Blinks at
   1 Hz (500 ms on / 500 ms off). `textPrimary`.
6. **Focus ring** — the border itself, recolored + thickened, serves
   as the focus ring. No separate inset ring.

When the text is longer than the bounds, the view **scrolls
horizontally** to keep the caret in view. Content left of the visible
window is clipped; a 4-px gradient fade at the left/right edges hints
that there's more.

---

## States

| State | Trigger | Visual cue |
|---|---|---|
| **idle-empty** | not focused, text empty | placeholder shown dim |
| **idle-filled** | not focused, has text | text shown primary |
| **hovered** | pointer over bounds | subtle background brighten |
| **focused-editing** | focus received | border recolored + thicker, caret visible + blinking |
| **focused-selecting** | dragging or keyboard-selecting | selection highlight painted |
| **focused-composing** | IME dead-key / composition pending | composition text underlined (IME convention) |
| **disabled** | `setEnabled(false)` | 40% desaturate; no input, no caret |
| **readonly** | `setReadOnly(true)` | enabled, focusable, caret visible, but typing / paste / cut blocked |

`focused-*` states compose with optional `hovered`. `readonly` is
orthogonal to disabled: disabled means "completely inert", readonly
means "user can focus and select but not modify."

---

## Gestures

### Pointer — left button

| Gesture | Result |
|---|---|
| **Click** inside text | Move caret to clicked character position. Clear any selection. If not already focused, also take focus. |
| **Click** on placeholder | Take focus, caret at position 0. |
| **Double-click** on word | Select the word under cursor. |
| **Triple-click** | Select entire text. |
| **Drag** (press + move) | Extend selection from press position to current position. |
| **Shift-click** | Extend selection from current caret to click position. |

### Pointer — right button

| Gesture | Result |
|---|---|
| **Right-click** | Open standard edit context menu (Cut / Copy / Paste / Select All) via the Overlay layer. |

### Keyboard (when focused)

#### Navigation

| Key | Action |
|---|---|
| `Left` / `Right` | Move caret ±1 character |
| `Ctrl + Left/Right` | Move caret by word |
| `Home` / `End` | Caret to start / end of text |
| `Shift + above` | Extend selection instead of moving |

#### Editing

| Key | Action |
|---|---|
| `Backspace` | Delete selection OR character before caret |
| `Delete` | Delete selection OR character after caret |
| `Ctrl + Backspace` / `Ctrl + Delete` | Delete by word |
| printable character | Insert at caret (or replace selection) |
| `Ctrl + A` | Select all |
| `Ctrl + C` | Copy selection to clipboard |
| `Ctrl + X` | Cut selection (Copy + delete) |
| `Ctrl + V` | Paste clipboard at caret (or replace selection) |
| `Ctrl + Z` | Undo most recent edit |

#### Control flow

| Key | Action |
|---|---|
| `Enter` / `Return` | Fire `onSubmit(text)`. By default releases focus; configurable via `setReleaseFocusOnSubmit(false)`. |
| `Escape` | Fire `onCancel()`. Reverts to pre-edit value. Releases focus. |
| `Tab` / `Shift + Tab` | Fire `onSubmit(text)`, pass focus to next/previous widget. |

### IME

- Dead keys / composition sequences accumulate in a composition
  buffer; the partial text is drawn with an underline while the IME
  is active.
- Composition completion flushes characters to the text at the caret.
- Escape during composition cancels the composition, not the edit.
- Backspace during composition deletes composition characters one at
  a time before touching the real text.

Beyond this basic behavior, full IME support (candidate windows,
composition re-commit) is platform-dependent and handled by SDL3 — we
forward `SDL_EVENT_TEXT_EDITING` / `SDL_EVENT_TEXT_INPUT` events to
FwTextInput's handlers.

### Touch

Tap = click. Long-press opens the edit context menu (replaces right-
click).

---

## Public API

```cpp
class FwTextInput : public Widget {
public:
    using SubmitCallback = std::function<void(const std::string&)>;
    using ChangeCallback = std::function<void(const std::string&)>;
    using CancelCallback = std::function<void()>;

    FwTextInput();
    explicit FwTextInput(std::string initialText);

    // Content
    void setText(std::string t);                  // clears selection, caret to end
    void setTextSilently(std::string t);          // same but no onChange fire
    const std::string& text() const;
    bool isEmpty() const;

    // Placeholder
    void setPlaceholder(std::string t);
    const std::string& placeholder() const;

    // Selection & caret
    struct Selection { int start = 0; int end = 0; };
    Selection selection() const;                  // start == end means no selection
    int caretPosition() const;                    // == selection.end
    void setSelection(int start, int end);
    void selectAll();
    void clearSelection();
    void moveCaretTo(int pos, bool extendSelection);

    // Behavior
    void setReadOnly(bool);                       // default false
    void setMaxLength(int n);                     // 0 = unlimited
    void setFilter(std::function<bool(uint32_t codepoint)>);
    // Filter called on each input codepoint; return false to reject.
    // Useful for "digits only", "ASCII only", etc. FwNumberInput uses
    // this to reject anything non-numeric except "-" and "." in the
    // right positions.

    void setReleaseFocusOnSubmit(bool);           // default true
    void setSubmitOnBlur(bool);                   // default false — lost focus == cancel
    void setSelectAllOnFocus(bool);               // default false — but NumberInput overrides true

    // Sizing
    void setMinWidth(float w);
    void setPreferredWidth(float w);              // used in measure()
    void setHeight(float h);                      // fixed; defaults to controlHeight

    // Callbacks
    void setOnSubmit(SubmitCallback cb);          // Enter or Tab
    void setOnChange(ChangeCallback cb);          // every edit — typing, paste, delete
    void setOnCancel(CancelCallback cb);          // Escape

    // Focus control
    void requestFocus();
    void releaseFocus();

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

### Callback firing rules

- **`onChange`** — fires after every mutation to the stored text:
  keystroke, paste, cut, Delete/Backspace. Does NOT fire during IME
  composition (only when the composition commits). Does NOT fire for
  `setTextSilently`.
- **`onSubmit`** — fires on Enter, on Tab (before focus moves), and
  optionally on blur (if `setSubmitOnBlur(true)`).
- **`onCancel`** — fires on Escape. The widget restores the text it
  had when focus was gained.

### Undo semantics

v2.0 supports single-step undo:

- On focus gain, the current text is saved as the "before" state.
- `Ctrl+Z` at any time restores the "before" state — single keystroke
  undoes any amount of editing since focus.
- On successful submit, the "before" state is discarded.

This is much simpler than full undo history and covers ~95% of text-
input use cases (I typed too much, let me start over).

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size FwTextInput::onMeasure(Constraints c, UIContext& ctx) {
    float w = m_preferredWidth > 0 ? m_preferredWidth : 200.0f;   // sensible default
    if (m_minWidth > 0) w = std::max(w, m_minWidth);
    float h = m_fixedHeight > 0 ? m_fixedHeight : theme().metrics.controlHeight;
    return c.constrain({w, h});
}
```

Width is NOT driven by text content — unlike Label, a text input's
width is ideally **stable** regardless of what the user types, so the
surrounding layout doesn't jitter character by character. If the text
overflows width, it scrolls horizontally inside the input.

Callers can override via `setPreferredWidth` or FlexBox flex props.

### `onLayout(Rect b, UIContext& ctx)`

Store bounds. No children except the optional right-click context
menu, which lives on the Overlay layer when open.

### Size policy

```cpp
SizePolicy{ width = Flex, height = Fixed }
```

Default stretches horizontally in a FlexBox row. Callers who want a
fixed-width input (e.g. track-name field, exactly 200 px) set it via
`setPreferredWidth`.

### Relayout boundary

**Yes.** Height is fixed; width is constraint-driven but text content
doesn't affect widget size (horizontal scroll absorbs overflow).

### Caching

Measure cache: `(constraints, preferredWidth, minWidth, fixedHeight,
theme.controlHeight)`. Text changes are paint-only. Focus / selection
/ caret position changes are paint-only. This is critical for
typing-responsiveness performance — 60 Hz typing can't trigger
measures.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Background fill (idle, unfocused) |
| `palette.elevated` | Background fill (focused) |
| `palette.border` | Border (unfocused) |
| `palette.accent` | Border (focused), selection highlight (with alpha 80) |
| `palette.textPrimary` | Text + caret color |
| `palette.textDim` | Placeholder text |
| `palette.textOnAccent` | Text when over selection highlight (contrast) |
| `metrics.controlHeight` | Default height |
| `metrics.cornerRadius` | Corner radius |
| `metrics.borderWidth` | Default border thickness |
| `metrics.fontSize` | Font size |
| `metrics.baseUnit` | Internal padding |

---

## Events fired

- `onChange(text)` — after every mutation (subject to IME rule).
- `onSubmit(text)` — Enter, Tab, optional blur.
- `onCancel()` — Escape.
- `FocusEvent` — gain/loss from framework.

---

## Invalidation triggers

### Measure-invalidating

- `setPreferredWidth`, `setMinWidth`, `setHeight`
- DPI / theme / font (global epoch)

### Paint-only invalidating

- `setText`, `setTextSilently`
- `setPlaceholder`
- Any edit: insert, delete, selection change, caret move
- Focus gain/loss
- Hover transitions
- `setReadOnly`, `setEnabled`
- Caret blink timer (once every 500 ms, but the widget avoids full
  widget-tree invalidation — it just re-paints itself out of band
  via a framework-provided "blink redraw" hook).

The caret blink deserves a note: naïvely invalidating the entire
widget every 500 ms would work but also bloat the debug overlay
statistics. The framework provides a paint-only blink timer that the
widget consults in its paint function, without touching the cache
state.

---

## Focus behavior

- **Tab-focusable:** yes — this is an editable field, always focusable.
- **Auto-focus on click:** yes. Click anywhere inside the field takes
  focus.
- **Focus ring:** border itself, thickened and recolored to accent.
- **Focus loss behavior:** controlled by `setSubmitOnBlur(bool)`.
  Default is `false` — losing focus silently keeps the current text
  (no submit, no cancel). `true` treats blur as submit. Use the true
  variant for inline text entry where clicking elsewhere commits.

---

## Accessibility (reserved)

- Role: `textbox`.
- `aria-label`, `aria-readonly`, `aria-placeholder`, `aria-value`,
  `aria-selection` all reflected.

---

## Animation

- **Caret blink:** 500 ms on / 500 ms off, stepwise (no smooth fade).
- **Focus border:** 80 ms fade from `border` color to `accent`.
- **Selection highlight:** no animation; appears/disappears instantly.

---

## Test surface

Unit tests in `tests/test_fw2_TextInput.cpp`:

### Content & selection

1. `InitialTextSet` — `FwTextInput("hello")` has `text() == "hello"`.
2. `SetTextMovesCaretToEnd` — `setText("hi")` places caret at
   position 2, no selection.
3. `SelectAll` — `selectAll()` selects entire text.
4. `SetSelectionClamps` — out-of-range start/end clamped to valid
   range.
5. `CaretClampsToTextBounds` — `moveCaretTo(-5)` → caret=0.

### Keyboard editing

6. `TypingInsertsAtCaret` — "a" + "b" + "c" results in "abc".
7. `BackspaceDeletesBefore` — backspace at position 3 in "abc"
   leaves "ab", caret=2.
8. `DeleteDeletesAfter` — delete at position 0 in "abc" leaves "bc",
   caret=0.
9. `CtrlBackspaceDeletesWord` — `ctrl+backspace` deletes the word
   before caret.
10. `TypingReplacesSelection` — with selection 1–3 in "abcde", typing
    "X" leaves "aXde".
11. `ArrowKeysMoveCaret` — left arrow decrements caret by 1.
12. `CtrlArrowMovesByWord` — `ctrl+right` in "hello world" at pos 0
    jumps to position 5.
13. `ShiftArrowExtendsSelection` — `shift+right` from a collapsed
    caret creates a 1-char selection.

### Clipboard

14. `CtrlCCopies` — selection is placed on clipboard; text unchanged.
15. `CtrlXCuts` — selection copied + deleted.
16. `CtrlVPastes` — clipboard text inserted at caret.
17. `PasteReplacesSelection` — paste while selection exists replaces it.

### Submit / cancel

18. `EnterFiresSubmit` — Enter fires `onSubmit` with current text.
19. `EscapeFiresCancelAndReverts` — Escape restores "before-focus"
    text and fires `onCancel`.
20. `TabSubmitsAndReleasesFocus` — Tab fires `onSubmit`, focus moves.
21. `SubmitOnBlurFires` — with `setSubmitOnBlur(true)`, losing focus
    fires `onSubmit`.

### Focus

22. `ClickTakesFocus` — mouse click inside field focuses it.
23. `SelectAllOnFocus` — with `setSelectAllOnFocus(true)`, focus-gain
    selects all text.
24. `UndoRestoresPreFocus` — edit text, Ctrl+Z restores text present
    at focus-gain moment.

### Rendering / layout

25. `MeasureFixedWidth` — `setPreferredWidth(200)` returns width 200
    regardless of text length.
26. `TextOverflowsHorizontallyScrolls` — long text keeps caret in
    view; text before view origin is clipped.
27. `PlaceholderShownWhenEmpty` — empty + unfocused shows placeholder.
28. `PlaceholderHiddenOnFocus` — focusing empty field hides
    placeholder, shows caret at position 0.

### Constraints

29. `ReadOnlyBlocksEditing` — typing in readonly does nothing; selection
    / copy still work.
30. `DisabledIgnoresEverything` — disabled field can't be clicked
    into or typed into.
31. `MaxLengthTruncates` — `setMaxLength(5)` rejects insertion past 5
    characters; paste truncates to 5.
32. `FilterRejectsCharacters` — custom filter (only digits) blocks
    letter keys.

### Relayout boundary + cache

33. `TextChangeIsPaintOnly` — typing does not bump measure cache
    version.
34. `MeasureCacheHit` — second measure with same constraints + same
    sizing hints skips measure work.

---

## Migration from v1

v1 has no standalone `FwTextInput`; text-entry logic is embedded
across:

- `TextInputDialogWidget` — a modal dialog with a text field; its
  field is the closest thing to v1 FwTextInput.
- Browser panel search field — inlined directly.
- Various rename-popups — each slightly different.

v2 consolidates into the single FwTextInput. Migration:

1. Replace `TextInputDialogWidget`'s embedded field with FwTextInput.
2. Replace browser search field with FwTextInput.
3. Audit rename flows; each is 3–10 lines in v2.

No source-compatible path — this is a new widget. Callers who were
using `TextInputDialogWidget` for modal-dialog text entry keep using
that dialog (the dialog wraps an FwTextInput now).

---

## Open questions

1. **Sticky keyboard shortcuts?** Some platforms map Ctrl+Shift+Z as
   redo; we're not supporting redo in v2.0, so that shortcut is
   silently ignored. Should we stub it to a visible "nothing to
   redo" UI hint or just let it be? Probably the latter.

2. **Mac Cmd-key variant?** On macOS the clipboard shortcuts use Cmd
   not Ctrl. SDL3 can tell us the platform; should the widget adapt,
   or do we always treat Ctrl as the edit modifier? Currently
   proposing Ctrl on all platforms for uniformity; revisit if Mac
   users complain.

3. **Autofill / system-level suggestions?** Modern OS surfaces
   offer suggested completions. Probably not relevant to DAW use
   cases; leave disabled.

4. **Per-character styling?** Would allow "highlight the parts of
   the text that matched a search". Outside v2.0 scope; would need
   rich-text infrastructure that doesn't exist.

5. **Password / obscured mode?** Useful for future cloud features
   (sign-in dialogs). Trivial to add as `setObscured(bool)` that
   renders all characters as `•`. Deferred until a real use case
   lands.

6. **Right-click menu language?** The context menu entries should
   follow the app's locale. Currently hardcoded English; plan a
   translation pass for v3.
