# Label — Widget Specification

**Status:** draft, pending review
**Spec version:** v2.0
**Supersedes:** the `Label` class in `src/ui/framework/Primitives.h`

---

## Intent

A **passive, non-interactive text display**. Label is the framework's
primary text primitive — every other widget that shows text
(Buttons, Toggles, DeviceHeader, menu items, status bars) either
embeds a Label internally or delegates to the same `Font::drawText`
plumbing Label uses.

Label's job is to **measure its text accurately and render it within
its bounds without overflow**. The current v1 framework's label
overlap bugs are, at root, because Label's `measure()` returns
whatever constraint it's handed rather than its actual content
width. v2 Label returns its real measured width so FlexBox can pack
siblings correctly.

## Non-goals

- **Editable text.** That's `FwTextInput`.
- **Rich text / markdown / embedded icons.** Plain text only.
  Multi-line wrap is in scope; inline formatting is not.
- **Selection.** User can't select Label text with the mouse. If that
  becomes needed, `FwSelectableLabel` is a future variant.
- **Click / hover handlers.** Label is passive. Widgets that want
  clickable text wrap a Label in a Button.
- **Internationalization direction (RTL / BiDi).** Out of scope for
  v2.0. Framework-level RTL support is future work; Label honors
  whatever direction the text naturally has from its codepoints.

---

## Visual anatomy

Just text rendered by `Font::drawText`. Optional single-line
truncation with ellipsis. Optional multi-line wrapping.

```
  ┌────────────────────────┐
  │ Label text here        │   Single-line, fits
  └────────────────────────┘

  ┌─────────────────┐
  │ Label text he…  │          Single-line, truncated with ellipsis
  └─────────────────┘

  ┌─────────────────┐
  │ Label text that │          Multi-line wrap (wrap mode enabled)
  │ wraps across    │
  │ multiple lines  │
  └─────────────────┘
```

No background, no border. A Label with styling (background fill,
padding, rounded corners) is composed as `Stack{ FillRect; Label }`.

---

## States

Label has no interaction-driven states. Two visual modes only:

| Mode | Trigger | Visual |
|---|---|---|
| **normal** | default | color = theme token |
| **disabled** | `setEnabled(false)` | color desaturated to 40% — matches disabled widgets inside |

Label's enabled state matters only for visual consistency when it's a
child of a disabled button / group. No event-related disabled behavior
(Label has no events to disable).

---

## Gestures

**None.** Label is passive. All pointer / keyboard events pass
through to parents.

---

## Public API

```cpp
enum class TextAlign {
    Start,   // left (LTR) / right (RTL)
    Center,
    End,     // right (LTR) / left (RTL)
};

enum class VerticalAlign {
    Top,
    Middle,
    Bottom,
    Baseline,    // align text baseline to a specified y-offset
};

enum class Truncation {
    None,        // Text may overflow bounds (caller responsible for clipping)
    Clip,        // Cut off at boundary, no ellipsis
    Ellipsis,    // Replace overflow with "…"
};

enum class WrapMode {
    NoWrap,      // Single line
    WordWrap,    // Break on word boundaries
    CharWrap,    // Break anywhere (useful for hashes, long unbroken strings)
};

class Label : public Widget {
public:
    Label();
    explicit Label(std::string text);

    // Content
    void setText(std::string text);
    const std::string& text() const;

    // Appearance
    void setColor(Color c);              // override theme token
    void clearColor();                   // revert to theme token (default: textPrimary)
    void setTextColorToken(TextColorToken tok);   // textPrimary / textSecondary / textDim
    void setFontScale(float s);          // 1.0 = default theme font size
    void setFontWeight(FontWeight w);    // Regular / Bold (loaded fonts only)

    // Layout behavior
    void setAlign(TextAlign h, VerticalAlign v = VerticalAlign::Middle);
    void setTruncation(Truncation t);    // default Ellipsis
    void setWrap(WrapMode w);            // default NoWrap
    void setMaxLines(int n);             // 0 = unlimited; only used with wrap

    // Sizing hints
    void setMinWidth(float w);
    void setMaxWidth(float w);           // forces wrap at this width when wrapping
    void setMinHeight(float h);

    // Accessibility (reserved)
    void setAriaLabel(const std::string& label);
};
```

