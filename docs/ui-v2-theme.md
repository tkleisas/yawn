# YAWN UI v2 — Theme System Deep Dive

Reference for the runtime-swappable theme system: palette + metrics
struct definitions, JSON file format, loading / hot-swap mechanics,
per-widget overrides, and migration from v1's compile-time theme.

Architecture: see [`ui-v2-architecture.md`](ui-v2-architecture.md)
Theme section.

---

## Why runtime themes

v1 theme is `inline constexpr` — colors and sizes compiled in. To
change a color, you recompile. v2 needs:

- **User-visible theme switching** — light mode, high-contrast,
  custom palettes.
- **JSON theme files** — community-shared themes, user tweaks
  without a C++ toolchain.
- **Per-context overrides** — an accent color per track / per plugin
  without global state.

Runtime themes = everything is data. Widgets read tokens via
`theme().palette.textPrimary` instead of `Theme::textPrimary`.

---

## Theme structure

Two parts: **palette** (colors) and **metrics** (sizes / spacing).

```cpp
struct Theme {
    std::string    name;        // "Dark", "Light", "HighContrast", "Custom"
    std::string    version;     // "1.0" — for JSON forward-compat
    ThemePalette   palette;
    ThemeMetrics   metrics;
};
```

Theme is a value type — copyable, hashable, comparable. Swapping
themes is `setTheme(Theme{...})`.

---

## ThemePalette — full reference

```cpp
struct ThemePalette {
    // ─── Backgrounds ───────────────────────────────────────────
    Color background;       // app root fill
    Color panelBg;           // panel / mixer / session body
    Color surface;           // menu bars, table headers, elevated strips
    Color elevated;          // dialogs, dropdowns, tooltips — "floating above"

    // ─── Controls ──────────────────────────────────────────────
    Color controlBg;         // buttons, toggles off, text inputs
    Color controlHover;      // hover brightened
    Color controlActive;     // pressed / active darkened
    Color border;            // standard widget borders
    Color borderSubtle;       // dividers that shouldn't draw attention

    // ─── Accent & state ────────────────────────────────────────
    Color accent;            // primary brand color
    Color accentHover;
    Color accentActive;

    Color success;           // "done", "healthy"
    Color warn;              // "attention", "armed"
    Color error;             // "failure", "destructive action"

    Color playing;           // mixer strip playing indicator, session clip green
    Color recording;         // record-arm red
    Color queued;            // launch-quantize yellow

    // ─── Modulation overlays ──────────────────────────────────
    Color modulation;        // LFO / automation-target indicator on knobs
    Color modulationRange;   // excursion halo (typically modulation.withAlpha(60))

    // ─── Text ─────────────────────────────────────────────────
    Color textPrimary;       // default text
    Color textSecondary;     // less-important text (subtitles, disabled-ish)
    Color textDim;           // very dim (placeholder, disabled)
    Color textOnAccent;      // text on colored backgrounds (accent fills)
    Color textOnError;       // text on error backgrounds

    // ─── Overlays ─────────────────────────────────────────────
    Color dropShadow;        // shadow under dropdowns, menus, dialogs
    Color scrim;             // modal scrim darkener

    // ─── Track colors (rotating palette) ──────────────────────
    std::array<Color, 8> trackColors;  // assigned round-robin to tracks
};
```

Every widget spec lists which tokens it consumes. The widget base
class and common utilities also read from this palette.

### Color type

```cpp
struct Color {
    uint8_t r, g, b, a;

    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    constexpr Color withAlpha(uint8_t alpha) const;
    static Color lerp(Color a, Color b, float t);
    Color desaturate(float amount) const;    // for disabled states
    Color brighten(float amount) const;
    Color darken(float amount) const;
};
```

Alpha channel in the palette is typically 255; widget code derives
transparent variants via `withAlpha`.

---

## ThemeMetrics — full reference

