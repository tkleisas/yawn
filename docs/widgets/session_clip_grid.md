# SessionClipGrid — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the grid-rendering portion of `SessionPanel` in
`src/ui/panels/SessionPanel.cpp`. The panel-level concerns
(scene launching, track header rendering) stay in SessionPanel;
this spec isolates the **clip-grid widget** as a reusable composite.

---

## Intent

The **2D clip launching grid** — the central UI of session-view
performance. Tracks as columns, scenes as rows, each cell a clip
slot that's empty / contains a clip / is currently playing /
queued / recording. Click a slot to launch it; right-click for
context actions; drag audio files onto a slot to load them.

This widget is the soul of session view. Every cell encodes a lot
of state visually, and interactions must feel immediate even when
the engine quantizes the actual audio change. It also has to handle
visual clips (thumbnails) and MIDI clips alongside audio — three
distinct content types in the same grid.

SessionClipGrid composes the grid rendering, cell interactions,
auto-scroll behavior, and drag-and-drop. It does NOT own:
- Track headers (drawn above the grid by SessionPanel).
- Scene labels (drawn left of the grid by SessionPanel).
- Clip-engine logic (audio-thread state machine).
- Track muting / solo / arming UI (MixerPanel's job).

## Non-goals

- **Scene management UI.** Scene insert / duplicate / delete menus
  live on scene labels, not the grid cells themselves.
- **Track-level actions.** Track select / rename / type-change sit
  on track headers.
- **Arrangement placement.** Right-click slot → "Send to Arrangement"
  is a callback the widget emits; actual placement happens in the
  arrangement view.
- **Clip properties editing.** Inline — only launch / stop / record-
  arm are surfaced on the grid itself. Full editing (gain, loop
  region, warp markers) happens in the detail panel when the clip
  is selected.
- **Multi-selection across tracks.** Selection is single-cell for
  v2.0. Multi-select (drag a lasso to select many clips for batch
  operations) is deferred.

---

## Visual anatomy

```
                 Track 1        Track 2        Track 3        Track 4
             ┌──────────────┬──────────────┬──────────────┬──────────────┐
  Scene 0    │░ audioclip1 ░│              │▓ midi1 ▓▓▓▓▓▓│              │
             ├──────────────┼──────────────┼──────────────┼──────────────┤
  Scene 1    │░ audioclip2 ░│░ audioclip3 ░│              │🖼 visualclip░│
             ├──────────────┼──────────────┼──────────────┼──────────────┤
  Scene 2    │▌●▌audioclip4░│░ audioclip5 ░│▓ midi2 ▓▓▓▓▓▓│              │
             ├──────────────┼──────────────┼──────────────┼──────────────┤
  Scene 3    │ …            │              │              │              │
             └──────────────┴──────────────┴──────────────┴──────────────┘
                ↑                          ↑
                │                          currently recording (red)
                currently playing (green border)
```

Cell anatomy (single clip slot):

```
    ┌──────────────────────────────┐
    │ ▌●▌ Clip Name            ←─ │  ← play/stop indicator + clip label
    │                               │
    │ ░░░░ waveform thumbnail ░░░░ │  ← content-type-specific preview
    │                               │
    └──────────────────────────────┘
     │
     ↑ left-edge state stripe (empty / stopped / playing / queued / recording colors)
```

Parts, per cell:
1. **Background fill** — `clipSlotEmpty` if empty, `clipSlotBorder`
   default; state colors overlay.
2. **State stripe** — thin left-edge bar showing cell's state
   (playing=green, queued=yellow, stopped=gray, recording=red).
3. **Play/stop indicator** — small icon glyph: ▶ (stopped clip),
   ■ (playing), ● (recording), ⧖ (queued / quantize-waiting).
4. **Clip label** — truncated with ellipsis. Bold when playing.
5. **Content preview**:
   - Audio: waveform thumbnail (baked, low-res).
   - MIDI: mini piano-roll dots.
   - Visual: thumbnail image.
   - Empty: subtle cell outline only.
6. **Progress bar** — at bottom during quantize-wait (shows time
   until launch) or while importing.
7. **Hover / press tint** — overlay.
8. **Selection ring** — 2 px accent border if this is the selected
   slot.
9. **Focused-cell outline** — when this cell has keyboard focus.

---

## Data model

SessionClipGrid reads from an abstract data source:

```cpp
class SessionGridDataSource {
public:
    virtual ~SessionGridDataSource() = default;

    virtual int trackCount() const = 0;
    virtual int sceneCount() const = 0;

    enum class SlotKind {
        Empty,
        Audio,
        MIDI,
        Visual,
    };

    enum class SlotState {
        Stopped,
        Playing,
        Queued,       // will launch at next quantize boundary
        Recording,
        Importing,    // in-progress import with progress bar
    };

    struct SlotData {
        SlotKind kind = SlotKind::Empty;
        SlotState state = SlotState::Stopped;
        std::string label;            // "clip_01" or user name
        std::optional<GLuint> thumbnailTex;  // for visual clips
        const float* waveformSamples = nullptr;  // mini-waveform for audio
        int waveformSampleCount = 0;
        const std::vector<MidiNote>* midiNotes = nullptr; // mini-roll
        float importProgress = 0.0f;  // 0..1, only when state==Importing
        float quantizeProgress = 0.0f; // 0..1, only when state==Queued
        Color trackColor;              // inherited from track
        bool trackArmed = false;
    };

    virtual SlotData getSlot(int track, int scene) const = 0;
};
```

The widget calls `getSlot` each frame per visible cell. Callers
implementing this should cache aggressively — YAWN's `Project` model
already stores per-slot state and can return it cheaply.

---

## Virtualization

Session grids are bounded (typical: 8–16 tracks × 8 scenes = 128
cells). Unlike Table, we don't need lazy row building. However we
DO paint-cull: cells scrolled out of the viewport aren't painted.

For projects with many tracks (64-track projects), horizontal
virtualization skips painting cells in off-screen tracks. Vertical
virtualization similar for scenes.

No widget-per-cell; each cell is drawn directly by
SessionClipGrid's paint function from `SlotData`. No child widgets
except inline overlays (import progress bar, inline rename TextInput).

---

## Public API

```cpp
class SessionClipGrid : public Widget {
public:
    SessionClipGrid();

    // Data source
    void setDataSource(SessionGridDataSource* source);

    // Dimensions
    void setTrackWidth(float px);                     // default 130
    void setSceneHeight(float px);                    // default 48
    void setCellPadding(float px);                    // default 2

    // Selection / focus
    void setSelectedSlot(int track, int scene);
    std::pair<int, int> selectedSlot() const;          // {-1, -1} if none
    void clearSelection();

    // Scrolling (delegates to embedded ScrollView)
    void scrollToSlot(int track, int scene);
    void scrollToCurrentlyPlaying();                   // auto-scroll to first playing
    Point scrollOffset() const;
    void setScrollOffset(Point);

    // Follow mode
    void setAutoFollowPlayhead(bool);                  // default false
    // When true: auto-scroll to keep currently-playing clips visible.

    // Visual toggles
    void setShowTrackColors(bool);                     // default true
    void setShowStateStripe(bool);                     // default true
    void setShowThumbnails(bool);                      // default true — MIDI/audio/visual previews
    void setShowLabels(bool);                          // default true

    // Callbacks
    void setOnClipLaunched(std::function<void(int track, int scene)>);
    void setOnClipStopped(std::function<void(int track, int scene)>);
    void setOnSceneLaunched(std::function<void(int scene)>);
    // ^ scene launch is typically triggered from the scene label, but
    //   a callback hook on the grid lets it respond to keyboard Enter-
    //   on-scene-row shortcut.

    void setOnSlotSelected(std::function<void(int track, int scene)>);
    void setOnSlotRightClick(std::function<void(int track, int scene, Point screen)>);
    void setOnSlotDoubleClick(std::function<void(int track, int scene)>);
    void setOnSlotRenamed(std::function<void(int track, int scene, const std::string& newName)>);

    // Drag-and-drop — audio files dropped onto a slot
    void setOnFileDropped(std::function<void(int track, int scene,
                                                const std::vector<std::string>& paths)>);

    // Clip-to-clip drag (move / copy within grid)
    void setOnSlotMoved(std::function<void(int fromTrack, int fromScene,
                                              int toTrack, int toScene,
                                              bool copy)>);

    // Accessibility
    void setAriaLabel(const std::string&);
};
```

---

## Gestures

### Pointer

| Gesture | Result |
|---|---|
| **Click on filled slot** | Launch or stop (toggle) — fires `onClipLaunched` or `onClipStopped`. Selection updates. |
| **Click on empty slot** | Select slot (fires `onSlotSelected`), does nothing else (no clip to launch). |
| **Double-click on empty slot** | MIDI tracks only: fires `onSlotDoubleClick` — caller typically creates an empty MIDI clip. |
| **Double-click on filled slot** | Fires `onSlotDoubleClick` — caller typically opens detail panel / piano roll. |
| **Right-click on any slot** | Fires `onSlotRightClick` with screen pos. Selection updates. |
| **Shift+click on filled slot** | Preview-launch (play until pointer released). Used in live-performance workflow. |
| **Ctrl+click on filled slot** | Stop ALL clips on this track (emergency stop gesture). |
| **Click and drag a clip** | Move clip to new slot. Ctrl+drag = copy instead. Fires `onSlotMoved`. |
| **Drop external audio file** | Fires `onFileDropped` with dropped paths. |

### Keyboard (when grid focused)

| Key | Action |
|---|---|
| `Arrow keys` | Move focused slot ±1 in each axis. |
| `Enter` / `Space` | Launch / stop the focused slot's clip. |
| `Delete` / `Backspace` | Clear focused slot (after confirmation via parent, not widget's job). |
| `F2` | Start inline rename of focused slot's clip. |
| `Tab` / `Shift+Tab` | Exit grid / move to next focusable. |
| `Home` / `End` | Focus first / last cell of current row. |
| `Ctrl+Home` | Focus (0, 0). |
| `Ctrl+End` | Focus last cell of last row. |
| Letter key (A–Z) | Type-ahead: jump to first clip whose label starts with the typed letter (case-insensitive). |

### Inline rename

Triggered by F2 or "Rename" context menu item. Places an
`FwTextInput` overlay over the cell; Enter commits, Escape cancels.

---

## Visual states

Each slot has a primary state from its `SlotState`. Additional
modifiers layer on top.

| Visual element | Empty | Stopped | Playing | Queued | Recording | Importing |
|---|---|---|---|---|---|---|
| Background | `clipSlotEmpty` | track-color * 0.3 | track-color * 0.5 | track-color * 0.4 | error tint | inset darkened |
| State stripe | none | dim gray | green solid | yellow solid | red solid | blue pulse |
| Icon | none | ▶ | ■ | ⧖ | ● | none (progress bar instead) |
| Progress bar | none | none | none | quantize progress (bottom strip) | none | import progress |
| Label font | n/a | normal | bold | normal | bold | dim |
| Cell animation | none | none | subtle fade-in of green on launch | pulsing yellow stripe | slow pulsing red stripe | indeterminate shimmer |

Selected slot adds a 2-px accent border. Focused cell (keyboard)
adds an inner 1-px focus ring. Hovered cell adds subtle brighten.

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

```cpp
Size SessionClipGrid::onMeasure(Constraints c, UIContext& ctx) {
    int T = m_source ? m_source->trackCount() : 0;
    int S = m_source ? m_source->sceneCount() : 0;
    float contentW = T * m_trackWidth + (T - 1) * m_cellPadding;
    float contentH = S * m_sceneHeight + (S - 1) * m_cellPadding;
    // SessionClipGrid uses an internal ScrollView for overflow.
    // Its outer measure is driven by parent constraints.
    float w = c.hasBoundedWidth()  ? c.maxW : std::min(contentW, 800.0f);
    float h = c.hasBoundedHeight() ? c.maxH : std::min(contentH, 400.0f);
    return c.constrain({w, h});
}
```

### `onLayout(Rect b, UIContext& ctx)`

- Lay out internal ScrollView over bounds.
- ScrollView's content size = (contentW, contentH) per measure math.
- Scroll offset clamped; cell rects computed from
  `(track * (trackWidth + padding), scene * (sceneHeight + padding))`.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

### Relayout boundary

**Yes** — track count / scene count changes invalidate measure but
don't affect parent's size (SessionPanel explicitly wraps in a
bounded region).

### Caching

Measure cache on `(constraints, T, S, trackWidth, sceneHeight,
padding)`. State changes, slot content changes, scroll offset changes
are all paint-only — critical because state updates can happen at
audio-engine rate (30 Hz or faster).

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.clipSlotEmpty` | Empty cell background |
| `palette.clipSlotHover` | Hovered cell overlay |
| `palette.clipSlotBorder` | Cell border |
| `palette.playing` | Playing state stripe, icon |
| `palette.queued` | Queued stripe, icon |
| `palette.stopped` | Stopped icon |
| `palette.recording` | Recording stripe, icon |
| `palette.accent` | Selection border, focus ring |
| `palette.textPrimary` | Label when playing |
| `palette.textSecondary` | Label when stopped, small annotations |
| `palette.textDim` | Empty-slot labels, importing |
| `metrics.cornerRadius` | Cell corners |
| `metrics.baseUnit` | Cell padding |
| `metrics.fontSize` | Label |
| `metrics.fontSizeSmall` | Indicator |

Plus `trackColors` for cell tinting by track.

---

## Events fired

See Public API.

Firing rules:
- `onClipLaunched` / `onClipStopped` fire immediately on user
  click, even if actual audio change is quantized. The widget's
  visual state updates immediately to show "queued"; real state
  from `SlotData` corrects once the engine confirms.
- `onSlotSelected` fires for any selection change, including from
  keyboard nav.
- `onSlotRightClick` fires before any follow-up action (caller
  typically shows ContextMenu).
- `onFileDropped` fires once per drop, with all dropped file paths
  in one call.

---

## Invalidation triggers

### Measure-invalidating

- Track or scene count changes (from data source)
- `setTrackWidth`, `setSceneHeight`, `setCellPadding`
- DPI / theme / font

### Paint-only

- Selection / focus changes
- Slot state changes (read from data source each frame)
- Scroll offset
- Hover / press transitions
- `setShowTrackColors`, `setShowThumbnails`, etc.

### Continuous repaint

SessionClipGrid sets `requiresContinuousRepaint()` when:
- Any slot is in `Queued` state (progress bar updates).
- Any slot is `Importing` (progress).
- Any animation is active (launch flash).

Otherwise static — saves CPU when session is paused.

---

## Focus behavior

- Grid is tab-focusable; first focused slot is (0, 0) or last
  selected.
- Arrow-key navigation moves focus without scrolling unless focus
  leaves visible area (then auto-scroll-into-view kicks in).
- Tab leaves grid entirely.

---

## Accessibility (reserved)

- Role: `grid` (ARIA).
- Each cell: `gridcell` with `aria-selected`, `aria-label` =
  track name + scene label + state.

---

## Animation

- **Launch flash**: 300 ms opacity pulse when a cell transitions
  to `Playing`.
- **Quantize-queued stripe**: pulsing yellow stripe, ~1 Hz.
- **Record-armed stripe**: slow pulsing red, ~0.5 Hz.
- **Import shimmer**: indeterminate gradient sweep on state stripe.
- **Selection border**: instant (no fade — user expects immediate
  feedback).
- **Hover fade**: 80 ms.
- **Smooth scroll on scrollToSlot**: 150 ms when programmatic.

---

## Test surface

Unit tests in `tests/test_fw2_SessionClipGrid.cpp`. Mock data
source for deterministic tests.

### Layout

1. `MeasureFromDataSource` — 8 tracks × 4 scenes →
   content = 8 × trackWidth + 3 × scenePadding.
2. `PaintCullSkipsOffScreen` — cells beyond scroll viewport are
   not painted.
3. `ScrollToSlotMakesVisible` — scrollToSlot(7, 3) adjusts offset
   to make that cell visible.

### Cell states

4. `EmptyCellPaintedEmpty` — empty slot paints empty background,
   no label or icon.
5. `StoppedCellShowsLabel` — stopped slot paints label + stopped
   icon.
6. `PlayingCellGreenStripe` — playing state paints green stripe.
7. `QueuedPulsingStripe` — queued state sets continuous repaint
   flag + paints pulsing yellow stripe.
8. `RecordingRedStripe` — recording state paints red stripe.
9. `ImportingProgressBar` — importing state renders progress bar
   based on importProgress (0..1).

### Interaction

10. `ClickLaunchesFilledClip` — click on stopped filled slot fires
    onClipLaunched.
11. `ClickOnPlayingStops` — click on playing slot fires
    onClipStopped.
12. `ClickOnEmptySelectsOnly` — click on empty slot fires
    onSlotSelected, nothing else.
13. `DoubleClickFires` — double-click fires onSlotDoubleClick.
14. `RightClickFires` — right-click fires onSlotRightClick with
    screen position.
15. `ShiftClickPreviewLaunch` — shift+click only plays while held.
16. `CtrlClickStopsTrack` — ctrl+click fires onClipStopped for
    every clip on that track.

### Keyboard

17. `ArrowKeysMoveFocus` — arrow keys move focused cell with
    clamping at bounds.
18. `EnterLaunchesFocused` — Enter fires launch on focused cell.
19. `TypeAheadJumps` — typing 'C' jumps focus to first clip
    starting with 'C'.
20. `F2StartsRename` — F2 opens inline rename.

### Selection

21. `SetSelectedSlotMovesFocus` — setSelectedSlot(3, 2) changes
    focus and selection.
22. `SelectionSurvivesScroll` — selected slot remains selected
    when scrolled out and back.

### Drag and drop

23. `DropFileFiresCallback` — simulated file drop on cell fires
    onFileDropped with that track/scene.
24. `DragClipToEmptySlotMoves` — drag a clip onto an empty slot
    fires onSlotMoved(from, to, copy=false).
25. `CtrlDragClipCopies` — with ctrl held, fires with copy=true.

### Auto-follow

26. `AutoFollowScrollsToPlaying` — with setAutoFollowPlayhead(true),
    a new playing clip scrolls into view.
27. `AutoFollowOffNoScroll` — flag off: playing clip doesn't
    cause auto-scroll.

### Cache

28. `StateChangePaintOnly` — slot state change doesn't bump
    measure version.
29. `RelayoutBoundary` — track/scene changes don't propagate.

---

## Migration from v1

v1 `SessionPanel` mixes panel-level concerns (scene management,
track headers) with grid-cell rendering. v2 extracts the grid-cell
portion into SessionClipGrid; SessionPanel retains the outer chrome.

Migration:
1. Extract the cell-rendering code from v1 SessionPanel's paint
   loop into SessionClipGrid's paint.
2. Implement `SessionGridDataSource` adapter that bridges v1
   Project model to the new interface.
3. SessionPanel becomes a container holding track headers + scene
   labels + SessionClipGrid.

Estimated LOC reduction: ~200–400 lines (lots of overlapping
rendering code in v1's SessionPanel).

---

## Open questions

1. **Multi-cell selection?** Lasso-drag to select many clips for
   batch delete / copy / move. Deferred.

2. **Per-cell context overlay?** Some DAWs show "launch quantize"
   visually on each cell. Currently shown only on the quantize-wait
   state. Possibly add a subtle indicator for clips with non-global
   quantize settings.

3. **Cell icons for track type?** Drum rack tracks show a different
   mini-icon than synth tracks. Hook via SlotData — caller provides
   optional kind-specific icon. Trivial.

4. **Hot-swap track colors?** Changing a track color should repaint
   all its cells. Data source already carries `trackColor` per slot;
   changing it is automatic.

5. **Grouped tracks?** Track groups (collapsible aggregations) are
   a future feature. Not in v2.0 SessionClipGrid — would be handled
   by changing trackCount/getSlot to reflect group-folded state.

6. **Clip launch with fade-in preview?** "Preview clip before
   launching" — hover + hold modifier = preview. Niche; deferred.
