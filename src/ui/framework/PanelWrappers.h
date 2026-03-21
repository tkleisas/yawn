#pragma once
// Panel wrapper widgets — thin Widget subclasses that delegate to existing
// panel classes for rendering and events.  This allows the old panels to
// participate in the framework's measure/layout/render cycle without being
// rewritten.  Only included from App.cpp (never in test builds).

#include "Widget.h"
#include "FlexBox.h"
#include "../SessionView.h"
#include "../MixerView.h"
#include "../DetailPanel.h"
#include "../PianoRoll.h"
#include "../MenuBar.h"

namespace yawn {
namespace ui {
namespace fw {

// ─── MenuBarWrapper ──────────────────────────────────────────────────────
// Layout-only wrapper.  paint() is a no-op because the MenuBar must be
// rendered last (on top of other panels) so its dropdown menus aren't
// occluded.  App::render() calls MenuBar::render() manually after the tree.

class MenuBarWrapper : public Widget {
public:
    explicit MenuBarWrapper(MenuBar& bar) : m_bar(bar) {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, m_bar.height()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext&) override { /* rendered manually by App */ }

private:
    MenuBar& m_bar;
};

// ─── SessionViewWrapper ─────────────────────────────────────────────────

class SessionViewWrapper : public Widget {
public:
    explicit SessionViewWrapper(SessionView& view) : m_view(view) {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, m_view.preferredHeight()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext& ctx) override {
        m_view.render(*ctx.renderer, *ctx.font,
                      m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h);
    }

private:
    SessionView& m_view;
};

// ─── MixerViewWrapper ──────────────────────────────────────────────────

class MixerViewWrapper : public Widget {
public:
    explicit MixerViewWrapper(MixerView& view) : m_view(view) {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, m_view.preferredHeight()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext& ctx) override {
        m_view.render(*ctx.renderer, *ctx.font,
                      m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h);
    }

private:
    MixerView& m_view;
};

// ─── DetailPanelWrapper ────────────────────────────────────────────────

class DetailPanelWrapper : public Widget {
public:
    explicit DetailPanelWrapper(DetailPanel& panel) : m_panel(panel) {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, m_panel.height()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext& ctx) override {
        m_panel.render(*ctx.renderer, *ctx.font,
                       m_bounds.x, m_bounds.y, m_bounds.w);
    }

private:
    DetailPanel& m_panel;
};

// ─── PianoRollWrapper ──────────────────────────────────────────────────

class PianoRollWrapper : public Widget {
public:
    explicit PianoRollWrapper(PianoRoll& roll) : m_roll(roll) {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain({c.maxW, m_roll.height()});
    }

    void layout(const Rect& bounds, const UIContext&) override {
        m_bounds = bounds;
    }

    void paint(UIContext& ctx) override {
        m_roll.render(*ctx.renderer, *ctx.font,
                      m_bounds.x, m_bounds.y, m_bounds.w);
    }

private:
    PianoRoll& m_roll;
};

} // namespace fw
} // namespace ui
} // namespace yawn
