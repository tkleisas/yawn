# ArrangementGrid — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the timeline-rendering portion of `ArrangementPanel`
in `src/ui/panels/ArrangementPanel.cpp`.
**Related:** [session_clip_grid.md](session_clip_grid.md) — sister
composite. SessionClipGrid is the cell-matrix view; ArrangementGrid
is the horizontal-timeline view.
[waveform_widget.md](waveform_widget.md) — used inside audio clip
blocks on the timeline.

---

## Intent

The **horizontal-timeline clip editor**: audio, MIDI, and visual
clips laid out on a beats/bars grid, per-track lanes stacked
vertically, with a playhead, loop range markers, automation lanes,
and full interactive editing (drag to move, resize edges, create by
double-click). The arrangement view is how YAWN writes a song rather
than jams one.

ArrangementGrid combines several concerns into one widget because
they share state and rendering passes:

- **Ruler** — time grid at top (bars / beats / seconds).
- **Track lanes** — horizontal strips, one per track.
- **Clips** — blocks within a lane, spanning from `startBeat` to
  `startBeat + lengthBeats`.
- **Automation lanes** — expandable strips below each track showing
  breakpoint envelopes (via WaveformWidget's automation overlay,
  or inline).
- **Playhead** — vertical line at `transport.position`.
- **Loop region** — green range in ruler, with dragable edges.

Panel-level concerns (track headers, send/track selection) live in
the ArrangementPanel wrapping ArrangementGrid.

## Non-goals

- **Piano roll editing.** MIDI clip internals are edited in the
  PianoRoll widget, opened via double-click on a MIDI clip.
- **Waveform editing.** Same — double-click an audio clip opens
  WaveformWidget in the detail panel.
- **Master tempo / time signature editing.** Handled via transport
  and preferences, not inline on the timeline.
- **Mixer automation (volume / pan on the track faders).** Lives on
  the mixer panel; arrangement automation is about parameters
  within clips / devices on the track.
- **Region markers with labels (section markers like "Verse",
  "Chorus").** Future enhancement; not in v2.0.

---

## Visual anatomy

```
       bar 1   bar 2   bar 3   bar 4   bar 5   bar 6   bar 7
     ┌───────┬───────┬───────┬───────┬───────┬───────┬───────┐
Ruler│░░░░░░░│░░░░░░░│░░░░░░░│░░░░░░░│░░░░░░░│░░░░░░░│░░░░░░░│  ← bar/beat markers
     │       [═══════Loop═══════]                              │  ← loop region
     ├───────┴───────┴───────┴───────┴───────┴───────┴───────┤
Track1│  ┌──────audioclip──────┐      ┌───audioclip───┐         │
     │  │ ░░░░░waveform░░░░░░  │      │░░░waveform░░░ │         │  ← track 1 lane
     │  └─────────────────────┘      └────────────────┘         │
     ├───────────────────────────────────────────────────────┤
Track2│           ┌───midi────┐         ┌───midi────┐           │
     │           │▓ mini-roll │         │▓ mini-roll│           │  ← track 2 lane (MIDI)
     │           └────────────┘         └────────────┘           │
     ├───────────────────────────────────────────────────────┤
     │  ○──╱╲___╱─────○──                                        │  ← automation lane (per-param)
     ├───────────────────────────────────────────────────────┤
Track3│                      ┌──visualclip───┐                  │
     │                      │🖼 thumbnail    │                   │  ← track 3 lane (visual)
     │                      └────────────────┘                  │
     └───────────────────────────────────────────────────────┘
            ↑ playhead (vertical line across all lanes)
```

Parts:
1. **Ruler** — top strip showing time markers (bars, beats,
   optionally seconds). `palette.surface` background, text labels at
   marker positions.
2. **Track lanes** — one per track, stacked vertically. Each lane's
   background color is a dimmed version of the track color.
3. **Clips** — rounded rectangles on a lane. Visually distinct per
   type:
   - Audio: rendered waveform inside.
   - MIDI: mini piano-roll (dots/lines per note).
   - Visual: thumbnail image.
4. **Automation lanes** — expandable strips below each track.
   Shows breakpoint curves for one or more automated parameters.
5. **Playhead** — vertical line at transport position, top to
   bottom across all lanes. `palette.playing` color.
6. **Loop region** — highlighted range in the ruler + faint overlay
   across all lanes. Draggable edge handles for resize.
7. **Selection indicator** — selected clip(s) get accent-colored
   outline.

---

## Coordinate system

