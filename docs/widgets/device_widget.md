# DeviceWidget — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** `DeviceWidget`, `DeviceHeaderWidget` in
`src/ui/framework/`.
**Related:** practically every primitive spec — DeviceWidget is the
archetype composite that uses Button, Toggle, Knob, Label,
VisualizerWidget, InstrumentDisplayWidget, and more.

---

## Intent

The **single-device panel** that represents one instrument or one
audio/MIDI effect in YAWN's detail view. Every device in a track's
effect chain, plus the track's instrument, renders as a DeviceWidget.
Used in snap-scroll device chains, the detail panel, and anywhere a
device's parameters need to be surfaced.

Structurally it's a vertical composition:

- **Header** — name, color, on/off toggle, presets, collapse, delete.
- **Body** — parameter knobs (via Grid or FlexBox) + optional
  instrument displays + optional visualizer area.

DeviceWidget is a **container** — it holds child widgets representing
parameters. It doesn't know what the parameters mean; it orchestrates
header + body layout and uniform look. Subclasses or factory
functions populate the body for specific device types (Reverb
device, FM synth, drum rack, etc.).

## Non-goals

- **Actual audio processing.** DeviceWidget is UI only; processing
  is in the audio engine. The widget reads/writes parameter values
  through caller-provided callbacks.
- **Preset management logic.** Widget exposes a "Preset" dropdown
  slot; caller wires it to the preset system.
- **MIDI Learn engine.** Right-click a knob to trigger learn —
  caller handles via the knob's `onRightClick` callback; widget
  doesn't own learn state.
- **Drag-to-reorder in the device chain.** That's the device-chain-
  panel's concern, which hosts a list of DeviceWidgets.

---

## Visual anatomy

```
    ┌──────────────────────────────────────────────┐
    │▓ ○ Reverb         ▼ Preset▼   ═ ☒  ×  ⋯    ▓│  ← header (colored strip)
    ├──────────────────────────────────────────────┤
    │                                                │
    │  ( ◯ )   ( ◯ )   ( ◯ )   ( ◯ )               │  ← parameter knob grid
    │  Size   Damp    Mix      Width                │
    │                                                │
    │  ( ◯ )   ( ◯ )   ( ◯ )   ( ◯ )               │
    │  HP      LP     Pre     PreG                  │
    │                                                │
    │ ┌──────────────────────────────────┐          │
    │ │ ░░░░░░ visualizer (optional) ░░░ │          │  ← visualizer (spectrum / osc / etc.)
    │ └──────────────────────────────────┘          │
    │                                                │
    └──────────────────────────────────────────────┘
```

Parts:
1. **Header strip** — colored (device type or track color), fixed
   height. Contains:
   - Device type icon or expand / collapse arrow.
   - Device name label (click to rename via `FwTextInput` inline
     edit).
   - Bypass toggle (circle indicator: filled = on / bypassed =
     hollow).
   - Preset dropdown (FwDropDown).
   - Device-specific buttons (e.g., "Edit" for VST3 with an external
     editor).
   - Remove button (×).
   - Overflow / more (⋯) — opens device context menu.
2. **Body** — content area. Variable height based on device type.
3. **Optional visualizer** — oscilloscope, spectrum, or a bespoke
   InstrumentDisplayWidget.
4. **Border** — 1 px around the entire DeviceWidget.

---

## Header composition

A DeviceHeader is itself a composite using FlexBox:

```
[expand ▾] [icon] [name (editable)]   [spacer flex=1]   [preset ▼] [toggle ⊙] [×] [⋯]
```

All sub-widgets wired through the DeviceWidget's API so the caller
doesn't build them manually.

### Preset dropdown

`FwDropDown` populated by caller via `setPresets(std::vector<std::string>)`
and `setPresetCallback(std::function<void(int)>)`. Optional — devices
without presets (e.g., Oscilloscope visualizer effect) hide the
preset slot.

---

## Body composition

Body is a single child widget — typically a FlexBox or Grid of
knobs + displays. The caller constructs whatever layout suits the
device:

```cpp
auto* body = new Grid();
body->setColumns({TrackSize::px(60), TrackSize::px(60), TrackSize::px(60), TrackSize::px(60)});
body->addChild(makeKnob("Size"),  {.row=0, .col=0});
body->addChild(makeKnob("Damp"),  {.row=0, .col=1});
body->addChild(makeKnob("Mix"),   {.row=0, .col=2});
body->addChild(makeKnob("Width"), {.row=0, .col=3});
// ... second row of knobs ...
body->addChild(visualizer, {.row=2, .col=0, .colSpan=4});

deviceWidget->setBody(body);
```

