# VisualizerWidget — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** `fw::VisualizerWidget` in
`src/ui/framework/VisualizerWidget.h`.

---

## Intent

A **real-time signal visualizer** for audio: oscilloscope (time-
domain waveform) or spectrum analyzer (frequency-domain FFT). Shown
on device panels next to effects (Oscilloscope, Spectrum Analyzer
effects), on track strips, and wherever a live-signal preview is
useful.

Updates at 60 Hz. Renders from a caller-provided data source that
writes to lock-free ring buffers from the audio thread; the widget
reads the latest samples each frame without blocking audio.

## Non-goals

- **Offline rendering / snapshot export.** Live only.
- **Advanced FFT features.** FFT size, window, and smoothing are
  configurable, but no multi-channel correlation, phase displays,
  vectorscope, or other exotic visualizations. Those become separate
  widgets when real use cases appear.
- **Data buffering / source management.** The widget reads; the
  caller owns the data source and its lifecycle. Decoupled so the
  same widget can visualize many sources.
- **Mouse interaction.** Purely visual. Right-click for a settings
  context menu (scope / range / color) is caller-wired, not built in.

---

## Visual anatomy

### Oscilloscope mode

```
    ┌─────────────────────────────────────┐
    │  ∿∿∿∿∧∧∧∧∧∧∧∧∿∿∿∿∿∿∿∿∧∧∧∧∧∧∿∿∿∿∿∿∿  │
    │                                      │  ← waveform trace
    │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│ ← center/zero line
    │                                      │
    │  ∿∿∿∿∧∧∧∧∧∧∧∿∿∿∿∿∿∿∿∧∧∧∧∧∧∿∿∿∿∿∿∿  │
    └─────────────────────────────────────┘
```

### Spectrum mode

```
    ┌─────────────────────────────────────┐
    │ ░░░▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▓▓▓▓▒▒▒▒▒▒░░░│
    │ ░░▓▓▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▓▓▓▓▓▓▓▒▒▒▒▒░│  ← frequency bars
    │ ▓▓▓▓▓▓▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▓▓▓▓▓▓▓▓▓▓▓▒▒▒│
    │ ▓▓▓▓▓▓▓▓▓▓▓▓▓▒▒▒▒▒▒▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▒│
    └─────────────────────────────────────┘
       ←── frequency axis (log) ──→
```

Parts:
1. **Background** — `palette.controlBg` or caller override.
2. **Center / zero reference line** (oscilloscope only) —
   horizontal line at y = bounds.h / 2.
3. **Data trace** — the actual signal visualization.
4. **Overlay grid** (optional) — light grid at dB / frequency
   markers.

---

## Modes

```cpp
enum class VisualizerMode {
    Oscilloscope,   // time-domain; shows raw samples
    Spectrum,       // frequency-domain; FFT bars
};
```

Mode set once per widget — typical pattern is one Oscilloscope widget
and one Spectrum widget side-by-side on the device strip. Switching
modes at runtime is supported but cache-busting, so don't do it
often.

### Oscilloscope

Reads `N` most recent samples from the source's ring buffer and draws
as a polyline. Sample count matches viewport width at 1 sample per
pixel for maximum detail, or decimates if buffer is larger than
viewport.

Center line at `y = bounds.h / 2` represents zero. Samples in
`[-1, 1]` map to y in `[0, bounds.h]` (with a configurable dB
scale).

### Spectrum

Performs an FFT on the source's latest `fftSize` samples (default
1024). Magnitude per bin mapped to a log frequency axis. Bars drawn
vertically, height proportional to magnitude (dB scale).

Smoothing (exponential decay) applied to bar heights per frame so
the display has a "meter ballistics" feel rather than jittery
per-frame flicker.

---

## Data source

```cpp
class VisualizerDataSource {
public:
    virtual ~VisualizerDataSource() = default;

    // Oscilloscope: fill `out` with the N most recent samples.
    // Caller passes desired count; source writes up to that many,
    // returns actual count written.
    virtual int readLatestSamples(float* out, int count) = 0;

    // Spectrum: same, but the widget runs FFT on the returned samples.
    // Source doesn't compute FFT — just provides time-domain data.

    // Must be thread-safe for audio-thread write + UI-thread read.
    // Standard pattern: lock-free ring buffer where audio thread
    // writes, UI reads latest without blocking.
};
```

