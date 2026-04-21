#pragma once

// UI v2 — Label.
//
// Passive text display. Critical design feature: measure() returns
// the REAL content width (via UIContext::font's textWidth) rather
// than passing through the constraint. That's the root fix for v1's
// "labels overlap in FlexBox" bug — siblings now get honest sizes.
//
// v2.0 ships single-line only; wrap/multi-line is flagged in the
// spec's open questions and deferred. Truncation modes (None / Clip /
// Ellipsis) are supported.

#include "Widget.h"
#include "Theme.h"

#include <optional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

enum class TextAlign {
    Start,
    Center,
    End,
};

enum class VerticalAlign {
    Top,
    Middle,
    Bottom,
};

enum class Truncation {
    None,       // text may overflow bounds — caller responsible for clipping
    Clip,       // text cut off at bounds edge
    Ellipsis,   // "…" replaces overflow
};

enum class TextColorToken {
    Primary,
    Secondary,
    Dim,
};

class Label : public Widget {
public:
    Label() = default;
    explicit Label(std::string text);

    // ─── Content ──────────────────────────────────────────────────
    void setText(std::string t);
    const std::string& text() const { return m_text; }

    // ─── Appearance ───────────────────────────────────────────────
    void setColor(Color c);             // overrides theme token
    void clearColor();                  // revert to theme token
    void setTextColorToken(TextColorToken tok);
    void setFontScale(float s);          // 1.0 = theme.metrics.fontSize

    // ─── Layout behavior ─────────────────────────────────────────
    void setAlign(TextAlign h, VerticalAlign v = VerticalAlign::Middle);
    void setTruncation(Truncation t);    // default Ellipsis

    // ─── Sizing hints ─────────────────────────────────────────────
    void setMinWidth(float w);
    void setMaxWidth(float w);           // forces truncation at this width
    void setMinHeight(float h);

protected:
    // ─── Widget overrides ────────────────────────────────────────
    Size onMeasure(Constraints c, UIContext& ctx) override;

    // Exposed for tests that want to verify the truncated string.
public:
    std::string effectiveText(float availableWidth, UIContext& ctx) const;

private:
    // Resolve text width via UIContext::font, with a fallback for
    // tests without a real font (returns text.length() * fallbackPxPerChar).
    float measureTextWidth(const std::string& s, UIContext& ctx) const;
    float measureLineHeight(UIContext& ctx) const;
    float effectiveFontSize(UIContext& ctx) const;

    // Content
    std::string m_text;

    // Appearance
    std::optional<Color> m_colorOverride;
    TextColorToken       m_colorToken = TextColorToken::Primary;
    float                m_fontScale  = 1.0f;

    // Layout behavior
    TextAlign     m_hAlign     = TextAlign::Start;
    VerticalAlign m_vAlign     = VerticalAlign::Middle;
    Truncation    m_truncation = Truncation::Ellipsis;

    // Sizing hints
    float m_minWidth  = 0.0f;
    float m_maxWidth  = 0.0f;   // 0 = unbounded
    float m_minHeight = 0.0f;

    // Tests-only: font fallback width. A real Font from UIContext
    // provides actual widths; without one we pretend each char is
    // 8 logical px. Predictable for unit tests.
    static constexpr float kFallbackPxPerChar = 8.0f;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
