# WaveformWidget — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** `WaveformWidget` in
`src/ui/framework/WaveformWidget.h`.

---

## Intent

A **zoomable, scrollable audio waveform display** with interactive
markers for transients, warp points, loop region, playhead, and
automation breakpoints layered on top of the audio. The main
workhorse for audio-clip visualization: used in the sampler panel,
clip detail view, piano roll audio lane, and the arrangement's
per-clip waveform rendering.

Three combined concerns:

1. **Audio rendering** — stereo-aware waveform with min/max
   envelope, zoomed views with sample-level detail, efficient
   rendering of multi-minute buffers.
2. **Markers** — transients (auto-detected onsets), warp markers
   (for time-stretching), loop region, playhead.
3. **Automation overlay** — per-parameter breakpoint envelopes
   superimposed on the waveform, with per-lane color and
   interactive editing (add/drag/delete breakpoints).

Interactivity: click to create warp markers, drag to move them,
right-click to delete. Shift+click in overview bar to jump playhead.
Automation lanes are editable in the same paint space with
per-breakpoint drag handles.

## Non-goals

- **Spectral display.** Waveform only — time-domain. Spectrogram is
  a separate widget (future, not in v2.0).
- **Sample-level editing.** No in-widget sample surgery
  (destructive editing, pencil tool, region fades). The widget is
  for inspection + marker editing; sample edits happen through
  export/import workflows.
- **Audio playback itself.** Widget displays; audio engine plays.
  Callers wire playhead position to the widget via `setPlayheadTime`.
- **Multi-clip / multi-buffer displays.** One audio buffer per
  widget. Arrangement view places multiple WaveformWidgets (or a
  specialized per-track composite).

---

## Visual anatomy

```
   ┌─────────────────────────────────────────────────────────────┐
   │ ░░░░░░░░░░░░ overview minimap ░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │  ← overview bar
   │        ├────────zoomed view position──────┤                │    (Shift+click to seek)
   ├─────────────────────────────────────────────────────────────┤
   │   │    ▼                                 ▼              │    │
   │ ──┴────╯╲───────────╱╲──────────╲───────╱╲──────────╲───┴── │  ← main waveform (stereo: L)
   │                                                              │    play head │
   │                                                              │    transient markers ▼
   │ ──┬────╮╱───────────╲╱──────────╱───────╲╱──────────╱───┬── │  ← main waveform (R)
   │   │                                                    │    │
   │ ← warp markers │                               loop region [═══]
   │                                                              │
   ├─────────────────────────────────────────────────────────────┤
   │ ▓                                                            │  ← automation lane (volume)
   │    ╱──╲______                ________╱──╲                   │    (breakpoints + curve)
   │   ● (breakpoint)                                             │
   └─────────────────────────────────────────────────────────────┘
```

Parts (from top to bottom):
1. **Overview bar** — optional minimap showing entire buffer;
   highlighted region shows the zoomed view's extent.
2. **Main waveform area** — the zoomed view. Stereo splits top/bottom.
   Transient markers, warp markers, loop region, playhead all drawn
   here.
3. **Automation lane(s)** — optional, stacked below main waveform.
   Each lane has its own Y axis (0..1) and color.

---

## Waveform rendering

### Min/max envelope

For performance, raw audio samples are pre-computed into a
**min/max pyramid** — a multi-resolution tree of min and max values
at successively coarser time resolutions. At zoom level N, the
widget reads from the pyramid level where one bucket ≈ one viewport
pixel. Each bucket renders as a vertical line from min to max.

Pyramid built on `setAudioBuffer` (O(n) amortized), cached in the
widget. Large buffers (several minutes × 48 kHz × stereo) are
reasonable — pyramid uses ~4× the raw sample memory.

### Zoom levels

```cpp
struct ZoomState {
    float viewStart;       // seconds
    float viewEnd;         // seconds; viewEnd - viewStart = visible window
};
```

Helpers:
- `zoomIn(anchorTime)` — halve visible window, keep anchor fixed.
- `zoomOut(anchorTime)` — double visible window.
- `fitToView()` — set window to full buffer.
- `zoomToRange(t0, t1)` — show exactly this range.

Min zoom: ~10 samples visible (single-sample detail). Max zoom: full
buffer.

### Stereo display

- Mono buffer: single horizontal envelope in the middle.
- Stereo buffer: split top/bottom, left channel on top. Optional
  `setStereoMode(StereoMode::Sum)` to render summed mono.