YAWN's existing infrastructure has these for mixer bus taps, effect
pre/post signal, master bus monitor, etc. The widget doesn't care
which source; just reads.

---

## Public API

```cpp
class VisualizerWidget : public Widget {
public:
    VisualizerWidget();
    explicit VisualizerWidget(VisualizerMode mode);

    // Data source
    void setSource(VisualizerDataSource* source);
    // Caller owns; widget holds raw pointer. Widget unreads/reads
    // each frame; no retention of old data.

    // Mode
    void setMode(VisualizerMode mode);
    VisualizerMode mode() const;

    // Oscilloscope settings
    void setOscilloscopeAmplitudeScale(float scale);  // default 1.0 = [-1..1] → full height
    void setOscilloscopeStroke(float px);             // trace line thickness

    // Spectrum settings
    void setFftSize(int n);                            // power of 2, 256..4096
    void setSpectrumSmoothing(float alpha);            // [0..1], default 0.5
    void setSpectrumDbRange(float minDb, float maxDb); // default -80..0
    void setSpectrumFrequencyRange(float hz0, float hz1);
    // default 20..20000
    void setSpectrumBarCount(int n);                   // 0 = auto (one per viewport px)
    void setSpectrumBarGap(float px);                  // default 1

    // Appearance
    void setTraceColor(Color c);                       // oscilloscope trace, spectrum bars
    void setBackgroundColor(std::optional<Color>);
    void setShowCenterLine(bool);                      // default true (oscilloscope)
    void setShowGrid(bool);                            // default false
    void setGridColor(Color);

    // Performance knobs
    void setSubsamplePolicy(SubsamplePolicy);
    // How to reduce waveform when sample count > viewport pixels.

    // Accessibility
    void setAriaLabel(const std::string&);
};

enum class SubsamplePolicy {
    MinMax,      // default — for each pixel, find min/max in the sample window and draw vertical line between
    Average,     // average of samples in window
    Peek,        // single sample at pixel's center
};
```

---

## Layout contract

### `onMeasure(Constraints c, UIContext& ctx) → Size`

VisualizerWidget is content-agnostic on size — it renders into
whatever bounds it's given. Caller controls sizing via FlexBox or
explicit constraints:

```cpp
Size VisualizerWidget::onMeasure(Constraints c, UIContext& ctx) {
    // Prefer constraint maximum; fall back to sensible defaults.
    float w = c.hasBoundedWidth() ? c.maxW : 200.0f;
    float h = c.hasBoundedHeight() ? c.maxH : 80.0f;
    return c.constrain({w, h});
}
```

### `onLayout(Rect b, UIContext& ctx)`

No children. Store bounds. Trivially cached.

### Size policy

```cpp
SizePolicy{ width = Stretch, height = Stretch }
```

Typical usage: sits in a FlexBox cell with fixed cross-axis height;
fills main-axis width.

### Relayout boundary

**Yes.** Data changes don't affect size.

### Caching

Measure cache trivial.

Render is NOT cached — each frame reads fresh samples. Widget sets
`requiresContinuousRepaint() = true` whenever a source is active.

---

## Continuous repaint

Like ProgressBar in indeterminate mode, VisualizerWidget requires
per-frame repaint because data changes every frame. Framework's
continuous-paint mechanism re-invokes `paint()` every frame while
the widget is visible and has a source.

Implementation: in `paint()`, the widget re-reads samples from the
source and re-draws. No caching of drawing commands — text uses
`drawLine` / `drawRect` that get re-emitted each frame. Performance
is bounded by viewport size × mode cost (oscilloscope: O(viewport
width); spectrum: O(fftSize) FFT + O(barCount) draws).

---

## Paint algorithm

### Oscilloscope

