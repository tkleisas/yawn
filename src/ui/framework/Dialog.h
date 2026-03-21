#pragma once
// Dialog — Base class for modal and non-modal dialog windows.
//
// Provides title bar with close button, optional OK/Cancel footer,
// draggable positioning, and modal backdrop support via Overlay.

#include "Widget.h"
#include "FlexBox.h"
#include <string>
#include <functional>

namespace yawn {
namespace ui {
namespace fw {

enum class DialogResult { None, OK, Cancel, Custom };

class Dialog : public Widget {
public:
    using ResultCallback = std::function<void(DialogResult)>;

    explicit Dialog(const std::string& title, float width = 400, float height = 300)
        : m_title(title), m_preferredW(width), m_preferredH(height) {}

    // ─── Configuration ──────────────────────────────────────────────────

    void setTitle(const std::string& t) { m_title = t; }
    const std::string& title() const { return m_title; }

    void setShowCloseButton(bool show) { m_showClose = true; }
    void setShowOKCancel(bool show) { m_showOKCancel = show; }
    void setOKLabel(const std::string& label) { m_okLabel = label; }
    void setCancelLabel(const std::string& label) { m_cancelLabel = label; }
    void setResizable(bool r) { m_resizable = r; }
    void setMinSize(float w, float h) { m_minW = w; m_minH = h; }
    void setMaxSize(float w, float h) { m_maxW = w; m_maxH = h; }

    void setOnResult(ResultCallback cb) { m_onResult = std::move(cb); }

    // ─── Actions ────────────────────────────────────────────────────────

    void close(DialogResult result = DialogResult::Cancel) {
        m_result = result;
        if (m_onResult) m_onResult(m_result);
        m_visible = false;
    }

    DialogResult result() const { return m_result; }

    // ─── Content ────────────────────────────────────────────────────────

    void setContent(Widget* content) {
        m_content = content;
        if (content && content->parent() != this) {
            addChild(content);
        }
    }
    Widget* content() const { return m_content; }

    // ─── Layout ─────────────────────────────────────────────────────────

    static constexpr float kTitleBarHeight = 32.0f;
    static constexpr float kFooterHeight = 40.0f;
    static constexpr float kCloseButtonSize = 24.0f;
    static constexpr float kPadding = 8.0f;

    Size measure(const Constraints& constraints, const UIContext& ctx) override {
        float w = m_preferredW;
        float h = m_preferredH;

        if (m_content) {
            float contentMaxW = w - kPadding * 2;
            float contentMaxH = h - kTitleBarHeight - kPadding * 2;
            if (m_showOKCancel) contentMaxH -= kFooterHeight;
            Size cs = m_content->measure(
                Constraints::tight(contentMaxW, contentMaxH), ctx);
            w = detail::cmax(w, cs.w + kPadding * 2);
            h = detail::cmax(h, cs.h + kTitleBarHeight + kPadding * 2 +
                              (m_showOKCancel ? kFooterHeight : 0));
        }

        w = detail::cclamp(w, m_minW, m_maxW);
        h = detail::cclamp(h, m_minH, m_maxH);
        return constraints.constrain({w, h});
    }

    void layout(const Rect& bounds, const UIContext& ctx) override {
        m_bounds = bounds;
        if (m_content) {
            float footerH = m_showOKCancel ? kFooterHeight : 0;
            Rect contentBounds{
                bounds.x + kPadding,
                bounds.y + kTitleBarHeight,
                bounds.w - kPadding * 2,
                bounds.h - kTitleBarHeight - footerH - kPadding
            };
            m_content->layout(contentBounds, ctx);
        }
    }

    // ─── Geometry helpers ───────────────────────────────────────────────

    Rect titleBarRect() const {
        return {m_bounds.x, m_bounds.y, m_bounds.w, kTitleBarHeight};
    }

