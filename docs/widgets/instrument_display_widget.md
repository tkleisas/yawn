# InstrumentDisplayWidget — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** `InstrumentDisplayWidget` and its sub-displays in
`src/ui/framework/InstrumentDisplayWidget.h`.

---

## Intent

A **family of small custom-paint widgets** that visualize instrument
parameters graphically rather than textually. Sit inside instrument
panels alongside parameter knobs to give at-a-glance feedback about
the instrument's current configuration.

The family:

- **ADSRDisplay** — attack/decay/sustain/release envelope curve.
- **FMAlgorithmDisplay** — operator routing diagram (FM synth).
- **OscillatorWaveformDisplay** — waveform preview (sine, saw,
  square, etc., morphed by shape params).
- **FilterResponseDisplay** — filter magnitude response curve vs
  frequency.
- **LFOWaveformDisplay** — LFO waveform preview with phase indicator.
- **GroupedKnobBody** — decorative container that groups related
  knobs (e.g., Op 1 / Op 2 boxes on the FM synth).

These aren't full widgets in the "interactive user input" sense —
they're custom-painted visualizations driven by parameter values. The
user adjusts knobs; these displays reflect the change. This spec
covers them as a cluster; each sub-display has the same contract
(read values, paint output) with different content.

## Non-goals

- **Interactive editing of the visualization** — clicking an ADSR
  curve to drag breakpoints, clicking the FM diagram to route
  operators. Out of scope for v2.0. Instrument params are edited via
  knobs only.
- **3D rendering** — visuals are 2D primitives only.
- **Real-time signal rendering** — these show *parameters* (settings),
  not *signals*. For live signal visualization use
  VisualizerWidget.

---

## Common contract

All sub-displays share:

- Small, content-driven size (typically 60×40 to 180×120 logical px).
- Paint-only — no user input.
- Update when relevant parameter values change (caller invalidates
  via `invalidate()`).
- Non-focusable, non-interactive, no accessibility role (purely
  decorative / informational).

Per-display API follows a consistent pattern: setters for each
parameter the display reads, `setColor` for the accent, a
`setShowLabels(bool)` toggle for text annotations.

---

## ADSRDisplay

### Visual anatomy

```
       ▲                              amplitude
       │
       │  ╱│╲
       │ ╱ │ ╲____________________
       │╱  │            │          ╲
       │───┴───┬────────┴──────────┴───▶ time
       │ A   D           S           R
       │
```

Curve drawn:
- Rise from (0, 0) to (A, peak) — attack.
- Fall from (A, peak) to (A+D, sustain) — decay.
- Flat at sustain level until release triggers.
- Fall from sustain to 0 over release duration.

### Public API

```cpp
class ADSRDisplay : public Widget {
public:
    ADSRDisplay();

    // Parameters (all in seconds or normalized as appropriate)
    void setAttack(float sec);
    void setDecay(float sec);
    void setSustain(float level01);
    void setRelease(float sec);

    // Visual config
    void setTimeRange(float maxSec);     // x-axis scale; default 5 sec
    void setAccentColor(Color c);
    void setShowLabels(bool);            // A/D/S/R letters
    void setCurveStroke(float px);
    void setFillBelow(bool);             // fill area under curve

    // Accessibility (decorative; minimal)
    void setAriaLabel(const std::string&);
};
```

### Paint algorithm

1. Map A/D/S/R to pixel coordinates within bounds.
2. Draw 4-segment polyline (rise, fall to sustain, sustain hold,
   release).
3. Optional fill: polygon from (0, bottom) along curve to (max,
   bottom).
4. Optional A/D/S/R letter labels at segment midpoints.

### Layout

Measure returns preferred size (120×60 logical px) or uses
constraint.

Size policy: Stretch / Stretch (fills FlexBox cell).

Paint is fully paint-only; no children.

---

## FMAlgorithmDisplay

### Visual anatomy

