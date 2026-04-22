#pragma once

// UI v2 — ScrollView.
//
// Scrollable viewport over a single content child. Handles the common
// "content larger than bounds" case — wheel, keyboard, and scrollbar
// drag input translate to a 2D scroll offset; the content is clipped
// to the viewport and drawn offset by -scrollOffset.
//
// This first-pass implementation ships the spec's core contract:
//   • setContent(Widget*) — one content child (usually a FlexBox
//     or FwGrid containing the actual children)
//   • Wheel + keyboard scrolling
//   • Auto / Always / Never / Scroll overflow policies per axis
//   • Self-painted scrollbar indicators (no embedded FwScrollBar;
//     visible-only)
//   • onScroll callback
//
// Deferred to later revisions:
//   • Drag-to-scroll (middle button pan)
//   • Embedded FwScrollBar children (when FwScrollBar gains vertical
//     orientation)
//   • Smooth scroll animation
//   • Touch momentum, scrollRectIntoView, focus-follow
//
// See docs/widgets/scroll_view.md for the full spec.

#include "Widget.h"
#include "Theme.h"

#include <functional>
#include <optional>

namespace yawn {
namespace ui {
namespace fw2 {

enum class ScrollOverflow {
    Auto,      // bar shown only when content overflows (default)
    Always,    // bar always visible (reserves space)
    Never,     // bar never painted; wheel / keys still scroll
    Scroll,    // bar always visible AND axis always considered scrollable
};

class ScrollView : public Widget {
public:
    using ScrollCallback = std::function<void(Point)>;

    ScrollView();
    ~ScrollView() override;

    // ─── Content ──────────────────────────────────────────────────
    // Replaces any previous content. ScrollView does NOT take
    // ownership — the caller keeps the content alive.
    void    setContent(Widget* content);
    Widget* content() const { return m_content; }

    // ─── Scroll offset ────────────────────────────────────────────
    void  setScrollOffset(Point offset,
                           ValueChangeSource src = ValueChangeSource::Programmatic);
    Point scrollOffset()    const { return {m_scrollX, m_scrollY}; }
    Point maxScrollOffset() const;

    void scrollTo(Point offset)   { setScrollOffset(offset); }
    void scrollBy(Point delta);
    void scrollToTop();
    void scrollToBottom();

    // Content size after the most recent layout pass.
    Size  contentSize()  const { return {m_contentSize.w, m_contentSize.h}; }
    Size  viewportSize() const;   // bounds minus reserved scrollbar thickness

    // ─── Overflow policy ──────────────────────────────────────────
    void setHorizontalOverflow(ScrollOverflow o);
    void setVerticalOverflow(ScrollOverflow o);
    ScrollOverflow horizontalOverflow() const { return m_hOverflow; }
    ScrollOverflow verticalOverflow()   const { return m_vOverflow; }

    // ─── Scrolling behavior ──────────────────────────────────────
    void  setScrollWheelMultiplier(float px)  { m_wheelMultiplier = px; }
    float scrollWheelMultiplier() const       { return m_wheelMultiplier; }

    void  setArrowStep(float px)              { m_arrowStep = px; }
    void  setPageStep(float px)               { m_pageStep  = px; }
    float arrowStep() const                   { return m_arrowStep; }
    float pageStep()  const                   { return m_pageStep; }

    // ─── Appearance ───────────────────────────────────────────────
    void setBackgroundColor(std::optional<Color> c) { m_bgColor = c; }
    const std::optional<Color>& backgroundColor() const { return m_bgColor; }

    void  setScrollbarThickness(float px);
    float scrollbarThickness() const;

    // ─── Callbacks ────────────────────────────────────────────────
    void setOnScroll(ScrollCallback cb) { m_onScroll = std::move(cb); }

    // ─── Paint-side accessors (used by the painter) ──────────────
    // Whether a visible scrollbar should be drawn on each axis, given
    // the most-recent layout's content + viewport sizes.
    bool showHorizontalBar() const;
    bool showVerticalBar()   const;

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

    // ScrollView paints its own background + scrollbars AROUND the
    // clipped content. We override render() to insert the viewport
    // clip push/pop between paint and child recursion.
public:
    void render(UIContext& ctx) override;

protected:
    bool onScroll(ScrollEvent& e) override;
    bool onKeyDown(KeyEvent& e)   override;

private:
    void clampScroll();
    void fireOnScroll(ValueChangeSource src);

    // State
    Widget* m_content       = nullptr;
    float   m_scrollX       = 0.0f;
    float   m_scrollY       = 0.0f;
    Size    m_contentSize{0.0f, 0.0f};

    // Policy / config
    ScrollOverflow m_hOverflow       = ScrollOverflow::Auto;
    ScrollOverflow m_vOverflow       = ScrollOverflow::Auto;
    float          m_wheelMultiplier = 40.0f;
    float          m_arrowStep       = 40.0f;
    float          m_pageStep        = 0.0f;   // 0 = viewport height - 40

    // Appearance
    std::optional<Color> m_bgColor;
    float                m_scrollbarThickness = 0.0f;   // 0 = theme default

    // Callbacks
    ScrollCallback m_onScroll;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