DeviceWidget stacks header on top of body via internal FlexBox
(vertical), with consistent padding and background rendering.

---

## States

| State | Trigger | Visual cue |
|---|---|---|
| **active (default)** | default | Full-opacity header + body |
| **bypassed** | bypass toggle off | Body visibly dimmed 50%; header toggle hollow |
| **collapsed** | expand arrow clicked | Only header visible; body hidden |
| **hovered** | pointer over header | Header brightens slightly |
| **focused** | any descendant has focus | Subtle accent border |
| **selected** | `setSelected(true)` | Accent-colored border (used in device chain for current selection) |
| **error** | `setError(msg)` | Red-tinted header + error badge; body may still be usable |

---

## Public API

```cpp
class DeviceWidget : public Widget {
public:
    DeviceWidget();
    explicit DeviceWidget(std::string deviceName);

    // Identity
    void setName(std::string name);
    const std::string& name() const;
    void setAccentColor(Color c);              // header tint (device type / track color)

    // Header layout
    void setHeaderHeight(float px);            // default controlHeight + 4
    void setShowBypassToggle(bool);            // default true
    void setShowPresetDropdown(bool);           // default true
    void setShowRemoveButton(bool);             // default true
    void setShowCollapseArrow(bool);            // default true

    // Body content
    void setBody(Widget* body);                 // takes ownership
    Widget* body() const;
    void setBodyPadding(Insets);                // default {12, 16, 12, 16}

    // Bypass
    void setBypassed(bool, ValueChangeSource = ValueChangeSource::Programmatic);
    bool isBypassed() const;

    // Collapsed
    void setCollapsed(bool);
    bool isCollapsed() const;

    // Presets
    void setPresets(std::vector<std::string> names);
    void setCurrentPreset(int idx);
    int  currentPreset() const;

    // Selection (for device-chain UX)
    void setSelected(bool);
    bool isSelected() const;

    // Error state
    void setError(std::optional<std::string> msg);
    // Non-nullopt: header shows error badge + msg as tooltip.

    // Callbacks
    void setOnBypassChanged(std::function<void(bool)>);
    void setOnPresetChanged(std::function<void(int)>);
    void setOnRemoveRequested(std::function<void()>);
    void setOnNameChanged(std::function<void(const std::string&)>);
    void setOnCollapseChanged(std::function<void(bool)>);
    void setOnHeaderRightClick(std::function<void(Point screen)>);
    void setOnSelected(std::function<void()>);

    // Custom header buttons (e.g., "Edit" for VST3)
    void addHeaderButton(DeviceButton btn);   // appended to header
    void clearHeaderButtons();

    // Accessibility
    void setAriaLabel(const std::string&);
};

struct DeviceButton {
    std::string label;          // or icon-only
    std::optional<GLuint> iconTex;
    std::function<void()> onClick;
    std::string tooltip;
};
```

### Callback semantics

- **`onBypassChanged`** — fires when user clicks bypass toggle or
  programmatic `setBypassed(v, User)` is called. Not for Programmatic
  no-op.
- **`onPresetChanged`** — from preset dropdown selection.
- **`onRemoveRequested`** — × button clicked. Caller decides whether
  to actually remove (may show confirmation).
- **`onNameChanged`** — after inline rename completes.
- **`onCollapseChanged`** — arrow toggled.
- **`onHeaderRightClick`** — anywhere on the header strip. Caller
  typically opens a DeviceContextMenu with "Move up", "Move down",
  "Duplicate", etc.
- **`onSelected`** — fires when DeviceWidget gains a click that wasn't
  a known header or body interaction (a plain header click selects
  the device in its chain).

---

## Gestures

### Header