```cpp
struct ThemeMetrics {
    // ─── Spacing ──────────────────────────────────────────────
    float baseUnit    = 4.0f;       // the fundamental spacing unit — all paddings are multiples
    float cornerRadius = 3.0f;       // default rounded-corner radius
    float borderWidth  = 1.0f;

    // ─── Control dimensions ──────────────────────────────────
    float controlHeight     = 28.0f;   // buttons, text inputs, dropdown triggers
    float scrollbarThickness = 12.0f;
    float splitterThickness  = 6.0f;

    // ─── Typography ──────────────────────────────────────────
    float fontSize        = 13.0f;     // default UI text
    float fontSizeSmall   = 11.0f;     // captions, unit suffixes
    float fontSizeLarge   = 16.0f;     // dialog titles, section headers

    // Font face — path relative to app resource dir, or absolute.
    // If empty, falls back to the default bundled JetBrainsMono.
    std::string fontPath;

    float lineHeightMultiplier = 1.2f;  // line spacing when wrapping
};
```

All dimensions in **logical pixels** — the framework multiplies by
DPI scale at paint time. So `controlHeight = 28` reads as "28
logical pixels ≈ 28 physical pixels at 1× DPI, 42 at 1.5× DPI".

---

## The default theme

YAWN ships with a single default dark theme, matching the v1 look:

```cpp
// File: src/ui/framework/v2/themes/dark.cpp (generated from dark.json).
Theme kDarkTheme = {
    .name = "Dark",
    .version = "1.0",
    .palette = {
        .background     = {30, 30, 33},
        .panelBg        = {40, 40, 43},
        .surface        = {50, 50, 55},
        .elevated       = {58, 58, 62},

        .controlBg      = {40, 40, 43},
        .controlHover   = {65, 65, 75},
        .controlActive  = {80, 80, 85},
        .border         = {70, 70, 75},
        .borderSubtle   = {55, 55, 58},

        .accent         = {255, 165, 0},
        .accentHover    = {255, 180, 50},
        .accentActive   = {220, 140, 0},

        .success        = {80, 230, 80},
        .warn           = {255, 200, 50},
        .error          = {220, 70, 70},

        .playing        = {80, 230, 80},
        .recording      = {255, 60, 60},
        .queued         = {255, 200, 50},

        .modulation      = {100, 150, 255},
        .modulationRange = {100, 150, 255, 60},

        .textPrimary    = {220, 220, 220},
        .textSecondary  = {140, 140, 145},
        .textDim        = {90, 90, 95},
        .textOnAccent   = {20, 20, 22},
        .textOnError    = {255, 255, 255},

        .dropShadow     = {0, 0, 0, 120},
        .scrim          = {0, 0, 0, 140},

        .trackColors = {{
            {255, 120,  50},  // orange
            {100, 180, 255},  // blue
            {120, 220, 100},  // green
            {220, 100, 220},  // purple
            {255, 220,  60},  // yellow
            {100, 220, 220},  // cyan
            {255, 100, 130},  // pink
            {180, 180, 100},  // olive
        }},
    },
    .metrics = {
        .baseUnit             = 4.0f,
        .cornerRadius         = 3.0f,
        .borderWidth          = 1.0f,
        .controlHeight        = 28.0f,
        .scrollbarThickness   = 12.0f,
        .splitterThickness    = 6.0f,
        .fontSize             = 13.0f,
        .fontSizeSmall        = 11.0f,
        .fontSizeLarge        = 16.0f,
        .fontPath             = "",  // default bundled font
        .lineHeightMultiplier = 1.2f,
    },
};
```

Future themes — a "Light" variant, a "High Contrast" variant — live
as JSON files in `~/.yawn/themes/` (or packaged with the app).

---

## Access pattern

```cpp
// Read current theme
const Theme& theme();

// Swap theme — bumps UIContext epoch, invalidates measure caches,
// triggers repaint.
void setTheme(Theme t);
```

Widgets read tokens directly:

```cpp
void FwButton::paint(UIContext& ctx) {
    Color fill = isHovered() ? theme().palette.controlHover
                              : theme().palette.controlBg;
    float radius = theme().metrics.cornerRadius;
    ctx.renderer->drawRoundedRect(m_bounds, radius, fill);
    // ...
}
```