```
                    ┌─────┐
                    │ Op1 │ ─┐
                    └─────┘  │          Op1 modulates Op2, Op2 is carrier.
                             ▼
                           ┌─────┐
                           │ Op2 │ ─▶ out
                           └─────┘

    (algorithm 1: simple 2-op serial)
```

Renders operator boxes connected by lines based on the current FM
**algorithm index** (FM synth has 8 algorithms). Carrier operators
(output) drawn with a bold border; modulators drawn plain.

### Public API

```cpp
class FMAlgorithmDisplay : public Widget {
public:
    FMAlgorithmDisplay();

    // Parameters
    void setAlgorithm(int idx);           // 0..7
    void setOperatorCount(int n);          // default 4

    // Visual
    void setOperatorColor(Color c);
    void setConnectionColor(Color c);
    void setCarrierBorderColor(Color c);
    void setShowOperatorNumbers(bool);

    void setAriaLabel(const std::string&);
};
```

### Paint

Each algorithm has a declarative layout defined internally — a list
of operator rects and a list of connection lines. The display picks
the algorithm's layout, renders boxes + lines.

Algorithm layouts are baked in (hardcoded per algorithm index).
Future: data-driven via `setAlgorithmLayouts(...)` for custom
synths.

### Layout

Preferred size 180×120. Content scales to bounds (rects and lines
scale linearly).

---

## OscillatorWaveformDisplay

### Visual anatomy

```
    ┌──────────────────────────────────┐
    │                                    │
    │  ∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿∿    │   ← computed waveform
    │                                    │
    └──────────────────────────────────┘
```

Shows a single cycle of the oscillator's waveform, computed from:
- Wave type (sine / saw / square / triangle / noise / custom)
- Shape parameters (pulse width, wavetable position, etc.)

### Public API

```cpp
enum class WaveShape {
    Sine, Saw, Square, Triangle, Pulse, Noise,
    WavetableSample,    // caller provides samples
};

class OscillatorWaveformDisplay : public Widget {
public:
    OscillatorWaveformDisplay();

    // Parameters
    void setShape(WaveShape s);
    void setPulseWidth(float w);          // 0..1, for Pulse shape
    void setWavetableSample(const float* samples, int n);
    // For custom wavetable display; caller copies data in.

    // Visual
    void setAccentColor(Color c);
    void setStrokeWidth(float px);
    void setShowZeroLine(bool);

    void setAriaLabel(const std::string&);
};
```

### Paint

Computes N samples (N = viewport width) synthesizing one period of
the wave shape with current parameters, draws as polyline.

---

## FilterResponseDisplay

### Visual anatomy

```
    ┌──────────────────────────────────┐
    │                                    │
    │                 ╱╲                │
    │          ______╱  ╲______           │   ← magnitude response
    │         ╱           ╲____          │
    │ ___    ╱                  ╲        │
    └──────────────────────────────────┘
       ← low           →high frequencies
```

Shows magnitude response of a filter given current cutoff / resonance
/ type. X-axis is log-frequency, Y-axis is linear magnitude (or dB).

### Public API

```cpp
enum class FilterType {
    Lowpass, Highpass, Bandpass, Notch, Allpass, Peak,
};

class FilterResponseDisplay : public Widget {
public:
    FilterResponseDisplay();

    // Parameters
    void setType(FilterType t);
    void setCutoff(float hz);
    void setResonance(float q);           // Q factor; 0.707 = no peak
    void setSampleRate(float hz);         // default 48000

    // Visual
    void setAccentColor(Color c);
    void setDbRange(float minDb, float maxDb);
    void setFrequencyRange(float hz0, float hz1);
    void setShowCutoffMarker(bool);

    void setAriaLabel(const std::string&);
};
```

### Paint

Computes the filter's transfer function at N log-spaced frequencies,
plots magnitude as a curve. Optional vertical marker at cutoff
frequency.

Sweet spot: N = 128 points for a small visualizer, smooth curve, low
compute cost.

