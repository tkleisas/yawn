# YAWN UI v2 — Widget Specification Index

Each widget in the v2 framework is defined by a verbose spec before
code is written. Specs follow the template established by
[`fader.md`](fader.md) — the order of sections is not strict but every
field is expected to be addressed explicitly. A section with nothing
to say should still be present, marked *n/a* with a reason.

The spec is the source of truth. Implementation disagreeing with it
is a bug; if behavior evolves, the spec is updated first, then code
follows.

See [`../ui-v2-architecture.md`](../ui-v2-architecture.md) for the
framework-level concepts every widget inherits (layer stack, measure/
layout cache, relayout boundaries, events, theme).

---

## Spec template

Every widget spec has the following sections, in this order:

1. **Intent** — one paragraph on what the widget is for.
2. **Non-goals** — what it explicitly does NOT do, so scope creep has
   a written enemy.
3. **Visual anatomy** — ASCII or image, plus a table of parts and
   their paint order.
4. **States** — all the states the widget can be in, their triggers,
   and their visual cues. State combination rules.
5. **Gestures** — pointer (left / right / middle), scroll, keyboard,
   touch. Every gesture a user might try should have a documented
   response, even if that response is "ignored".
6. **Value semantics** — the public API. Setters, getters, callbacks,
   clamping rules, callback firing rules.
7. **Drag / input sensitivity** (if applicable) — explicit formulas
   and DPI handling.
8. **Layout contract** — `onMeasure`, `onLayout`, size policy,
   relayout boundary status, caching behavior.
9. **Theme tokens** — every `theme().palette.*` or `theme().metrics.*`
   the widget reads.
10. **Events fired** — what the widget emits (distinct from what it
    listens to).
11. **Invalidation triggers** — categorized as measure-invalidating
    vs paint-only.
12. **Focus behavior** — tab order, focus ring, auto-focus.
13. **Accessibility (reserved)** — placeholders; AT support is v3+.
14. **Animation** — where the widget animates, where it doesn't, why.
15. **Test surface** — what unit tests will exist.
16. **Migration from v1** — differences from the current
    implementation on master.
17. **Open questions** — anything deferred to a future version.

Reviewers should be able to read a spec and know, before any code
exists, exactly how the widget will behave in every situation they
can think of.

---

## Status tracking

| ✓ | Spec | Widget | Notes |
|---|---|---|---|
| ✓ | [fader.md](fader.md) | **FwFader** | Vertical linear control. First spec — proof of concept for the template. |
| ✓ | [button.md](button.md) | **FwButton** | Push button. Clickable, optionally toggleable. |
| ✓ | [toggle.md](toggle.md) | **FwToggle** | Two-state on/off. Simpler state machine than Button. |
| ✓ | [knob.md](knob.md) | **FwKnob** | Circular rotary control. Shares drag infrastructure with Fader. Includes secondary value display for LFO / automation / CC modulation overlay. |
| ✓ | [label.md](label.md) | **Label** | Text display. Measures from font; truncation policy. |
| ✓ | [text_input.md](text_input.md) | **FwTextInput** | Single-line editable text. Focus, caret, selection, clipboard. |
| ✓ | [number_input.md](number_input.md) | **FwNumberInput** | Numeric text entry with clamping and formatting. Inline text-entry for knobs/faders. |
| ✓ | [dropdown.md](dropdown.md) | **FwDropDown** | Select-one from a list. Primary consumer of the Overlay layer. |
| ✓ | [checkbox.md](checkbox.md) | **FwCheckbox** | Tri-state optional (off / on / indeterminate). |
| ✓ | [radio_group.md](radio_group.md) | **FwRadioGroup** | Mutually exclusive selection. Composed of FwRadioButtons. |
| ✓ | [scroll_bar.md](scroll_bar.md) | **FwScrollBar** | Standalone scrollbar. Used by scroll containers. |
| ✓ | [progress_bar.md](progress_bar.md) | **FwProgressBar** | Determinate / indeterminate progress indicator. |
| ✓ | [tooltip.md](tooltip.md) | **Tooltip** | Hover-triggered informational bubble. Overlay-layer consumer. |
| ✓ | [context_menu.md](context_menu.md) | **ContextMenu** | Right-click popup. Overlay-layer consumer. Submenus supported. |

### Layout containers