Horizontal axis is **beats** (not samples or seconds). Transport
time in beats → pixel via `pxPerBeat` zoom factor. Ruler shows
bars (= 4 beats in 4/4 time).

```cpp
float beatToPx(float beat) const {
    return (beat - m_viewStartBeat) * m_pxPerBeat;
}

float pxToBeat(float px) const {
    return m_viewStartBeat + px / m_pxPerBeat;
}
```

Zoom range: 4 px/beat (very zoomed out, full song at a glance) to
120 px/beat (zoomed in for per-beat editing).

Vertical axis is track lanes. Each track has:
- `trackHeight` — the lane's row height.
- Optional `automationLanes` expanded below, each with its own height.

---

## Data model

Like SessionClipGrid, ArrangementGrid reads from a data source:

```cpp
class ArrangementDataSource {
public:
    virtual ~ArrangementDataSource() = default;

    virtual int trackCount() const = 0;
    virtual int clipCount(int track) const = 0;

    enum class ClipKind { Audio, MIDI, Visual };

    struct ClipData {
        int id;                     // stable
        ClipKind kind;
        float startBeat;
        float lengthBeats;
        std::string label;
        Color color;                // inherited from track or custom

        // Per-kind preview data
        std::optional<GLuint> thumbnailTex;    // visual
        const float* waveformSamples = nullptr;  // audio
        int waveformSampleCount = 0;
        const std::vector<MidiNote>* midiNotes = nullptr;

        // Fade in/out (beats) — applied to audio clips visually
        float fadeInBeats = 0.0f;
        float fadeOutBeats = 0.0f;
    };

    virtual ClipData getClip(int track, int clipIdx) const = 0;

    // Automation lanes
    virtual int automationLaneCount(int track) const = 0;
    virtual AutomationLane getAutomationLane(int track, int laneIdx) const = 0;
    // AutomationLane shares the struct from WaveformWidget.

    // Transport
    virtual float playheadBeat() const = 0;
    virtual bool  isPlaying() const = 0;
    virtual std::optional<std::pair<float, float>> loopRange() const = 0;

    // Time signature / tempo — for ruler labeling
    virtual int beatsPerBar() const = 0;
    virtual float tempo() const = 0;
};
```

Caller maintains arrangement state; widget reads + renders.

---

## Public API