`theme()` is O(1) — returns a reference to the globally-held current
theme. No locking needed on UI thread; theme swap is synchronized
with frame boundary by the app.

### Thread safety

- **UI thread** reads `theme()` freely.
- **Audio thread** does NOT read theme — audio thread never touches
  UI colors.
- **Theme swap** happens on UI thread between frames. Not safe to
  call from audio thread (would need std::atomic + RCU-style swap;
  not needed).

---

## Per-widget overrides

Callers can override specific theme values per-widget-instance:

```cpp
// Fader's setAccentColor overrides theme().palette.accent for just this widget.
fader.setAccentColor({0, 255, 128, 255});
```

Pattern: widget stores `std::optional<Color> m_accentOverride`; paint
uses override if set, falls back to theme:

```cpp
Color accent = m_accentOverride.value_or(theme().palette.accent);
```

Every widget spec's "Theme tokens consumed" section lists which
tokens have per-instance overrides. Not every token is overridable —
only ones that make sense (accents, specific fills). Common
structural colors (border, shadow) aren't per-widget.

### `clear` variants

```cpp
fader.setAccentColor({0, 255, 128});
// ...
fader.clearAccentColor();   // revert to theme's accent
```

Widgets that have overrides provide `clearXxxColor()` to reset.

---

## JSON file format

Themes can be loaded from JSON files. File format:

```json
{
    "name": "Dark",
    "version": "1.0",
    "palette": {
        "background":     [30, 30, 33],
        "panelBg":        [40, 40, 43],
        "surface":        [50, 50, 55],
        "elevated":       [58, 58, 62],

        "controlBg":      [40, 40, 43],
        "controlHover":   [65, 65, 75],
        "controlActive":  [80, 80, 85],
        "border":         [70, 70, 75],
        "borderSubtle":   [55, 55, 58],

        "accent":         [255, 165, 0],
        "accentHover":    [255, 180, 50],
        "accentActive":   [220, 140, 0],

        "success":        [80, 230, 80],
        "warn":           [255, 200, 50],
        "error":          [220, 70, 70],

        "playing":        [80, 230, 80],
        "recording":      [255, 60, 60],
        "queued":         [255, 200, 50],

        "modulation":      [100, 150, 255],
        "modulationRange": [100, 150, 255, 60],

        "textPrimary":    [220, 220, 220],
        "textSecondary":  [140, 140, 145],
        "textDim":        [90, 90, 95],
        "textOnAccent":   [20, 20, 22],
        "textOnError":    [255, 255, 255],

        "dropShadow":     [0, 0, 0, 120],
        "scrim":          [0, 0, 0, 140],

        "trackColors": [
            [255, 120,  50],
            [100, 180, 255],
            [120, 220, 100],
            [220, 100, 220],
            [255, 220,  60],
            [100, 220, 220],
            [255, 100, 130],
            [180, 180, 100]
        ]
    },
    "metrics": {
        "baseUnit":             4.0,
        "cornerRadius":         3.0,
        "borderWidth":          1.0,
        "controlHeight":        28.0,
        "scrollbarThickness":   12.0,
        "splitterThickness":    6.0,
        "fontSize":             13.0,
        "fontSizeSmall":        11.0,
        "fontSizeLarge":        16.0,
        "fontPath":             "",
        "lineHeightMultiplier": 1.2
    }
}
```

### Color format

Colors are JSON arrays:
- `[r, g, b]` — 3 entries, alpha defaults to 255.
- `[r, g, b, a]` — 4 entries, explicit alpha.
- Values are 0..255 integers.

No hex strings (`"#FFAA00"`) in v2.0 — simplifies parsing. Could add
as convenience later.

### Loading

```cpp
// Load from file; returns nullopt on parse / validation error.
std::optional<Theme> loadThemeFromJson(const std::string& path);

// Load from JSON string.
std::optional<Theme> parseThemeJson(const std::string& json);
```