| Gesture | Result |
|---|---|
| Click on header area (no button) | Select device (fires `onSelected`), focus body. |
| Click bypass toggle | Toggle bypass; fires `onBypassChanged`. |
| Click preset dropdown | Opens FwDropDown; selection fires `onPresetChanged`. |
| Click × | Fires `onRemoveRequested`. |
| Click collapse arrow | Toggle collapsed; fires `onCollapseChanged`. |
| Click custom header button | That button's `onClick`. |
| Click ⋯ overflow | Fires `onHeaderRightClick` (yes — same callback, since it's a menu trigger). |
| Double-click name | Inline rename via FwTextInput. |
| Right-click header | Fires `onHeaderRightClick`. |

### Body

Gestures pass through to child widgets (knobs, toggles, etc.). No
widget-level gestures at the body layer beyond child dispatching.

### Keyboard

| Key (DeviceWidget focused) | Action |
|---|---|
| `Space` | Toggle bypass. |
| `Delete` | Fires `onRemoveRequested`. |
| `F2` | Start inline rename. |
| `C` | Toggle collapse. |
| `Tab` | Dive into body (first focusable child). |

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size DeviceWidget::onMeasure(Constraints c, UIContext& ctx) {
    float headerH = m_headerHeight > 0 ? m_headerHeight
                                        : theme().metrics.controlHeight + 4;
    float bodyH = 0;
    if (!m_collapsed && m_body) {
        Constraints cc = c;
        cc.maxH = c.hasBoundedHeight() ? c.maxH - headerH : INFINITY;
        Size bodyS = m_body->measure(cc, ctx);
        bodyH = bodyS.h + m_bodyPadding.vert();
    }
    float w = c.hasBoundedWidth() ? c.maxW : 320.0f;
    float h = headerH + bodyH;
    return c.constrain({w, h});
}
```

Collapsed → body contributes zero height.

### `onLayout(Rect b, UIContext& ctx)`

1. Lay out header at `{bounds.x, bounds.y, bounds.w, headerHeight}`.
2. If not collapsed: lay out body at `{bounds.x + padding.left,
   bounds.y + headerHeight + padding.top, bounds.w - padding.horiz(),
   bounds.h - headerHeight - padding.vert()}`.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Fixed }
```

Height is content-driven. Width stretches — device chain UI sets
explicit widths via snap-scroll.

### Relayout boundary

**Opt-in.** Collapsing / expanding changes height, which propagates.
Setting boundary on avoids that; caller typically doesn't.

### Caching

Measure cached on `(constraints, header height, body measure version,
collapsed state, body padding)`. Body recomposition (setBody)
invalidates.

---

## Collapse animation

When `setCollapsed(true)`:
1. Body's height animates from current to 0 over 200 ms (ease-out).
2. Widget's own height participates in the animation; parent sees
   the widget shrink smoothly.

When `setCollapsed(false)`:
1. Body is laid out with its measured size.
2. Animates from 0 to full height over 200 ms.

During animation, SizePolicy effectively becomes "shrinking" — the
parent must handle a changing child size each frame. FlexBox handles
this by re-measuring on each frame; for perf, device-chain panels
that host many DeviceWidgets should avoid animating more than one at
a time.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.surface` | Body background |
| `palette.elevated` | Header default fill (tinted with accent) |
| `palette.border` | Widget border, divider between header and body |
| `palette.textPrimary` | Header name text |
| `palette.textSecondary` | Disabled / bypassed dim |
| `palette.accent` | Selected border, active indicators |
| `palette.error` | Error-state header tint + badge |
| `metrics.controlHeight` | Header height baseline |
| `metrics.cornerRadius` | Outer corners |
| `metrics.borderWidth` | Borders |
| `metrics.baseUnit` | Header internal padding + body padding |
| `metrics.fontSize` | Name label |
| `metrics.fontSizeSmall` | Preset dropdown, subscript text |

---

## Events fired

See API above — each user action has a corresponding callback.

---

## Invalidation triggers

### Measure-invalidating

- `setName`
- `setBody` (new body widget)
- `setHeaderHeight`, `setBodyPadding`
- Show/hide any header button (`setShowBypass`, `setShowPreset`, etc.)
- `addHeaderButton`, `clearHeaderButtons`
- `setCollapsed` (changes total height)
- Body child's measure invalidation (bubbles normally)
- DPI / theme / font

### Paint-only

- `setBypassed`, `setSelected`, `setError`
- `setAccentColor`, `clearAccentColor`
- `setCurrentPreset`
- Header button hover / press states
- Focus / hover transitions

---

## Focus behavior

- DeviceWidget is tab-focusable: Tab sets focus on the widget's
  header, arrow-keys or Tab dive into body.
- Tab from body last focusable → exits DeviceWidget.
- Selected state is persistent (via `setSelected`) separate from
  focus; multiple widgets can be focused at different times but
  only one is "selected" in a chain.

---

## Accessibility (reserved)

- Role: `group` or `region` (ARIA), with `aria-labelledby` =
  header name text.
- `aria-expanded` for collapsed state.
- Children participate in their own accessibility roles (knob =
  slider, toggle = switch, etc.).

---

## Animation

- **Header hover** fade: 80 ms.
- **Collapse / expand**: 200 ms height animation.
- **Selected-state border**: 100 ms fade-in.
- **Error-state header tint**: 150 ms color swap.
- **Bypass dim**: 150 ms opacity fade on body.

---

## Test surface

Unit tests in `tests/test_fw2_DeviceWidget.cpp`:

### Header

1. `NameRendered` — header shows device name.
2. `NameRenameFlow` — double-click name → FwTextInput overlay;
   Enter fires onNameChanged.
3. `BypassToggleFlips` — click bypass → fires onBypassChanged.
4. `SetBypassedProgrammatic` — setBypassed(true, Programmatic)
   dims body, no callback.
5. `RemoveButtonFires` — × fires onRemoveRequested; doesn't remove.
6. `CollapseArrowToggles` — fires onCollapseChanged; height
   animates.

### Header buttons

7. `AddHeaderButton` — custom button appears in header bar.
8. `ClearHeaderButtons` — removes all customs.
9. `HeaderButtonClickFires` — custom button's onClick fires.

### Presets

10. `SetPresetsPopulatesDropdown` — preset dropdown has the given
    items.
11. `PresetChangeFires` — selection fires onPresetChanged.
12. `NoPresetDropdownWhenEmpty` — setPresets({}) hides the slot.

### Body

13. `SetBodyAddsChild` — setBody(widget) makes it a child.
14. `BodyMeasureCachedSeparately` — body's measure works normally;
    DeviceWidget wraps.
15. `BodyRepaintsIndependently` — a knob in body updating doesn't
    force DeviceWidget re-measure.

### States

16. `BypassedDimsBody` — bypass makes body 50% opacity.
17. `SelectedShowsBorder` — setSelected(true) draws accent border.
18. `ErrorShowsBadge` — setError("foo") displays error tint +
    badge.
19. `CollapsedHidesBody` — measure returns only headerH.

### Gestures

20. `HeaderClickSelects` — click on header area (not on a button)
    fires onSelected.
21. `BodyClickDoesNotSelect` — clicks in body (on a knob) don't
    fire onSelected.
22. `RightClickHeader` — fires onHeaderRightClick with screen pos.

### Keyboard

23. `SpaceBypassToggles` — focused + space fires bypass toggle.
24. `DeleteRemoves` — fires onRemoveRequested.
25. `F2StartsRename` — simulates opening rename overlay.
26. `CCollapses` — C key toggles collapse.

### Cache

27. `MeasureCacheHit` — repeated measure with same state cached.
28. `CollapseInvalidates` — setCollapsed bumps measure.
29. `BypassPaintOnly` — setBypassed doesn't bump measure.

---

## Migration from v1

v1 has `DeviceWidget` and `DeviceHeaderWidget` as separate files.
The API is roughly similar but less uniform.

Migration:
1. Consolidate into v2 DeviceWidget with inlined header (or a
   private DeviceHeader sub-widget).
2. Standardize callback names (some v1 ones are inconsistent).
3. Remove ad-hoc v1 "accent draw" hacks in favor of accent token.

---

## Open questions

1. **Drag-to-reorder in a device chain?** v1 has this via the
   device-chain-panel doing its own drag logic. v2 could push drag
   handling into DeviceWidget (drag the header to rearrange).
   Likely needs a `setDragReorderable(bool)` flag. Defer until v2
   rollout on the device chain.

2. **Variable-width header for long names?** Currently header spans
   full widget width. If device-chain widgets are narrow (200 px),
   long names get truncated. Already handled by Label's ellipsis.

3. **Preset save button?** "Save as preset" typically goes in the
   header's ⋯ menu or preset dropdown's context menu. Caller-
   wired; widget has no opinion.

4. **Undock device?** Open device in its own window (for plugin
   editors). Custom header button, caller-wired. Fine.

5. **Per-device help tooltip?** `setHelpTooltip(str)` on the widget;
   render a `?` icon in the header that shows a tooltip. Nice; add
   if requested.

6. **Shared visualizer panel across devices?** An A/B-view where
   two DeviceWidgets' visualizers show side-by-side at higher
   detail. Higher-level panel concern, not DeviceWidget's job.