| ✓ | Spec | Widget | Notes |
|---|---|---|---|
| ✓ | [flex_box.md](flex_box.md) | **FlexBox** | Row / column layout engine. Flexbox semantics (grow, shrink, basis, justify, align). Mirrors Yoga. |
| ☐ | fw_grid.md | **FwGrid** | Row-major grid (current v1 naming). Auto-wraps children into rows of fixed column count. Simple; good for device parameter knob grids. |
| ☐ | grid.md | **Grid** | 2D CSS-grid-style layout. Named tracks, span declarations, explicit placement. More powerful than FwGrid, used by the Mixer and Session panel layouts. |
| ☐ | table.md | **Table** | Tabular data: header row, body rows, sortable columns, selection model, virtualized scroll for large datasets. Used by MIDI Monitor, preset browsers. |
| ✓ | [snap_scroll_container.md](snap_scroll_container.md) | **SnapScrollContainer** | Horizontal snap-to-page scroll viewport with nav buttons. Used by device chain. |
| ✓ | [scroll_view.md](scroll_view.md) | **ScrollView** | Generic scrollable viewport (vertical + horizontal, free-form). New in v2. Different from SnapScrollContainer's paged behavior. |
| ✓ | [stack.md](stack.md) | **Stack** | Z-order stacking within a single rect — children overlap, drawn in order. Useful for overlays, visual layers, composite widgets. |
| ☐ | split_view.md | **SplitView** | Resizable panel splitter (horizontal or vertical). User-draggable divider. Used by main window layout. |
| ☐ | tab_view.md | **TabView** | Tab strip + content area. One child visible at a time, tab header switches. |

### Dialogs & higher-level composites

| ✓ | Spec | Widget | Notes |
|---|---|---|---|
| ☐ | dialog.md | **Dialog** | Modal dialog base. Title bar, content, drag-to-move, Escape/Enter. Consumer of the Modal layer. |
| ☐ | confirm_dialog.md | **ConfirmDialog** | Yes/No confirmation. Subclass of Dialog. |
| ☐ | device_widget.md | **DeviceWidget** | Composite: header + parameter grid + knob bank + visualizer. Used for every effect / instrument panel. |
| ☐ | waveform_widget.md | **WaveformWidget** | Zoom/scroll waveform display with transient markers, warp markers, loop overlay. Heavy custom paint. |
| ☐ | visualizer_widget.md | **VisualizerWidget** | Oscilloscope + spectrum display. Data-driven, updates every frame. |
| ☐ | instrument_display_widget.md | **InstrumentDisplayWidget** | FM algorithm diagram, ADSR curve, oscillator waveform preview. Composable displays for instrument panels. |
| ☐ | menu_bar.md | **MenuBar** | Application menu bar. Keyboard-navigable, mnemonics, submenus. |

---

## Spec-writing order (proposed)

Each batch should land as its own PR. The order is chosen so each
widget's spec can reference earlier ones without forward references.

1. **FwFader** ✓ (done; this is the proof of concept)
2. **FwButton, FwToggle** — establish gesture patterns (click, hover, disabled) without value complexity.
3. **FwKnob** — reuses Fader's drag spec; adds rotational paint.
4. **Label** — fixes "labels overlap" by defining a strict measure-from-font contract. Truncation policy, ellipsis rendering, line wrapping.
5. **FwTextInput, FwNumberInput** — focus, keyboard, IME basics, clipboard. NumberInput consumed by Fader/Knob's inline text entry.
6. **FlexBox** — the first container spec. Relies on Label's measure behavior to get text-based stretch right.
7. **FwDropDown** — first Overlay-layer consumer. Forces the LayerStack infrastructure to be real, not just specified. Keyboard nav, filter/search, upward opening.
8. **ContextMenu, Tooltip** — once DropDown works, these are variations.
9. **FwGrid** — straightforward; use FlexBox internally.
10. **ScrollView, FwScrollBar** — standalone scrolling. Precursor to Table virtualization.
11. **SnapScrollContainer** — adapt existing to the new framework.
12. **Grid (2D)** — the CSS-grid-style container. Harder: named tracks, placement rules.
13. **SplitView, TabView, Stack** — remaining layout containers.
14. **Dialog, ConfirmDialog** — once all content widgets exist.
15. **Table** — the hardest. Virtualized scroll, column resize, sort, selection, column types. Builds on ScrollView + Label + several primitives.
16. **Composite widgets** — DeviceWidget, WaveformWidget, etc. Use everything built before.
17. **MenuBar** — last because it has application-level concerns (keyboard accelerators, focus transfer).

Each spec discussion stands on its own. Nothing forces the order if a
different one makes sense later; this is the path of least surprise.

---

## Infrastructure specs

These aren't widgets but need their own design docs before v2 lands:

| ✓ | Doc | Covers |
|---|---|---|
| ✓ | [`../ui-v2-architecture.md`](../ui-v2-architecture.md) | Framework-level: layers, cache, events, theme, multi-toast, debug overlay |
| ☐ | `../ui-v2-layer-stack.md` | Full LayerStack API + OverlayEntry lifecycle + hit-test ordering + dismissal rules |
| ☐ | `../ui-v2-measure-layout.md` | Detailed constraint rules, cache internals, relayout boundary semantics, invalidation propagation, animation interaction |
| ☐ | `../ui-v2-theme.md` | Theme struct + loading + runtime switching + JSON file format |
| ☐ | `../ui-v2-events.md` | Full event model: raw → gesture → command, dispatching, capture, focus |

The architecture doc is the high-level overview; these infrastructure
docs expand specific areas when the abstract overview isn't enough
for implementation.