---

## LFOWaveformDisplay

### Visual anatomy

Similar to OscillatorWaveformDisplay but:
- Shows LFO waveforms (sine / triangle / saw / square / S&H) at a
  slower visual scale.
- Optional phase indicator showing current LFO phase position (dot
  on the curve).
- Used in LFO device and similar UIs.

### Public API

```cpp
class LFOWaveformDisplay : public Widget {
public:
    LFOWaveformDisplay();

    void setShape(WaveShape s);           // reuses enum from Oscillator
    void setPhase(float phase01);         // current position; -1 = don't draw indicator
    void setCycles(int n);                // how many cycles to show (default 2)

    void setAccentColor(Color c);
    void setPhaseIndicatorColor(Color c);
    void setShowPhaseIndicator(bool);

    void setAriaLabel(const std::string&);
};
```

Phase indicator animates if caller updates phase per frame. Treat
as paint-only invalidation — set `requiresContinuousRepaint()` when
phase is being live-updated.

---

## GroupedKnobBody

Decorative container rather than a "display" per se — draws a subtle
grouped background behind a set of child knobs to visually cluster
them. Used on the FM synth's Op 1 / Op 2 / Op 3 / Op 4 panels.

### Visual anatomy

```
    ┌ Op 1 ──────────────────────┐
    │                              │
    │    [k1] [k2] [k3] [k4]      │
    │                              │
    └──────────────────────────────┘
```

Parts:
- Bordered box with subtle rounded corners, `palette.surface`
  background.
- Optional top-left title label ("Op 1", "Filter", etc.).
- Children laid out via internal FlexBox or Grid (caller provides
  the child layout).

### Public API

```cpp
class GroupedKnobBody : public Widget {
public:
    GroupedKnobBody();
    explicit GroupedKnobBody(std::string title);

    // Content
    void setTitle(std::string);
    void setContent(Widget* child);       // typically a FlexBox / Grid of knobs

    // Appearance
    void setBackgroundColor(std::optional<Color>);
    void setBorderColor(std::optional<Color>);
    void setShowBorder(bool);
    void setPadding(Insets);              // default { 8, 20, 8, 8 } — extra top for title
};
```

### Paint

- Fill background rect.
- Draw border.
- Paint title text at top-left (if present).
- Recurse into child widget.

Trivial widget; included in this spec batch because it's part of
the instrument-display family visually.

---

## Common layout behavior

All sub-displays:
- `onMeasure` returns preferred size clamped to constraints.
- `onLayout` stores bounds (GroupedKnobBody additionally lays out
  its child with padding applied).
- Size policy: Stretch on both axes — fills parent cell.
- Relayout boundary: yes (content-driven size doesn't change for
  parameter value changes, only for text content changes).

## Common invalidation rules

### Measure-invalidating

- Title text (GroupedKnobBody)
- DPI / theme / font

### Paint-only

- Any parameter setter (all of them)
- Color setters
- Show/hide toggles

Parameter changes are high-frequency (user drags a knob, values
change 60 Hz) so they MUST be paint-only. This is the most critical
performance requirement for this widget family.

---

## Shared theme tokens

| Token | Use |
|---|---|
| `palette.accent` | Curve / shape color (default) |
| `palette.surface` | Background (GroupedKnobBody, display bg) |
| `palette.border` | GroupedKnobBody border |
| `palette.textPrimary` | Labels (A/D/S/R letters, Op numbers) |
| `palette.textSecondary` | Axis / grid (where applicable) |
| `metrics.fontSizeSmall` | Labels |
| `metrics.baseUnit` | Padding / margins |

Instance overrides via `setAccentColor` / `setBackgroundColor` etc.

---

## Events

None. All displays are output-only.

---

## Focus behavior

Not focusable.

---

## Accessibility

- Role: `img` or `none`.
- `aria-label` describes the display's purpose ("ADSR envelope
  curve"). Optional.