```cpp
class ArrangementGrid : public Widget {
public:
    ArrangementGrid();

    // Data source
    void setDataSource(ArrangementDataSource* source);

    // Zoom / scroll
    void setPxPerBeat(float px);                     // default 24
    float pxPerBeat() const;
    void setViewStartBeat(float beat);
    float viewStartBeat() const;
    void zoomIn(float anchorBeat);
    void zoomOut(float anchorBeat);
    void fitToContent();                              // zoom/scroll to show all clips

    // Track heights
    void setDefaultTrackHeight(float px);             // default 60
    void setTrackHeight(int track, float px);         // per-track override
    float trackHeight(int track) const;

    // Automation expansion
    void setAutomationLanesExpanded(int track, bool expanded);
    bool isAutomationLanesExpanded(int track) const;

    // Grid / snap
    enum class SnapResolution {
        Off, Bar, Beat, Half, Quarter, Eighth, Sixteenth,
    };
    void setSnapResolution(SnapResolution);
    SnapResolution snapResolution() const;

    // Selection
    void setSelectedClip(int track, int clipId);
    std::optional<std::pair<int, int>> selectedClip() const;
    void clearSelection();

    // Follow mode
    void setAutoScrollFollowPlayhead(bool);           // default false (F key toggles)

    // Callbacks (clip editing)
    void setOnClipMoved(std::function<void(int trackFrom, int id,
                                              int trackTo, float newStartBeat)>);
    void setOnClipResized(std::function<void(int track, int id,
                                                float newStartBeat, float newLengthBeats)>);
    void setOnClipCreated(std::function<void(int track, float startBeat, float lengthBeats)>);
    void setOnClipDeleted(std::function<void(int track, int id)>);
    void setOnClipDuplicated(std::function<void(int track, int id, float targetBeat)>);
    void setOnClipRenamed(std::function<void(int track, int id, const std::string& name)>);
    void setOnClipRightClick(std::function<void(int track, int id, Point screen)>);

    // Callbacks (automation editing)
    void setOnAutomationBreakpointAdded(std::function<void(int track,
                                                              const std::string& laneId,
                                                              float beat, float value01)>);
    void setOnAutomationBreakpointMoved(std::function<void(int track,
                                                              const std::string& laneId,
                                                              int id, float beat, float value01)>);
    void setOnAutomationBreakpointDeleted(std::function<void(int track,
                                                                const std::string& laneId,
                                                                int id)>);

    // Callbacks (transport / loop)
    void setOnPlayheadSeek(std::function<void(float beat)>);
    void setOnLoopRangeChanged(std::function<void(float startBeat, float endBeat)>);
    void setOnLoopEnabledChanged(std::function<void(bool)>);

    // Drag-and-drop (external files)
    void setOnFileDropped(std::function<void(int track, float beat,
                                                const std::vector<std::string>& paths)>);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

---

## Gestures

### Ruler

| Gesture | Result |
|---|---|
| **Click** | Seeks playhead. Fires `onPlayheadSeek(beat)`. |
| **Shift+click** | Sets loop start to clicked beat. Fires `onLoopRangeChanged`. |
| **Shift+right-click** | Sets loop end. |
| **Drag loop edge** | Resizes loop range. |
| **Drag loop middle** | Moves loop range. |
| **Double-click loop range** | Toggle loop enabled. |

### Track lanes — empty area

| Gesture | Result |
|---|---|
| **Click** | Selects nothing (clears selection). Also seeks playhead to click beat. |
| **Double-click** | Creates a new clip: MIDI for MIDI tracks, empty audio clip for audio tracks. Fires `onClipCreated`. |
| **Right-click** | Fires `onTrackRightClick` with screen pos. Callers usually show "Paste here" if clipboard has a clip. |

### Track lanes — on a clip

| Gesture | Result |
|---|---|
| **Click** | Selects the clip. Fires `onSlotSelected`. |
| **Double-click** | Opens clip in detail editor (caller decides which editor). |
| **Drag body** | Moves clip in time (and vertically to change track, if allowed). Snap applies. Fires `onClipMoved` on release. |
| **Drag left / right edge** | Resizes. Snap applies. Fires `onClipResized`. |
| **Ctrl+drag** | Duplicate rather than move. Fires `onClipDuplicated`. |
| **Right-click** | Fires `onClipRightClick`. |
| **Delete / Backspace** key (clip selected) | Fires `onClipDeleted`. |
| **Ctrl+D** (clip selected) | Fires `onClipDuplicated` with clip placed immediately after current. |

### Scrolling / zoom

| Gesture | Result |
|---|---|
| **Scroll wheel (vertical)** | Scrolls tracks vertically. |
| **Shift+wheel** | Scrolls horizontally (time axis). |
| **Ctrl+wheel** | Zooms horizontally at cursor time. |
| **Middle-button drag** | Pans both axes. |

### Keyboard

| Key | Action |
|---|---|
| `Space` | Play / pause (passes to caller via transport). |
| `Arrow Left / Right` | Move selected clip by snap increment. Fires `onClipMoved`. |
| `Arrow Up / Down` | Move selected clip to adjacent track (if allowed). |
| `Delete` | Delete selected. |
| `Ctrl+D` | Duplicate selected (after current position). |
| `Ctrl+L` | Toggle loop enabled. |
| `L` | Toggle loop enabled (no Ctrl — single-letter shortcut). |
| `F` | Toggle auto-scroll follow playhead. |
| `Home` | Seek playhead to beat 0. |
| `End` | Seek to last clip end. |
| `+` / `-` | Zoom in / out at view center. |

### Drag-and-drop

- Audio file from OS file manager → fires `onFileDropped` at the
  drop beat / track.

---

## Snap behavior

Snap resolution chosen globally. When snap is on, clip drag +
resize edges snap to the grid of the chosen subdivision.

| Resolution | Grid |
|---|---|
| Off | No snap; beat-accurate |
| Bar | `beatsPerBar` snap |
| Beat | 1 beat |
| Half | 0.5 beat |
| Quarter | 0.25 |
| Eighth | 0.125 |
| Sixteenth | 0.0625 |

Shift during drag temporarily disables snap (free-drag).

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Takes parent's constraints; internal scroll handles overflow:

```cpp
Size ArrangementGrid::onMeasure(Constraints c, UIContext& ctx) {
    float w = c.hasBoundedWidth()  ? c.maxW : 600.0f;
    float h = c.hasBoundedHeight() ? c.maxH : 400.0f;
    return c.constrain({w, h});
}
```

### `onLayout(Rect b, UIContext& ctx)`

1. Reserve ruler height at top (`rulerHeight`, default 28 px).
2. Compute total content width from maxClipEnd (across all tracks)
   and current zoom.
3. Compute total content height from trackHeight × trackCount +
   expanded automation lanes.
4. Position internal ScrollView over bounds minus ruler.
5. Update scroll offsets; sync viewStartBeat / viewEndBeat.
6. Lay out playhead overlay across all lanes at `playheadBeat × pxPerBeat`.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes** — content layout changes don't affect widget bounds.

### Caching

Measure cached on `(constraints)`. Layout cached on `(bounds, track
heights, automation expansion state, pxPerBeat, viewStartBeat, data
version)`.

Playhead movement is paint-only (continuous-repaint).
Selection changes are paint-only.

---

## Paint order

1. Background tracks/lanes fill.
2. Grid lines (bars / beats based on zoom).
3. Clips in each lane, rendered by type:
   - Audio: clip background + waveform (using WaveformWidget's
     rendering but inline; or waveform bitmap if pre-baked).
   - MIDI: clip background + mini-piano-roll dots.
   - Visual: clip background + thumbnail image.
4. Clip name labels (overlaid).
5. Selection borders on selected clips.
6. Automation lane curves (if expanded).
7. Loop region overlay (faint vertical highlight across all lanes).
8. Playhead vertical line.
9. Ruler over everything at top.

Ruler and playhead draw AFTER scroll-content so they stay visible.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.surface` | Ruler background |
| `palette.panelBg` | Lane backgrounds |
| `palette.border` | Lane dividers, grid lines |
| `palette.textDim` | Grid lines, inactive clip labels |
| `palette.textPrimary` | Ruler labels, active clip names |
| `palette.playing` | Playhead line, loop-enabled overlay |
| `palette.accent` | Selected clip border |
| `palette.queued` | Loop region highlight |
| `palette.modulation` | Automation curves |
| `metrics.fontSize` / `fontSizeSmall` | Clip labels / ruler labels |
| `metrics.baseUnit` | Lane padding |