```cpp
void VisualizerWidget::paintOscilloscope(UIContext& ctx) {
    int nSamples = m_bounds.w;      // one sample per logical pixel
    static thread_local std::vector<float> buf;
    buf.resize(nSamples);
    int actual = m_source->readLatestSamples(buf.data(), nSamples);
    if (actual == 0) {
        // No data: draw center line only
        if (m_showCenterLine) drawCenterLine(ctx);
        return;
    }

    // If oversampled (actual > nSamples), subsample per policy.
    // ...

    // Draw polyline
    float centerY = m_bounds.y + m_bounds.h * 0.5f;
    float halfH = m_bounds.h * 0.5f * m_ampScale;
    for (int i = 1; i < actual; ++i) {
        float x0 = m_bounds.x + (i - 1);
        float x1 = m_bounds.x + i;
        float y0 = centerY - buf[i-1] * halfH;
        float y1 = centerY - buf[i]   * halfH;
        ctx.renderer->drawLine(x0, y0, x1, y1, m_traceColor, m_strokePx);
    }
}
```

Subsample policies for when buffer exceeds viewport:
- **MinMax** (default): per viewport pixel, compute min and max of
  the sample window, draw a vertical line between them. Preserves
  transient detail at the cost of smoothness.
- **Average**: mean of window — smoother, loses transients.
- **Peek**: single sample at window center — fastest, aliasing
  artifacts visible for long buffers.

### Spectrum

```cpp
void VisualizerWidget::paintSpectrum(UIContext& ctx) {
    static thread_local std::vector<float> buf;
    buf.resize(m_fftSize);
    int actual = m_source->readLatestSamples(buf.data(), m_fftSize);

    // Apply Hann window
    applyHannWindow(buf.data(), actual);

    // FFT
    static thread_local std::vector<std::complex<float>> fftOut;
    fftOut.resize(m_fftSize / 2);
    forwardFft(buf.data(), fftOut.data(), m_fftSize);

    // Magnitude to dB per bin
    for (int i = 0; i < m_fftSize / 2; ++i) {
        float mag = std::abs(fftOut[i]);
        float db = 20.0f * std::log10(std::max(mag, 1e-12f));
        m_smoothedDb[i] = m_smoothing * m_smoothedDb[i] + (1.0f - m_smoothing) * db;
    }

    // Map bars to frequency buckets (log axis), draw each as vertical rect.
    float binHz = sampleRate / m_fftSize;
    int barCount = m_barCount > 0 ? m_barCount : static_cast<int>(m_bounds.w);
    for (int b = 0; b < barCount; ++b) {
        // Compute this bar's frequency window in log space
        float t0 = float(b) / barCount;
        float t1 = float(b + 1) / barCount;
        float hz0 = expLerp(m_freqMin, m_freqMax, t0);
        float hz1 = expLerp(m_freqMin, m_freqMax, t1);

        // Average dB of bins in [hz0, hz1]
        float avgDb = averageBinsInRange(hz0, hz1, binHz);

        // Normalize to bar height
        float norm = (avgDb - m_dbMin) / (m_dbMax - m_dbMin);
        float barH = m_bounds.h * clamp(norm, 0.0f, 1.0f);

        float barX = m_bounds.x + b * (m_bounds.w / barCount);
        float barW = (m_bounds.w / barCount) - m_barGap;
        ctx.renderer->drawRect(barX, m_bounds.y + m_bounds.h - barH,
                                barW, barH, m_traceColor);
    }
}
```