### Performance

- Paint cost: O(viewport width) per channel — one envelope column
  per pixel.
- Memory: O(buffer samples) for raw + O(4 × buffer samples) for
  pyramid.
- No per-frame allocation — both buffers are fixed at setAudioBuffer
  time.

---

## Markers

### Transient markers

Auto-detected onsets. Displayed as small downward-pointing triangles
at the top of the waveform area.

```cpp
void setTransients(const std::vector<float>& timesSec);
void clearTransients();
```

Non-editable by default (they're detected by the engine). Right-click
a transient for a context menu (remove this transient, detect again,
etc.).

### Warp markers

User-editable points that map an original time to a target time.
Used for time-stretch alignment. Displayed as draggable handles on a
thin line below the waveform.

```cpp
struct WarpMarker {
    int id;                 // stable ID for edit operations
    float originalTimeSec;
    float targetTimeSec;
};

void setWarpMarkers(std::vector<WarpMarker> markers);
const std::vector<WarpMarker>& warpMarkers() const;

// Interactive editing:
void setWarpEditMode(bool enabled);   // enables add/drag/delete gestures
```

Gestures in warp-edit mode:
- **Click on waveform**: add a warp marker at click time.
- **Drag warp marker**: move it horizontally; fires
  `onWarpMarkerMoved(id, newTargetSec)`.
- **Right-click warp marker**: context menu (delete this marker).
- **Double-click warp marker**: reset its target to its original
  (straight line).

### Loop region

A highlighted range with two draggable edge handles.

```cpp
void setLoopRegion(std::optional<std::pair<float, float>> range);
void setLoopEditMode(bool enabled);
```

Gestures: drag left / right edge to resize; drag middle to move.

### Playhead

A vertical line at the current playback position. Continuously
updated by the caller. Always visible when in `[viewStart, viewEnd]`.

```cpp
void setPlayheadTime(float sec);
void setPlayheadColor(Color c);
void setShowPlayhead(bool);
```

Playhead updates are paint-only — setting time 60 Hz doesn't
invalidate measure.

---

## Automation overlay

**New in v2** (your requested feature). Lets callers layer per-
parameter automation envelopes directly on the waveform, either
beneath the audio (in a dedicated lane) or overlaid on top.

### Data model

```cpp
struct AutomationBreakpoint {
    int id;             // stable ID for edit operations
    float timeSec;
    float value01;       // normalized [0..1]; caller maps to actual param range
    // Optional: curve shape (linear / bezier / exponential)
    enum class Curve { Linear, Exponential, Logarithmic, Hold };
    Curve curve = Curve::Linear;
};

struct AutomationLane {
    std::string id;                  // caller-defined, e.g. "volume" or "pan"
    std::string label;                // displayed
    std::vector<AutomationBreakpoint> breakpoints;
    Color color;                      // lane accent
    bool  editable = true;
    bool  visible = true;
    AutomationLayout layout = AutomationLayout::Below;
};

enum class AutomationLayout {
    Overlay,   // drawn on top of the waveform in its own Y range (0..1 → scaled to main area height)
    Below,     // in a dedicated lane strip beneath the waveform
};
```

### Rendering

- **Overlay lanes**: curve drawn in `color.withAlpha(140)` over the
  main waveform area. Good when automation is occasional (e.g., a
  fader ride during a chorus).
- **Below lanes**: each lane gets its own horizontal strip below the
  main waveform (default lane height 40 logical px). Multiple below-
  lanes stack. Good for dense automation (multiple parameters
  automated simultaneously).

Each breakpoint is drawn as a small dot. The curve between adjacent
breakpoints follows the first breakpoint's `Curve` setting.

### Interactive editing

When `editable = true`:

| Gesture | Result |
|---|---|
| **Click on empty curve area** | Add a new breakpoint at that time/value. |
| **Drag breakpoint** | Move it in time + value. Fires `onBreakpointMoved(laneId, id, newTime, newValue)`. |
| **Right-click breakpoint** | Context menu (delete, change curve type). |
| **Double-click breakpoint** | Reset value to caller's configured default (if any). |
| **Shift+drag** | Constrain to vertical (value only) or horizontal (time only) based on initial drag direction. |

Adding / removing / moving breakpoints emits events; Table-like
pattern where Widget doesn't mutate data directly — emits events
that the caller applies to its model, then reflects back via
`setLanes`.

### Value scaling

All breakpoint `value01` is normalized. Caller provides optional
per-lane formatters:

```cpp
lane.valueFormatter = [](float v01) -> std::string {
    return std::format("{:.1f} dB", -60 + v01 * 60);
};
```

Widget shows formatted value next to a breakpoint on hover.

---

## Public API

```cpp
class WaveformWidget : public Widget {
public:
    WaveformWidget();

    // Audio buffer
    void setAudioBuffer(const float* samples, int numSamples,
                         int numChannels, float sampleRate);
    void clearAudioBuffer();

    // Zoom / scroll
    void setViewRange(float startSec, float endSec);
    void zoomIn(float anchorTimeSec);
    void zoomOut(float anchorTimeSec);
    void fitToView();

    // Markers
    void setTransients(std::vector<float> timesSec);
    void setWarpMarkers(std::vector<WarpMarker> markers);
    void setLoopRegion(std::optional<std::pair<float, float>> range);
    void setPlayheadTime(float sec);

    void setWarpEditMode(bool);
    void setLoopEditMode(bool);
    void setShowPlayhead(bool);
    void setShowTransients(bool);

    // Automation
    void setAutomationLanes(std::vector<AutomationLane> lanes);
    void updateLane(const std::string& laneId, AutomationLane newState);

    // Appearance
    void setOverviewHeight(float px);                  // 0 = no overview
    void setWaveformColor(Color c);
    void setBackgroundColor(std::optional<Color>);
    void setStereoMode(StereoMode);                     // SplitLR / Sum
    void setShowGrid(bool);                             // time grid lines

    // Callbacks
    void setOnViewRangeChanged(std::function<void(float, float)>);
    void setOnWarpMarkerMoved(std::function<void(int id, float newTargetSec)>);
    void setOnWarpMarkerAdded(std::function<void(float timeSec)>);
    void setOnWarpMarkerDeleted(std::function<void(int id)>);
    void setOnLoopRegionChanged(std::function<void(float, float)>);
    void setOnPlayheadClicked(std::function<void(float timeSec)>);

    // Automation callbacks
    void setOnBreakpointAdded(std::function<void(const std::string& laneId,
                                                   float timeSec, float value01)>);
    void setOnBreakpointMoved(std::function<void(const std::string& laneId, int id,
                                                   float timeSec, float value01)>);
    void setOnBreakpointDeleted(std::function<void(const std::string& laneId, int id)>);

    // Accessibility
    void setAriaLabel(const std::string&);
};

enum class StereoMode { SplitLR, Sum };
```

---

## Gestures

### Main waveform area

| Gesture | Result |
|---|---|
| **Click (no edit mode)** | Fires `onPlayheadClicked(time)`. Caller typically seeks playback. |
| **Click (warp edit mode)** | Adds warp marker at click time. |
| **Click (loop edit mode)** | Sets loop region anchor. |
| **Drag in overview bar** | Sets view range (drag-to-pan-view). |
| **Shift+click in overview bar** | Jumps playhead without moving view. |
| **Drag warp marker** | Moves marker. |
| **Drag loop edge** | Resizes loop region. |
| **Scroll wheel over main** | Zoom in/out around cursor position. |
| **Shift + scroll wheel** | Pan view left/right. |
| **Ctrl + scroll wheel** | Scroll the lane stack (if automation lanes exceed widget height). |

### Automation lanes

(When lane is editable.)

| Gesture | Result |
|---|---|
| **Click on curve (not on breakpoint)** | Add breakpoint at click. |
| **Drag breakpoint** | Move in time and value. |
| **Shift+drag breakpoint** | Constrain to one axis. |
| **Right-click breakpoint** | Context menu. |
| **Double-click breakpoint** | Reset value. |

### Keyboard (when focused)

| Key | Action |
|---|---|
| `+` / `-` | Zoom in / out at view center |
| `Left` / `Right` | Pan view by 10% of visible |
| `Home` / `End` | Jump to buffer start / end |
| `F` | Fit to view |
| `Space` | Play / stop (passes to caller) — not really WaveformWidget's job, but callers often wire it |

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size WaveformWidget::onMeasure(Constraints c, UIContext& ctx) {
    float w = c.hasBoundedWidth() ? c.maxW : 600.0f;

    // Total height = overview + main + each automation lane strip.
    float overviewH = m_showOverview ? m_overviewHeight : 0;
    float mainH = c.hasBoundedHeight() ? (c.maxH - overviewH - automationLanesH())
                                        : 200.0f;
    float totalH = overviewH + mainH + automationLanesH();

    return c.constrain({w, totalH});
}

float WaveformWidget::automationLanesH() const {
    float h = 0;
    for (auto& lane : m_lanes) {
        if (lane.layout == AutomationLayout::Below && lane.visible)
            h += m_laneHeight;
    }
    return h;
}
```

### `onLayout(Rect bounds, UIContext& ctx)`

Divides bounds into overview strip (top) + main waveform area +
below-lanes strips (stacked). Overlay lanes share main-area bounds.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes.** Audio data, markers, breakpoints, playhead changes are all
paint-only (don't affect widget size).

Adding / removing automation lanes DOES change height; triggers
measure invalidation if the lane has `layout = Below`.

### Caching

Measure cache: `(constraints, overview shown, below-lanes heights
sum)`. Content caches:

- **Waveform min/max pyramid**: built once per `setAudioBuffer`,
  persisted until cleared.
- **Transient / warp / loop rendering**: tiny amount of per-frame
  computation; no cache needed.
- **Automation curve paths**: cached per lane per view range —
  recomputed when lane data or view range changes.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Background (if no override) |
| `palette.accent` | Default waveform color |
| `palette.textPrimary` | Time grid labels |
| `palette.textDim` | Grid lines |
| `palette.playing` | Playhead color |
| `palette.queued` | Transient markers |
| `palette.border` | Lane dividers |
| `metrics.fontSizeSmall` | Time / value labels |
| `metrics.baseUnit` | Padding |

---

## Events fired

Listed in API above. All are fire-and-forget; caller updates its
data model, then calls widget setters to reflect back.

---

## Invalidation triggers

### Measure-invalidating

- `setOverviewHeight`
- Add / remove automation lanes with `layout = Below`
- Lane `setLaneHeight`
- DPI / theme / font

### Paint-only

- `setAudioBuffer` (pyramid rebuilt; then paint updates next frame)
- View range changes
- All marker setters
- `setPlayheadTime`
- `setWarpEditMode`, `setLoopEditMode`
- Breakpoint add / move / delete
- All color / visibility setters

### Continuous repaint

Set when playhead is being live-updated — common when caller
refreshes it every frame during playback. Clear when playback stops
or widget is hidden.

---

## Focus behavior

Tab-focusable for keyboard shortcuts (zoom, pan). Clicking focuses.

---

## Accessibility (reserved)

- Role: `application` or custom — this is a complex interactive
  graphic.
- Exposes aggregate info via aria (buffer duration, playhead
  position, number of markers).

---

## Animation

- **Smooth zoom**: ~100 ms when programmatic (fitToView, zoomTo).
  User-driven zoom (scroll wheel) is instant.
- **Smooth pan**: same pattern.
- **Playhead follow**: no animation; instant updates.
- **Marker drag**: 1:1 tracking.
- **Breakpoint value change on hover**: tooltip-style floating
  label with 80 ms fade.

---

## Test surface

Unit tests in `tests/test_fw2_WaveformWidget.cpp`. Complex widget;
many tests.

### Waveform rendering

1. `NoBufferPaintsEmpty` — no audio set: paints background +
   markers only.
2. `MonoRendersSingleEnvelope` — mono buffer paints single envelope
   line.
3. `StereoSplitsTopBottom` — stereo paints two envelopes.
4. `StereoSumRendersSingle` — setStereoMode(Sum) renders one combined.
5. `PyramidBuiltAfterSetBuffer` — setAudioBuffer triggers pyramid
   construction.
6. `ZoomShowsSampleDetail` — zooming to show < viewport samples
   paints sample-level detail (not envelope).

### Zoom / scroll

7. `SetViewRangeClamps` — setViewRange past buffer bounds clamps.
8. `ZoomInHalvesVisibleWindow` — zoomIn halves viewEnd-viewStart.
9. `ZoomOutDoubles` — zoomOut doubles.
10. `FitToViewShowsFullBuffer` — fitToView sets viewStart=0,
    viewEnd=duration.
11. `ScrollWheelZoomsAtCursor` — wheel zoom preserves time-at-
    cursor.
12. `ShiftWheelPans` — Shift+wheel pans without zoom.

### Markers

13. `TransientsRenderedAsTriangles` — at specified times.
14. `WarpMarkersDraggable` — drag fires onWarpMarkerMoved with
    correct new time.
15. `WarpClickAddsMarker` — click in warp edit mode fires
    onWarpMarkerAdded.
16. `WarpRightClickDeletes` — via context menu.
17. `LoopRegionDraggable` — drag edges fires onLoopRegionChanged.
18. `PlayheadRendered` — at specified time.
19. `PlayheadOutsideViewNotRendered` — playhead outside view range
    not drawn.

### Automation overlay

20. `SetLanesRenders` — lane curves drawn in their colors.
21. `OverlayLayoutDrawsOnMain` — Overlay lanes draw in main area.
22. `BelowLayoutAllocatesLane` — Below lanes add to widget height.
23. `ClickAddsBreakpoint` — click on curve fires
    onBreakpointAdded.
24. `DragBreakpointMoves` — drag fires onBreakpointMoved.
25. `ShiftDragConstrains` — shift constrains to vertical or
    horizontal based on initial direction.
26. `RightClickBreakpointMenu` — right-click fires context menu
    request.
27. `NonEditableLaneIgnoresEdit` — editable=false: clicks and
    drags don't fire callbacks.
28. `MultipleLanesStack` — multiple Below lanes stack in added
    order; heights add to widget measure.
29. `HiddenLaneNotPainted` — visible=false skips paint and
    skips measure contribution.

### Overview

30. `OverviewShowsFullBuffer` — overview bar shows entire buffer
    at minimal detail.
31. `OverviewHighlightsCurrentView` — zoomed range highlighted in
    overview.
32. `DragOverviewSetsView` — click+drag in overview moves view.
33. `NoOverviewWhenHeightZero` — setOverviewHeight(0) hides
    overview.

### Cache & performance

34. `SetAudioBufferPaintOnly` — new buffer is paint-only (pyramid
    is separate cache).
35. `PlayheadUpdatePaintOnly` — setPlayheadTime doesn't bump
    measure.
36. `LaneLayoutBelowInvalidatesMeasure` — adding a Below lane
    invalidates measure.
37. `RelayoutBoundary` — data changes don't propagate to parent.

### Keyboard

38. `PlusZoomsIn` — `+` key zooms in at view center.
39. `FFitsView` — `F` key calls fitToView.
40. `ArrowsPan` — Left/Right pan by 10%.

---

## Migration from v1

v1 `WaveformWidget` in
`src/ui/framework/WaveformWidget.h` has most of:
- Zoom/scroll
- Transient markers
- Warp markers
- Loop region

v2 adds:
- **Automation overlay** (new — your feature request).
- Multiple automation lanes stacked or overlaid.
- Consistent callback signatures (id-based edits).
- Relayout-boundary behavior (was previously doing more re-measure
  than needed on data changes).

Migration straightforward; automation overlay is additive.

---

## Open questions

1. **Vertical (flipped) orientation?** Automation lanes could be
   vertical if an audio clip is shown vertically (sidebar audio
   preview). Low priority; vertical variant = future widget.

2. **Programmatic automation preview?** Sometimes users want to
   hover a point on an automation curve and see its interpolated
   value without actually adding a breakpoint. Useful for inspection;
   default-off toggle.

3. **Multi-curve comparison?** Overlay multiple lanes' curves in
   the same overlay band for comparing parameters. Current design
   supports: multiple Overlay-layout lanes draw in the same area.
   Collision possible; callers manage via color + alpha.

4. **Snap to beats / bars?** Automation breakpoints should snap to
   the project's grid when enabled. Needs `setSnapResolutionBeats
   (beats)` and widget-level snap logic. Add when call sites need it.

5. **Copy / paste breakpoints?** Select multiple breakpoints, copy,
   paste at playhead. Useful for repetitive patterns. Future
   extension.

6. **Export / render automation curves?** For offline bouncing, the
   curves are already in the caller's data model. Widget isn't
   involved. N/A at widget level.

7. **Very-long buffers (30+ minutes)?** Pyramid memory grows
   linearly with sample count. For extreme cases (full-mix exports),
   might need lazy pyramid construction (build coarsest levels
   first, refine on zoom-in). Deferred; normal use cases fine.