Each track/clip also uses its own inherited color from `trackColors`.

---

## Events fired

See API above — exhaustive editing callbacks.

All edits fire callbacks; widget does NOT mutate data directly.
Caller updates its model in response, then next frame's data source
reads reflect the change.

---

## Invalidation triggers

### Measure-invalidating

- Nothing external — widget takes constraint as given.

### Layout-invalidating

- Track count change (data source)
- `setDefaultTrackHeight`, `setTrackHeight`
- Automation-lane expansion change
- DPI / theme / font

### Paint-only

- Playhead updates
- Selection changes
- All data-source content reads
- Loop range / enable changes
- Snap / zoom / scroll state
- Hover / press transitions

### Continuous repaint

Set while transport is playing (playhead moves). Clear when
transport pauses and no animation is active.

---

## Focus behavior

- Tab-focusable.
- Arrow keys move selected clip (if any) or navigate playhead
  (if none).
- Ctrl+Home seeks to beat 0.

---

## Accessibility (reserved)

- Role: custom — arrangement view is a complex editor, standard
  roles don't fit.
- Exposes aggregate info: total track count, clip count, selected
  clip metadata.

---

## Animation

- **Playhead**: follows transport position; renders every frame
  during play.
- **Auto-scroll follow**: 150 ms smooth-scroll when playhead
  crosses viewport edge with follow mode on.
- **Selection border**: instant.
- **Hover fade on clip**: 80 ms.
- **Clip move/resize**: 1:1 tracking during drag.
- **Loop region pulse**: subtle breathing highlight when loop is
  enabled, ~1 Hz.

---

## Test surface

Unit tests in `tests/test_fw2_ArrangementGrid.cpp`. Significant
mocking for data source.

### Ruler / coordinate

1. `BeatToPx` — viewStart=0, pxPerBeat=24: beat 1 → px 24.
2. `PxToBeat` — inverse.
3. `RulerLabelsAtBars` — bar markers with labels ("1", "2", ...).

### Clips

4. `AudioClipRendered` — audio clip drawn with waveform preview.
5. `MidiClipRendered` — MIDI clip drawn with mini-roll.
6. `VisualClipRendered` — visual clip drawn with thumbnail.
7. `ClipClampedToView` — clip outside view not painted.

### Interaction — clip

8. `ClickClipSelects` — click fires onSlotSelected, selection
   updates.
9. `DragClipMoves` — drag fires onClipMoved with snapped beat.
10. `DragResizeRightEdge` — drag right edge fires onClipResized.
11. `DragResizeLeftEdge` — drag left edge fires onClipResized.
12. `CtrlDragDuplicates` — ctrl+drag fires onClipDuplicated.
13. `DeleteSelectedFiresDelete` — Delete key fires onClipDeleted.
14. `CtrlDFiresDuplicate`.
15. `DoubleClickEmptyCreates` — double-click empty lane area
    fires onClipCreated.