---

## Layout contract — the key piece

Label's measure() is the feature, not an afterthought.

### `onMeasure(Constraints c, UIContext& ctx) → Size`

Depends on wrap mode.

**No-wrap (default):**

```cpp
Size Label::onMeasure(Constraints c, UIContext& ctx) {
    float textW = ctx.font->textWidth(m_text, effectiveFontScale());
    float lineH = ctx.font->lineHeight(effectiveFontScale());

    float w = textW;
    if (m_minWidth > 0) w = std::max(w, m_minWidth);

    // Honor constraints. If text exceeds maxW, the widget takes maxW
    // (truncation will handle the overflow in paint).
    if (w > c.maxW) w = c.maxW;
    if (w < c.minW) w = c.minW;

    float h = std::max(lineH, m_minHeight);
    if (h > c.maxH) h = c.maxH;
    if (h < c.minH) h = c.minH;

    return { w, h };
}
```

**Wrapping:**

```cpp
// Width is driven by constraints (use maxW, or maxWidth if tighter).
float targetW = std::min(c.maxW, m_maxWidth > 0 ? m_maxWidth : c.maxW);

int lineCount = ctx.font->wrappedLineCount(m_text, targetW, effectiveFontScale(), m_wrapMode);
if (m_maxLines > 0) lineCount = std::min(lineCount, m_maxLines);

float lineH = ctx.font->lineHeight(effectiveFontScale());
float h = lineCount * lineH;
// Width: the widest wrapped line, clamped to targetW
float w = ctx.font->widestLine(m_text, targetW, ..., m_wrapMode);

return c.constrain({ w, h });
}
```

This requires new `Font` helpers: `wrappedLineCount()`,
`widestLine()`. These are cheap (single text-width sweep with
line-breaking).

### `onLayout(Rect b, UIContext& ctx)`

Store bounds. No children. Trivially cached.

### Size policy

```cpp
// No-wrap:  width = Fixed (content-driven),  height = Fixed
// Wrap:     width = Flex (fills available),  height = Fixed (driven by wrap result)
```

### Relayout boundary

- **No-wrap:** yes, automatically (both dimensions fixed).
- **Wrap:** no — height depends on constraints (different parent width
  produces different wrap). Cannot be a boundary because measure()
  results vary with the constraint passed in.

### Caching

Measure cache key: `(constraints, text, fontScale, wrapMode,
truncation, maxLines, minWidth, maxWidth, minHeight, font version)`.

The font version bumps when the font is reloaded (theme changes can
alter fontSize → different measurements). Theme-token changes that
don't affect font size are paint-only.

---

## Text measurement contract — what Font returns

For Label to do its job, `Font` needs to provide reliable:

```cpp
class Font {
    // Existing
    float textWidth(const std::string& text, float scale) const;
    float lineHeight(float scale) const;

    // New for Label:
    int   wrappedLineCount(const std::string& text,
                            float maxWidth, float scale,
                            WrapMode mode) const;
    float widestLine(const std::string& text,
                      float maxWidth, float scale,
                      WrapMode mode) const;
    std::string ellipsized(const std::string& text,
                            float maxWidth, float scale) const;
    // Returns text truncated to fit maxWidth, with "…" appended.
};
```

`ellipsized()` is consumed by the paint path when truncation is on and
the label's bounds are narrower than the full text.

These helpers belong in Font, not Label, because other widgets
(ContextMenu, DropDown, DeviceHeader) will want them too.

---

## Paint