FFT implementation: built-in `KissFFT`-style or similar; small
dependency. Already exists in `src/effects/SpectrumAnalyzer.h`
implicitly; v2 exposes it as a shared utility.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.controlBg` | Background (if no override) |
| `palette.accent` | Default trace color |
| `palette.textDim` | Grid lines, center line |
| `palette.textSecondary` | Any axis labels (future) |
| `metrics.borderWidth` | Grid line thickness |

---

## Events fired

None. VisualizerWidget is output-only; user interactions on it
(right-click for settings menu) are handled by parent widgets.

---

## Invalidation triggers

### Measure-invalidating

- DPI / theme / font (global)

### Paint-only (re-rendered every frame anyway)

- Mode change (Spectrum ↔ Oscilloscope)
- FFT size, smoothing, dB range, frequency range (spectrum)
- Amplitude scale, stroke (oscilloscope)
- Trace / background / grid colors

### Data flow

- Data source is read every frame; no explicit invalidation. If
  source is nullptr, widget paints empty (center line only).

---

## Focus behavior

Not focusable. Visualizer is passive output.

---

## Accessibility (reserved)

- Role: `img` — visualization is a graphical display; screen readers
  don't normally consume it. Provides `aria-label` for context.
- Future: could expose current peak dB / instantaneous RMS as
  `aria-valuetext` for rough accessibility reads.

---

## Animation

All paint is implicit animation via continuous repaint. No explicit
animations at the widget level (fade in/out of state changes happens
through smoothing parameter).

---

## Test surface

Unit tests in `tests/test_fw2_VisualizerWidget.cpp`. Harder to
test than static widgets — use a mock data source that returns
deterministic samples.

### Basic

1. `DefaultOscilloscope` — measures to constraint; paints center
   line with no data.
2. `ModeChangeRerenders` — switching from Oscilloscope to Spectrum
   re-computes cache.
3. `NullSourcePaintsEmpty` — no source → center line only.

### Oscilloscope

4. `SamplesPaintAsPolyline` — mock source provides known sine →
   paint emits expected drawLine sequence.
5. `AmplitudeScaleApplied` — samples at [-1, 1] with scale 0.5
   produce half-height draws.
6. `MinMaxSubsampling` — source provides 2N samples for N-pixel
   viewport: per pixel, min/max vertical lines drawn.
7. `AverageSubsampling` — Average policy averages window.
8. `PeekSubsampling` — Peek picks single center-of-window sample.

### Spectrum

9. `SpectrumFftSine` — source produces 1 kHz sine at 48 kHz sample
   rate: expected peak bar around 1 kHz frequency.
10. `SpectrumSmoothing` — smoothing=0.5 averages current with
    previous frame's values; no frame-to-frame jitter on constant
    input.
11. `SpectrumDbRangeClips` — magnitudes outside [dbMin, dbMax]
    clamp to edges of visualizer.
12. `SpectrumLogFrequency` — bars mapped to log scale: doubling
    frequency moves by fixed pixel amount.

### Continuous repaint

13. `ContinuousRepaintFlagSetWhenActive` — with source + visible:
    flag = true.
14. `ContinuousRepaintFlagClearedWhenHidden` — setVisible(false)
    clears flag.
15. `NoRepaintWhenNullSource` — flag = false when source == nullptr.

### Cache

16. `MeasureCacheHit` — measure constraints unchanged → cache.
17. `NoDataCachingBetweenFrames` — each frame reads fresh samples;
    no cross-frame caching.

---

## Migration from v1

v1 `VisualizerWidget` in `src/ui/framework/VisualizerWidget.h`
already exists with similar functionality. Used by Oscilloscope and
Spectrum Analyzer effects, and in various device visualizers.

Migration:
1. Consolidate existing oscilloscope + spectrum implementations
   behind the v2 API.
2. Widget source is abstracted (v1 had it hardcoded per instance;
   v2 uses the VisualizerDataSource interface).
3. Subsample policy exposed; v1 hardcoded Peek (visible aliasing).
4. FFT shared utility; v1 had separate implementations per effect.

---

## Open questions

1. **Color gradient for spectrum bars?** Low-frequency bass could
   render red, mids green, highs blue. Visual cue without labels.
   Easy add; defer until designer mockup.

2. **Peak hold lines?** Horizontal marker at each bar's peak
   magnitude from the past N seconds, for "is it clipping?"
   readability. Common in meter plugins. Easy opt-in.

3. **Logarithmic vs linear amplitude?** Oscilloscope uses linear.
   dB-scaled oscilloscope (visible amplitude compression) is a
   niche preference. Add if requested.

4. **Stereo correlation display?** Not in scope; separate widget.

5. **Freeze / snapshot?** Spacebar-pause-the-visualizer for
   inspection? Niche; right-click menu callback.

6. **Dark / light mode traces?** Trace color via theme; caller
   override still works. Theme swap → paint updates next frame.
