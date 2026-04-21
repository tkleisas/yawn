#include "Label.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

Label::Label(std::string text) : m_text(std::move(text)) {
    // Labels default to non-focusable + fixed-size (flexWeight=0),
    // so auto-detect makes them a relayout boundary. Text changes
    // invalidate, which is why setters below call invalidate().
    setFocusable(false);
}

void Label::setText(std::string t) {
    if (t != m_text) {
        m_text = std::move(t);
        invalidate();   // content-driven width may change
    }
}

void Label::setColor(Color c) {
    m_colorOverride = c;
    // paint-only
}

void Label::clearColor() {
    m_colorOverride.reset();
    // paint-only
}

void Label::setTextColorToken(TextColorToken tok) {
    m_colorToken = tok;
    // paint-only
}

void Label::setFontScale(float s) {
    if (s != m_fontScale) {
        m_fontScale = s;
        invalidate();   // font size affects measurement
    }
}

void Label::setAlign(TextAlign h, VerticalAlign v) {
    m_hAlign = h;
    m_vAlign = v;
    // paint-only — alignment doesn't change measured size
}

void Label::setTruncation(Truncation t) {
    if (t != m_truncation) {
        m_truncation = t;
        invalidate();   // truncation can cap width
    }
}

void Label::setMinWidth(float w) {
    if (w != m_minWidth) { m_minWidth = w; invalidate(); }
}
void Label::setMaxWidth(float w) {
    if (w != m_maxWidth) { m_maxWidth = w; invalidate(); }
}
void Label::setMinHeight(float h) {
    if (h != m_minHeight) { m_minHeight = h; invalidate(); }
}

// ─── Measure ─────────────────────────────────────────────────────────

Size Label::onMeasure(Constraints c, UIContext& ctx) {
    float textW = measureTextWidth(m_text, ctx);
    float lineH = measureLineHeight(ctx);

    // Apply minWidth / maxWidth sizing hints.
    float w = textW;
    if (m_maxWidth > 0.0f) w = std::min(w, m_maxWidth);
    if (m_minWidth > 0.0f) w = std::max(w, m_minWidth);

    // Constraint clamping: text wider than maxW means we'll truncate
    // in paint. Width reported matches constraint so siblings can pack
    // other content in the remaining space.
    w = std::min(w, c.maxW);
    w = std::max(w, c.minW);

    float h = std::max(lineH, m_minHeight);
    h = std::min(h, c.maxH);
    h = std::max(h, c.minH);

    return {w, h};
}

// ─── Helpers ────────────────────────────────────────────────────────

namespace {
// Count UTF-8 codepoints in a string. Each codepoint is signaled by a
// lead byte (any byte that is NOT 10xxxxxx continuation). Matches how
// a real Font would measure by visual characters — the fallback path
// should feel comparable.
int countCodepoints(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}
} // anon

float Label::measureTextWidth(const std::string& s, UIContext& ctx) const {
    if (s.empty()) return 0.0f;
    if (ctx.textMetrics) {
        return ctx.textMetrics->textWidth(s, effectiveFontSize(ctx));
    }
    // Fallback: 8 px per visual character (UTF-8 codepoints). Makes
    // multi-byte glyphs like "…" count as one char — matching how
    // production Font measures visually.
    return static_cast<float>(countCodepoints(s)) * kFallbackPxPerChar;
}

float Label::measureLineHeight(UIContext& ctx) const {
    if (ctx.textMetrics) {
        return ctx.textMetrics->lineHeight(effectiveFontSize(ctx));
    }
    return effectiveFontSize(ctx) * 1.2f;
}

float Label::effectiveFontSize(UIContext& /*ctx*/) const {
    return theme().metrics.fontSize * m_fontScale;
}

// ─── Effective text (truncated / clipped) ────────────────────────────

std::string Label::effectiveText(float availableWidth, UIContext& ctx) const {
    if (m_text.empty() || availableWidth <= 0.0f) return m_text;

    const float full = measureTextWidth(m_text, ctx);
    if (full <= availableWidth) return m_text;

    if (m_truncation == Truncation::None) return m_text;
    if (m_truncation == Truncation::Clip) return m_text;  // paint clips at widget bounds

    // Ellipsis: find longest prefix whose width + "…" fits.
    const std::string ellipsis = "\xE2\x80\xA6";   // UTF-8 for U+2026 horizontal ellipsis
    const float ellW = measureTextWidth(ellipsis, ctx);
    if (ellW >= availableWidth) return ellipsis;    // degenerate

    // Binary search over prefix length (in bytes — OK for ASCII; for
    // UTF-8 multi-byte content this may cut mid-codepoint. A Font-
    // aware truncator would respect codepoint boundaries. v2.0
    // ships the simple version; Font::ellipsized() helper flagged in
    // the spec's open questions.)
    int lo = 0;
    int hi = static_cast<int>(m_text.size());
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        const std::string candidate = m_text.substr(0, mid) + ellipsis;
        if (measureTextWidth(candidate, ctx) <= availableWidth) lo = mid;
        else                                                     hi = mid - 1;
    }
    return m_text.substr(0, lo) + ellipsis;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