```cpp
void Label::paint(UIContext& ctx) {
    if (m_text.empty() || m_bounds.w <= 0) return;

    Color color = m_colorOverride.has_value() ? *m_colorOverride
                                               : tokenColor(m_colorToken);
    if (!isEnabled()) color = color.desaturate(0.6f);

    float scale = effectiveFontScale();

    if (m_wrapMode != WrapMode::NoWrap) {
        // Paint each wrapped line
        auto lines = ctx.font->wrap(m_text, m_bounds.w, scale, m_wrapMode);
        if (m_maxLines > 0 && lines.size() > size_t(m_maxLines))
            lines.resize(m_maxLines);
        float y = verticalStartY(m_bounds, lines.size() * ctx.font->lineHeight(scale));
        for (auto& ln : lines) {
            float x = horizontalStartX(m_bounds, ctx.font->textWidth(ln, scale));
            ctx.font->drawText(*ctx.renderer, ln.c_str(), x, y, scale, color);
            y += ctx.font->lineHeight(scale);
        }
        return;
    }

    // Single line
    float textW = ctx.font->textWidth(m_text, scale);
    std::string toDraw = m_text;
    if (textW > m_bounds.w) {
        switch (m_truncation) {
            case Truncation::None:     break;  // overflow
            case Truncation::Clip:
                ctx.renderer->pushClip(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h);
                break;
            case Truncation::Ellipsis:
                toDraw = ctx.font->ellipsized(m_text, m_bounds.w, scale);
                textW = ctx.font->textWidth(toDraw, scale);
                break;
        }
    }
    float x = horizontalStartX(m_bounds, textW);
    float y = verticalStartY(m_bounds, ctx.font->lineHeight(scale));
    ctx.font->drawText(*ctx.renderer, toDraw.c_str(), x, y, scale, color);

    if (m_truncation == Truncation::Clip && textW > m_bounds.w)
        ctx.renderer->popClip();
}
```

Text positioning helpers respect `align()` / `verticalAlign()`.

---

## Theme tokens consumed

| Token | Use |
|---|---|
| `palette.textPrimary` | Default color |
| `palette.textSecondary` | Via `setTextColorToken(Secondary)` |
| `palette.textDim` | Via `setTextColorToken(Dim)` |
| `metrics.fontSize` | Base font size (multiplied by `fontScale`) |
| `metrics.fontSizeSmall` | Alternate base (for captions etc.) |

Caller can always override color via `setColor(Color)`. The token
path is for "style defaults that should follow the theme."

---

## Events fired

None.

---

## Invalidation triggers

### Measure-invalidating

- `setText`
- `setFontScale`
- `setFontWeight`
- `setWrap`, `setMaxLines`, `setMaxWidth`
- `setTruncation` (when Clip vs Ellipsis choice affects measured w —
  actually it doesn't, but `setTruncation` might as well invalidate
  to be safe and re-measure cheaply)
- `setMinWidth`, `setMinHeight`
- DPI / theme / font (global epoch)

### Paint-only invalidating

- `setColor`, `clearColor`
- `setTextColorToken`
- `setAlign`
- `setEnabled`

---

## Focus behavior

**Not focusable.** Label is passive text. Cannot receive keyboard
focus; Tab skips past it.

---

## Accessibility (reserved)

- Role: `text` (default) or `label` (when explicitly associated with
  another widget via `setLabelFor(Widget*)`).
- `setAriaLabel` — overrides the visible text for screen readers (use
  sparingly; prefer visible text).

---

## Animation

None. Labels don't animate. If a caller wants fade-in on appearance,
that's a parent-level animation, not a Label responsibility.

---

## Test surface

Unit tests in `tests/test_fw2_Label.cpp`:

1. `MeasureExactText` — `measure()` returns `(font.textWidth(text) ×
   font.lineHeight)` for short text that fits.
2. `MeasureRespectsMaxW` — text wider than constraints → width clamps
   to maxW.