16. `SnapAlignsToResolution` — snap=Bar: drag to 3.7 beats snaps
    to nearest bar (4 beats in 4/4).
17. `ShiftDuringDragDisablesSnap` — shift during drag ignores
    snap.

### Interaction — ruler

18. `ClickRulerSeeks` — fires onPlayheadSeek.
19. `ShiftClickSetsLoopStart` — fires onLoopRangeChanged.
20. `ShiftRightClickSetsLoopEnd`.
21. `DragLoopEdgeResizes`.
22. `LKeyTogglesLoop` — fires onLoopEnabledChanged.

### Automation

23. `ExpandedLaneShowsCurve` — setAutomationLanesExpanded(true)
    shows curve + increases track lane height.
24. `AddBreakpointFires` — click on curve fires
    onAutomationBreakpointAdded.
25. `DragBreakpointFires` — onAutomationBreakpointMoved.
26. `DeleteBreakpointViaContextMenu` — right-click + delete
    fires onAutomationBreakpointDeleted.

### Zoom / scroll

27. `CtrlWheelZoomsAtCursor` — zoom preserves time at cursor.
28. `FitToContentShowsAll` — fitToContent zooms to first and
    last clip.
29. `AutoScrollFollowsPlayhead` — follow mode on: playhead
    crossing edge auto-scrolls.
30. `ScrollWheelScrollsVertical` — vertical scroll via wheel.

### Playhead

31. `PlayheadDrawnAcrossLanes` — playhead line spans all lanes.
32. `PlayheadOutsideViewNotDrawn` — off-screen playhead not
    rendered.
33. `ContinuousRepaintWhenPlaying` — setRequiresContinuousRepaint
    while isPlaying() is true.

### Cache

34. `PlayheadUpdatePaintOnly` — no measure bump.
35. `SelectionChangePaintOnly`.
36. `AutomationExpansionInvalidatesLayout` — but not measure.
37. `RelayoutBoundary` — content changes stop at widget.

---

## Migration from v1

v1 `ArrangementPanel` mixes timeline rendering, track header UI,
and scroll management in one huge file. v2 splits:
- ArrangementGrid: timeline rendering + clip/automation editing.
- ArrangementPanel: wraps grid, adds track headers, send/session
  toggle, scroll wiring.

Migration is the biggest composite-to-widget conversion in the v2
effort. Strategy:
1. Extract paint code from v1 ArrangementPanel into
   ArrangementGrid's paint function.
2. Implement ArrangementDataSource adapter over the v1 Project +
   AutomationEngine models.
3. Port editing gesture handlers into v2 gesture callbacks.
4. Replace ArrangementPanel's scroll / zoom / loop handling with
   v2 equivalents.

Estimated LOC reduction: ~500–800 (ArrangementPanel's current
implementation is large and much of it becomes widget-level).

---

## Open questions

1. **Vertical track reordering?** Drag a track lane vertically to
   change order. Currently handled at the track header level;
   arrangement grid just reflects the order. Fine.

2. **Multi-clip selection?** v2.0 single-clip only. Lasso-drag in
   empty lane area to select multiple clips would enable batch
   move / copy / delete. Deferred, but spec'd as an open path.

3. **Region-copy / region-paste?** Select a range of timeline +
   ctrl+c to copy all clips in that range. Future.

4. **Region markers / section labels?** "Verse" / "Chorus"
   markers on the ruler. Future.

5. **Track folders / groups?** Collapsible track groups. Future.

6. **Clip fades as editable ramps?** Currently fadeIn/fadeOut
   beats are rendered as a triangle on the audio clip edge. Interactive
   editing (drag the fade edge) is valuable — add as `onClipFadeIn/
   Out` callbacks + drag gesture on fade triangle. Likely v2.1.

7. **Time-signature changes mid-song?** Rendering adapts if the
   data source's `beatsPerBar()` returns per-bar (not global). v2.0
   assumes global time signature; revisit when composers need
   section-level changes.

8. **Automation-lane merge / split?** Multiple parameters on one
   lane vs. separate lanes per parameter. Currently data source
   exposes lanes explicitly; caller decides how to group. Fine.

9. **Real-time visual-clip preview during drag?** When dragging a
   visual clip, show its thumbnail at the new position in real-time.
   WaveformWidget does this for audio; visual clips should too.
   Easy add.
