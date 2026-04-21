# Tooltip — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Related:** [dropdown.md](dropdown.md) — sister Overlay-layer
consumer; Tooltip uses the same LayerStack infrastructure but lives
on Layer 3 (Tooltip) rather than Layer 2 (Overlay) so it always
draws **above** an open dropdown.

---

## Intent

A **hover-triggered informational bubble** that appears near a
target widget after a short delay and disappears when the pointer
leaves. Used for: button labels that are icon-only ("what does this
do?"), truncated Labels ("what's the full text?"), knob value
tooltips ("Cutoff: 1250 Hz"), extra context on compact UI elements.

Tooltips are the primary way a dense UI provides "progressive
disclosure" — users learn on demand without cluttering the surface.

## Non-goals

- **Interactive content.** Tooltips are read-only. Clickable
  tooltips (tool palettes, inline menus) are `ContextMenu` or
  custom `Popover` widgets (future).
- **Persistent tooltips.** No "pin this open" affordance. If
  persistent, it's not a tooltip.
- **Rich HTML / markdown.** Plain text only (multi-line supported
  via embedded newlines or wrap, but no formatting).
- **Latching on click.** Some platforms show tooltips on focus-gain
  (keyboard-only users). v2 Tooltip shows ONLY on hover with a
  delay. Keyboard-focus tooltips are a future accessibility pass
  concern.

---

## Architecture

Tooltips are not widgets that live in the widget tree. They're
**registered against a trigger widget** via a helper:

```cpp
Tooltip::attach(widget, "Mute track");
Tooltip::attach(btn, [](const TooltipContext& ctx) {
    return std::string{"Value: "} + std::to_string(currentValue);
});
```

The `TooltipManager` (owned by App) tracks:
- The hovered widget (via `Widget::onHoverChanged`).
- Hover start timestamp.
- Whether a tooltip is currently shown.
- The tooltip's OverlayEntry handle.

When the pointer stays on a widget with an attached tooltip for
`showDelay` milliseconds, the manager pushes a `TooltipEntry` on
Layer 3. When the pointer leaves (or moves to a different widget
with its own tooltip), the entry is removed.

This model means **any widget** can participate in tooltips without
needing to know about the Tooltip class. The widget just has to
fire hover events (which the framework base class does already).

---

## Visual anatomy

```
    ┌────────────────────────────┐
    │  Mute track                │   ← tooltip body
    └────────────▲───────────────┘
                 │                    ← arrow pointing at trigger widget
              trigger
              widget
```

Parts:
1. **Body rectangle** — rounded, `elevated` fill, drop shadow.
2. **Text** — `textPrimary`, single-line by default. Wraps at
   configurable `maxWidth`.
3. **Arrow** — small triangle pointing at the trigger widget's
   center. Optional; enabled by default.

---

## Positioning

Tooltip position relative to its trigger widget follows a priority
list. For each position, we check: would the tooltip fit in the
window without being clipped? First one that fits wins.

Default priority (below trigger → above → right → left):

1. **Below** — tooltip arrow points up; body below the trigger.
2. **Above** — arrow down; body above.
3. **Right** — arrow left; body to the right.
4. **Left** — arrow right; body to the left.

Caller can override priority via `setPreferredPosition(Position)`
if a specific side reads better for that trigger (e.g. tooltips for
mixer channels probably want "above" because the channel strip is
tall and below would be outside the panel).

Horizontal clamping: body slides horizontally to stay within the
window edge (arrow stays pointed at trigger center even when body
is offset).

---

## States

| State | Trigger | Visual |
|---|---|---|
| **hidden** | default | nothing rendered |
| **pending** | pointer arrived on trigger; within `showDelay` window | nothing rendered yet |
| **visible** | shown; pointer still over trigger | tooltip at resolved position |
| **fading-out** | pointer left trigger; fade animation running | opacity lerping down |

The pending state is invisible but tracked by the manager — if the
pointer moves to a different tooltip-attached widget during the
delay, the first pending tooltip is canceled and a new one starts.

---

## Show / hide timing

```cpp
struct TooltipTiming {
    float showDelay      = 0.6f;    // seconds before tooltip appears
    float fastShowDelay  = 0.1f;    // short delay when user is "warm"
    float warmTimeout    = 1.5f;    // how long after last tooltip hides before going cold again
    float fadeInMs       = 80.0f;
    float fadeOutMs      = 120.0f;
};
```

**Warm-up behavior:** once a tooltip has appeared and the user moves
to another tooltip-attached widget within `warmTimeout` seconds of
the last tooltip disappearing, the new tooltip uses `fastShowDelay`
(100 ms) instead of `showDelay` (600 ms). This matches standard OS
tooltip behavior: the first tooltip feels deliberate; subsequent
tooltips feel immediate.

Configurable globally via `TooltipManager::setTiming(TooltipTiming)`.

---

## Public API

```cpp
struct TooltipContext {
    Widget* target;
    Point pointerScreen;
    // Can add more if dynamic tooltips want them (e.g., current value
    // of the widget being hovered).
};

class Tooltip {
public:
    // Static tooltip text
    static void attach(Widget* w, std::string text);

    // Dynamic tooltip — computed each time the tooltip is about to show.
    // Useful for "current value" tooltips that shouldn't cache.
    static void attach(Widget* w, std::function<std::string(const TooltipContext&)>);

    static void detach(Widget* w);

    // Position hint for this widget (overrides default priority).
    enum class Position { Auto, Below, Above, Right, Left };
    static void setPreferredPosition(Widget* w, Position p);

    // Max wrap width before tooltip wraps. 0 = no wrap, single line.
    // Default 320 logical px.
    static void setMaxWidth(Widget* w, float px);
};

class TooltipManager {
public:
    // Installed once by App; not user-facing.
    static TooltipManager& instance();

    // Called by framework's hover dispatch.
    void onWidgetHovered(Widget* w);
    void onWidgetUnhovered(Widget* w);
    void onPointerMoved(Point screenPos);

    // Per-frame tick for delay accounting + animation.
    void tick(float dtSec);

    // Global settings
    void setTiming(TooltipTiming t);
    void setGloballyEnabled(bool);   // user preference to disable tooltips entirely
};
```

Calling conventions:

- `Tooltip::attach(btn, "Save project")` is the common case — static
  string, called once at widget creation.
- `Tooltip::attach(knob, [](auto& ctx) { return std::to_string(knob->value()) + " Hz"; })`
  is for dynamic tooltips — the closure captures the knob and returns
  fresh text each show.
- `Tooltip::detach(w)` — called automatically when a widget is
  destroyed (framework cleans up the manager's registry).

---

## Layout contract

The Tooltip body paints as an OverlayEntry on Layer 3. The body
itself isn't a standalone widget in the tree — it's just a
render closure. That means there's no `onMeasure` / `onLayout` in
the usual sense; size is computed inline at show time:

```cpp
// When tooltip shows:
float textScale = fontScale();
float maxW = attachedMaxWidth(w);  // 320 default
float textW = (maxW > 0) ? min(maxW, font.textWidth(text, scale))
                         : font.textWidth(text, scale);
int lineCount = wrapIfWidthExceeded(text, textW);
float textH = lineCount * font.lineHeight(scale);

float padX = theme().metrics.baseUnit * 2;
float padY = theme().metrics.baseUnit;

tooltipBody = { resolvedX, resolvedY, textW + 2*padX, textH + 2*padY };
```

No caching at the tooltip level — each show computes fresh. The
overhead is negligible (one text-width measurement).

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.elevated` | Body fill |
| `palette.textPrimary` | Text |
| `palette.border` | Optional body outline (1 px, subtle) |
| `palette.dropShadow` | Drop shadow |
| `metrics.fontSize` | Text size (often smaller than body — default `fontSizeSmall` or `fontSize × 0.9`) |
| `metrics.cornerRadius` | Body corners |
| `metrics.baseUnit` | Internal padding |

---

## Events fired

None — tooltips are view-only. No user-facing callbacks.

---

## Invalidation triggers

Tooltip content is re-computed each time the tooltip shows
(dynamic text via closure) or on attach() (static text). No widget-
tree invalidation involved; the overlay layer repaints only when
tooltip state changes.

**Continuous repaint during fade:** active fades set a one-shot
continuous-paint flag; framework repaints until fade completes.

---

## Focus behavior

Tooltips don't interact with focus. The trigger widget keeps focus;
tooltips just appear/disappear around it.

---

## Accessibility (reserved)

- Tooltip text is associated with the trigger widget's
  `aria-describedby` for screen readers.
- Keyboard-only users: future v3 pass will also show tooltips on
  focus-gain (for users who can't hover). For v2.0: hover-only.

---

## Animation

- **Show:** 80 ms opacity fade in.
- **Hide:** 120 ms opacity fade out.
- **Target-to-tooltip transition:** no animation when moving between
  tooltip-attached widgets during warm window — the old tooltip fades
  out while the new one fades in (crossfade effect).

---

## Test surface

Unit tests in `tests/test_fw2_Tooltip.cpp`. Simulated clock for
delay timing.

### Basic show / hide

1. `AttachStatic` — `Tooltip::attach(w, "Hi")` registers without
   error.
2. `HoverStartsPendingTimer` — hovering an attached widget starts
   the show-delay countdown.
3. `ShowAfterDelay` — after `showDelay` ms of hover, tooltip appears
   on Layer 3.
4. `LeaveCancelsPending` — pointer leaves before delay → no tooltip
   shown.
5. `LeaveHidesShown` — pointer leaves after tooltip shown → fade-
   out begins, entry removed after fade.

### Warm-up

6. `WarmUsesFastDelay` — two tooltip shows within warmTimeout use
   fastShowDelay for the second.
7. `ColdReturnsAfterTimeout` — gap exceeding warmTimeout returns to
   slow delay.

### Positioning

8. `BelowByDefault` — tooltip placed below trigger when there's
   room.
9. `AboveWhenNoRoomBelow` — near window bottom → tooltip above.
10. `RightFallback` — no room above or below → tooltip to the right.
11. `LeftFallback` — no room on three sides → tooltip to the left.
12. `PreferredPositionRespected` — `setPreferredPosition(Above)`
    uses above unless it doesn't fit.
13. `HorizontalClamp` — wide tooltip near window right edge shifts
    left to stay in bounds; arrow still aligns with trigger center.

### Dynamic content

14. `DynamicTextComputedAtShow` — closure-based tooltip computes
    text each show, not at attach.
15. `DynamicReflectsCurrentState` — knob value tooltip reflects
    the knob's current value each show.

### Wrapping

16. `WrapsAtMaxWidth` — long text with default maxWidth (320) wraps
    into multi-line.
17. `NoWrapWithZeroMaxWidth` — `setMaxWidth(0)` forces single line,
    overflows horizontally (caller's problem).

### Global controls

18. `DisabledGloballySkipsShow` — `setGloballyEnabled(false)` never
    shows tooltips.
19. `DetachRemoves` — `Tooltip::detach(w)` removes attached info;
    subsequent hover no-op.
20. `WidgetDestructionCleansUp` — destroying a widget removes its
    tooltip registration automatically (via framework hook).

### Overlay interaction

21. `TooltipAboveDropdown` — tooltip and dropdown both open: tooltip
    on Layer 3 draws above the dropdown on Layer 2.
22. `DropdownClickDismissesTooltip` — clicking the trigger widget
    (or anywhere) while tooltip is visible hides it immediately
    (no wait for mouse-leave).

---

## Migration from v1

v1 has no tooltip system. All informational text is hardcoded near
the element (label below knob, title in dialog, etc.). v2 Tooltip is
a net-new feature.

Gradual rollout: start by attaching tooltips to icon-only buttons in
the transport bar and VST3 browser, then extend coverage.

---

## Open questions

1. **Keyboard-focus tooltips?** Defer to v3 accessibility pass.
2. **Tooltip delay per-widget override?** Currently global timing.
   Could add `Tooltip::setShowDelay(w, ms)` if needed, but no
   strong use case yet.
3. **Tooltip on mobile / touch?** Tap-and-hold for ~500 ms shows
   tooltip. Out of scope until touch support matters.
4. **Rich tooltips (icons, colored text)?** `FwRichTooltip` future
   widget — attaches a widget subtree instead of a string. Useful
   for plugin preset previews. Deferred.
5. **Tooltip across panels?** What if the trigger is scrolled out of
   view while tooltip is showing? Check each frame: if trigger's
   `globalBounds()` no longer intersects the window, hide tooltip.