3. `MeasureMinW` — `setMinWidth(100)` enforces 100 even for empty
   text.
4. `EllipsisTruncation` — text wider than bounds produces an
   ellipsized string via `Font::ellipsized`.
5. `ClipTruncation` — with `setTruncation(Clip)`, text overflows but
   paint path calls pushClip / popClip.
6. `WrapWordBoundary` — `setWrap(WordWrap)` breaks long text into
   multiple lines at word boundaries.
7. `WrapMaxLines` — `setMaxLines(2)` caps output to 2 lines regardless
   of text length.
8. `WrapChangesMeasure` — enabling wrap changes `measure()` output
   (height grows, width clamps).
9. `WrapIsNotRelayoutBoundary` — unlike no-wrap Label, a wrapping
   Label does NOT mark itself as a relayout boundary.
10. `CenterAlignment` — `setAlign(Center)` positions text at bounds
    center.
11. `BaselineAlignment` — text baseline matches configured offset.
12. `MeasureCacheHit` — re-measure with same inputs skips font calls.
13. `FontReloadInvalidates` — font reload bumps epoch, next
    measure() re-runs.
14. `DisabledDesaturates` — `setEnabled(false)` paints with
    desaturated color; measure unaffected.
15. `EmptyTextMeasuresToMin` — `setText("")` returns
    `(minWidth, lineHeight)` with no text-width contribution.

---

## Migration from v1

v1 `Label`:

```cpp
Label lbl;
lbl.setText("Volume");
lbl.setColor(Theme::textPrimary);
```

v1's `measure()` returns whatever constraints it receives rather than
actual content width. This is the root cause of most "labels overlap"
bugs — FlexBox thinks every Label wants the full container width, so
it treats them all as stretching. v2 returns real content width.

**Breaking change:** sibling widgets in a FlexBox may re-distribute
space differently after migration. Expected outcomes:

- Fixed-width FlexBox rows that previously had Labels stretching to
  fill space now leave unused space at the end. Use
  `FlexBox.setJustify(SpaceBetween)` or insert explicit spacers where
  needed.
- Labels that were truncating because of the size-fill bug may now
  not truncate — the Label asks for its full content width, and the
  FlexBox now allocates it.

Migration plan:

1. Ship v2 Label with new measure behavior.
2. Visually audit panels that use dense FlexBox rows (transport,
   mixer channel strips, menu items).
3. Fix layouts that visibly regressed, typically by adding a
   `FlexBox` container with explicit distribution policy.

New in v2:

- `setWrap(WrapMode)` — v1 had no wrap support.
- `setTruncation(Truncation)` — v1 always ellipsized.
- `setMaxLines(int)` — wrapping cap.
- `setAlign(TextAlign, VerticalAlign)` — v1 had fixed top-left.
- `setTextColorToken(...)` — v1 required `setColor(Theme::...)`
  directly. Tokens get automatic theme-swap support.

---

## Open questions

1. **Auto-tooltip when truncated?** Truncated labels hiding their
   full text is a common UX frustration. Option: automatically show
   a tooltip with the full text when the label is truncated and the
   user hovers. Behind a `setTruncationTooltip(bool)` flag, default
   true? Decision deferred to Tooltip spec.

2. **Selection?** If we add selectable text, does it stay in Label
   (optional `setSelectable(bool)`) or branch to `FwSelectableLabel`?
   Leaning separate class since selection brings state, clipboard
   integration, and painting overhead that passive Label shouldn't
   carry.

3. **Baseline alignment across siblings?** When multiple Labels sit
   in a FlexBox row with different font sizes, baseline alignment
   requires cross-widget negotiation. That's a FlexBox feature, not
   a Label one — Label just exposes baseline-y to the layout system.

4. **Right-to-left reading order?** Arabic / Hebrew project strings
   would need BiDi treatment. Deferred to a framework-wide
   internationalization pass.