    Rect closeButtonRect() const {
        return {m_bounds.x + m_bounds.w - kCloseButtonSize - 4,
                m_bounds.y + (kTitleBarHeight - kCloseButtonSize) * 0.5f,
                kCloseButtonSize, kCloseButtonSize};
    }

    Rect contentRect() const {
        float footerH = m_showOKCancel ? kFooterHeight : 0;
        return {m_bounds.x + kPadding, m_bounds.y + kTitleBarHeight,
                m_bounds.w - kPadding * 2,
                m_bounds.h - kTitleBarHeight - footerH - kPadding};
    }

    Rect footerRect() const {
        return {m_bounds.x, m_bounds.y + m_bounds.h - kFooterHeight,
                m_bounds.w, kFooterHeight};
    }

    Rect okButtonRect() const {
        float bw = 80, bh = 28;
        return {m_bounds.x + m_bounds.w - kPadding - bw,
                m_bounds.y + m_bounds.h - kFooterHeight + (kFooterHeight - bh) * 0.5f,
                bw, bh};
    }

    Rect cancelButtonRect() const {
        float bw = 80, bh = 28;
        return {m_bounds.x + m_bounds.w - kPadding - bw - kPadding - bw,
                m_bounds.y + m_bounds.h - kFooterHeight + (kFooterHeight - bh) * 0.5f,
                bw, bh};
    }

    // ─── Events ─────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        Point local = toLocal(e.x, e.y);

        // Close button
        if (m_showClose) {
            Rect cb = closeButtonRect();
            Rect localCB{cb.x - m_bounds.x, cb.y - m_bounds.y, cb.w, cb.h};
            if (localCB.contains(local.x, local.y)) {
                close(DialogResult::Cancel);
                e.consume();
                return true;
            }
        }

        // OK button
        if (m_showOKCancel) {
            Rect ob = okButtonRect();
            Rect localOB{ob.x - m_bounds.x, ob.y - m_bounds.y, ob.w, ob.h};
            if (localOB.contains(local.x, local.y)) {
                close(DialogResult::OK);
                e.consume();
                return true;
            }

            Rect cb = cancelButtonRect();
            Rect localCB{cb.x - m_bounds.x, cb.y - m_bounds.y, cb.w, cb.h};
            if (localCB.contains(local.x, local.y)) {
                close(DialogResult::Cancel);
                e.consume();
                return true;
            }
        }

        // Title bar drag
        Rect tb = titleBarRect();
        Rect localTB{0, 0, tb.w, tb.h};
        if (localTB.contains(local.x, local.y)) {
            m_dragging = true;
            m_dragStartX = e.x;
            m_dragStartY = e.y;
            e.consume();
            return true;
        }

        return false;
    }

    bool onMouseUp(MouseEvent&) override {
        m_dragging = false;
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_dragging) {
            float dx = e.x - m_dragStartX;
            float dy = e.y - m_dragStartY;
            m_bounds.x += dx;
            m_bounds.y += dy;
            m_dragStartX = e.x;
            m_dragStartY = e.y;
            return true;
        }
        return false;
    }

    bool onKeyDown(KeyEvent& e) override {
        if (e.keyCode == 27) { // Escape
            close(DialogResult::Cancel);
            return true;
        }
        if (e.keyCode == 13 && m_showOKCancel) { // Enter → OK
            close(DialogResult::OK);
            return true;
        }
        return false;
    }

protected:
    std::string m_title;
    float m_preferredW, m_preferredH;
    float m_minW = 200, m_minH = 150;
    float m_maxW = 2000, m_maxH = 1500;

    bool m_showClose = true;
    bool m_showOKCancel = false;
    std::string m_okLabel = "OK";
    std::string m_cancelLabel = "Cancel";
    bool m_resizable = false;

    Widget* m_content = nullptr;
    DialogResult m_result = DialogResult::None;
    ResultCallback m_onResult;

    bool m_dragging = false;
    float m_dragStartX = 0, m_dragStartY = 0;
};

} // namespace fw
} // namespace ui
} // namespace yawn