Validation rules:
- Top-level `name`, `version` must be strings.
- Every palette field must be present (no partial themes in v2.0 —
  see open questions).
- Colors must be 3- or 4-entry arrays.
- Metrics must be numeric.

On validation failure: returns `std::nullopt`, logs the specific error
to the app's log. Caller decides fallback (typically: load default
dark theme).

### Saving

```cpp
std::string serializeThemeJson(const Theme& t);
bool saveThemeToJson(const std::string& path, const Theme& t);
```

Round-trip preserves all fields.

---

## Theme directory convention

```
~/.yawn/
├── themes/
│   ├── dark.json        ← default; shipped with app but user-editable here
│   ├── light.json
│   ├── my-custom.json   ← user-created
```

The app scans this directory on startup and exposes all found themes
in the Preferences dialog. User-selected theme persisted as a
preference string (name of the theme file).

Default file locations per platform:
- **Windows**: `%APPDATA%\YAWN\themes\`
- **macOS**: `~/Library/Application Support/YAWN/themes/`
- **Linux**: `~/.config/yawn/themes/` (XDG) or `~/.yawn/themes/`

---

## Hot-swap / live reload

Theme changes propagate via epoch bump — same mechanism as DPI
change:

```cpp
void setTheme(Theme newTheme) {
    g_currentTheme = std::move(newTheme);
    UIContext::global().bumpEpoch();
    // Next frame: all widgets' measure caches miss; re-layout.
    // Paint cache is inherently per-frame; automatic.
}
```

### Optimization: metric changes vs palette changes

Theme palette changes don't affect layout — just colors. Metrics
changes DO affect layout (fontSize affects text widths, controlHeight
affects button heights).

Ideally palette-only changes would skip measure invalidation. Current
implementation bumps epoch for both (simpler, always correct). Future
optimization: compare old vs new metrics, bump epoch only if metrics
changed.

### Hot-reload from JSON

For theme authors, a file watcher on `~/.yawn/themes/*.json`:

```cpp
// Dev mode only (behind a flag).
void App::watchThemeFile(const std::string& path) {
    // Check file mtime each second; reload + setTheme on change.
}
```

Iterating on a theme: edit JSON in external editor, save, see
YAWN update within a second. Great for theme designers.

---

## Per-track colors

Tracks cycle through `palette.trackColors[]` based on insertion
order:

```cpp
Color colorForTrack(int trackIdx) {
    return theme().palette.trackColors[trackIdx % 8];
}
```

Alternative: user-overridable per-track. Currently a simple
round-robin; extendable to explicit track-color assignment in
project data later.

---

## Accessibility considerations

### High-contrast mode

A `HighContrast` theme variant should:
- Use stronger color contrasts (black / white / pure colors).
- Thicker borders (`borderWidth = 2.0`).
- Larger focus rings.
- Bigger default font size.

Provided as `high-contrast.json`; users enable via Preferences.

### Color-blind-friendly palettes

Green / red state colors (playing / recording) are a common CVD
(color vision deficiency) problem. Alternative themes:
- Use shape / texture in addition to color (already present: state
  stripes + icons, not just color).
- Swap green → teal, red → magenta.

A `colorblind-friendly.json` could ship as an option.

### WCAG contrast ratios

All text-on-background combinations in default themes should meet
WCAG AA (4.5:1 for normal text, 3:1 for large). Validation tool
could check during theme loading and warn (dev mode).

---

## Per-widget color derivation

Rather than defining a color for every state of every widget
(explosion of tokens), the palette defines base colors and widgets
derive state variants:

```cpp
// FwButton hover = controlBg → controlHover (explicit token).
// FwButton pressed = controlHover → controlActive (explicit).
// FwButton disabled = whatever fill × 0.4 saturation (derived).

Color fill = ...;
if (!isEnabled()) fill = fill.desaturate(0.6f);
```

This keeps the palette manageable (~30 tokens) while supporting
many state permutations.

---

## Font handling

`ThemeMetrics::fontPath` points to a TTF file:
- Empty → use bundled JetBrainsMono (fallback).
- Relative path → resolve against app resources.
- Absolute path → load directly.

On theme swap with a different fontPath:
1. New font loaded via `Font::load()`.
2. UIContext's font reference updated.
3. Epoch bumped (text widths will differ → re-measure).
4. Old font texture freed on next UI thread tick.

Font loading is synchronous on UI thread (~10 ms). Acceptable for
infrequent theme swap.

---

## Testing

Unit tests in `tests/test_fw2_Theme.cpp`:

### Access

1. `DefaultThemeLoaded` — theme() returns a valid theme at startup.
2. `SetThemeUpdates` — setTheme() changes current theme.
3. `BumpsEpoch` — setTheme bumps UIContext epoch.

### JSON round-trip

4. `ParseValidJson` — parseThemeJson on default-theme JSON returns
   Theme equal to default.
5. `SerializeRoundTrips` — serialize → parse produces identical
   theme.
6. `RejectsMissingField` — JSON with missing palette field returns
   nullopt + error log.
7. `RejectsMalformedColor` — color with only 2 values or non-numeric
   returns nullopt.
8. `AcceptsColorWithoutAlpha` — 3-entry color defaults alpha = 255.
9. `AcceptsColorWithAlpha` — 4-entry color reads alpha.

### Per-widget overrides

10. `AccentColorOverride` — widget.setAccentColor uses override.
11. `ClearRevertsToTheme` — widget.clearAccentColor falls back to
    theme.
12. `OverrideDoesNotAffectOthers` — one widget's override doesn't
    change other widgets' colors.

### Hot-reload

13. `ThemeSwapTriggersRepaint` — after setTheme, next frame paints
    with new colors.
14. `MetricsChangeInvalidatesMeasure` — metrics change (fontSize)
    causes measure re-run.
15. `PaletteOnlyChangeRepaints` — palette change without metrics
    change: widgets repaint (still bumps epoch in current impl).

---

## Migration from v1

v1 `namespace Theme { inline constexpr Color ... }` replaced by
`theme()` function call. Mechanical find/replace across widget code:

```
Theme::background    →  theme().palette.background
Theme::kFontSize      →  theme().metrics.fontSize
Theme::scaled(x)      →  UIContext::scaled(x)   // DPI scaling now separate
```

One sweep across ~150 call sites. Debug overlay's "Theme" widget
inspector helps verify all references updated.

v1's `Theme::scaleFactor` / `Theme::scaled()` DPI helpers move to
`UIContext` — theme doesn't handle DPI anymore, just base logical
sizes.

---

## Open questions

1. **Partial theme overlays?** A theme JSON that only overrides a
   few colors, inheriting everything else from a base theme. Useful
   for "my slight tweak on Dark." Adds complexity (merge logic,
   base-theme resolution). Defer until requested.

2. **Color semantic naming?** Current palette fields are
   semantic-ish (textPrimary, not text200). Could go further with
   material-design-style numbered scales (color1, color2…) but
   that's overkill for our size. Current naming stays.

3. **Built-in light theme?** Should ship as a first-party
   alternative. Requires design work (contrasts, icons that work on
   light). Defer to when visual design has capacity.

4. **Per-component themes?** Some apps let you theme a single
   component differently. E.g., "green accent for the arrangement,
   red for mixer." Overkill for YAWN; stick with app-wide theme.

5. **Animation for theme swap?** Fade between old and new colors
   over 200 ms would be pretty but complex (requires keeping both
   themes in memory + per-widget cross-fade). Defer; instant swap
   is fine.

6. **Multi-color gradients in palette?** Some themes want gradients
   for fills. Current palette is solid colors only. Gradients would
   be a separate extension type.

7. **Theme API for plugins?** If YAWN ever exposes a plugin API,
   plugins should read theme tokens rather than hardcoding colors.
   Easy to add: expose `theme()` in the plugin ABI.