- Numeric values aren't exposed through accessibility — users who
  need exact values read the adjacent knob values.

---

## Animation

- **LFOWaveformDisplay's phase indicator** — animates when caller
  updates phase per-frame; widget requires continuous repaint in
  that case.
- **Envelope / response / waveform updates** — not animated per se;
  just repaint when parameters change.

---

## Test surface

Unit tests in `tests/test_fw2_InstrumentDisplays.cpp`:

### ADSRDisplay

1. `CurveStartsAtZero` — curve begins at (0, 0).
2. `AttackRisesToPeak` — paint includes line from (0, 0) to
   (A * scale, peak).
3. `SustainFlat` — middle segment is horizontal at sustain level.
4. `ReleaseDrops` — final segment drops from sustain to 0 over
   release.
5. `TimeRangeScaling` — total curve width fits `maxTime` seconds.

### FMAlgorithmDisplay

6. `Algorithm0Layout` — algorithm 0's operator rects and connections
   rendered.
7. `Algorithm7Layout` — algorithm 7's different layout rendered.
8. `CarrierHasBoldBorder` — operators flagged as carriers render
   with thicker border.

### OscillatorWaveformDisplay

9. `SineShapeIsSine` — paint polyline approximates a sine wave.
10. `SawHasSharpEdge` — saw wave paint has discontinuity at period
    boundary.
11. `PulseWidthAffectsSquare` — Pulse shape with width=0.2 produces
    asymmetric square wave.
12. `WavetableSampleRendered` — custom samples rendered directly.

### FilterResponseDisplay

13. `LowpassCutsHighFreqs` — magnitude at frequencies above cutoff
    is reduced.
14. `HighpassCutsLowFreqs` — inverse.
15. `ResonancePeak` — high Q produces visible peak around cutoff.
16. `CutoffMarkerDrawn` — vertical marker at cutoff frequency.

### LFOWaveformDisplay

17. `CyclesShown` — N=3 cycles draws 3 periods.
18. `PhaseIndicatorAtCurrentPhase` — indicator dot positioned at
    `phase × cycleWidth` within the first cycle.
19. `PhaseUpdateRequiresRepaint` — setPhase(...) bumps continuous
    repaint flag.

### GroupedKnobBody

20. `TitleRendered` — title text appears at top-left.
21. `ChildLaidOutInside` — content child is inset by padding.
22. `NoBorderWhenHidden` — setShowBorder(false) skips border.

### Common

23. `AllDisplaysPaintOnlyForParamChange` — setters don't bump
    measure version (critical performance test).
24. `RelayoutBoundary` — parameter changes stop at display boundary.

---

## Migration from v1

v1 has these as classes in `InstrumentDisplayWidget.h`.
Functionality is retained; v2 tightens the API:

- Consistent `setAccentColor` across all sub-displays.
- Explicit `requiresContinuousRepaint()` for phase-animating LFOs.
- Theme-token consumption instead of hardcoded colors.

Signature changes minimal; should be largely drop-in.

---

## Open questions

1. **Unify into a single `ParamDisplay` base?** Possible, but each
   sub-display has enough unique parameters that a common base
   would just be `Widget`. Keep as independent classes.

2. **Data-driven FM algorithm layouts?** Current: hardcoded for
   FM synth's 8 algorithms. If we add another FM-style synth with
   different algorithms, pass `std::function<AlgorithmLayout(int)>`.

3. **Interactive display editing?** "Drag attack-point of the ADSR
   curve to change attack time." Out of scope; nice future feature
   for dedicated envelope editor widgets.

4. **3D rendering for complex instruments?** E.g., a wavetable synth
   showing its 3D surface. Out of scope.

5. **Consistent reset (double-click = reset to default)?** Knobs have
   this; displays don't have values to reset. N/A.

6. **Screenshot / export?** Occasional preset-browser need ("show
   me what the envelope of this preset looks like"). Future work.
