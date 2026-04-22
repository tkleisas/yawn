// FwTextInputDialog.cpp — main-exe-only implementation (touches v1
// Renderer2D through UIContext::renderer, which pulls glad into the
// TU).

#include "TextInputDialog.h"

#include "ui/framework/v2/Theme.h"
#include "ui/framework/v2/LayerStack.h"

#include "ui/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <utility>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Lifetime
// ───────────────────────────────────────────────────────────────────

FwTextInputDialog::FwTextInputDialog() = default;

FwTextInputDialog::~FwTextInputDialog() {
    if (m_handle.active()) {
        m_dismissingInternally = true;
        m_handle.remove();
    }
}

// ───────────────────────────────────────────────────────────────────
// Public API
// ───────────────────────────────────────────────────────────────────

void FwTextInputDialog::prompt(std::string title,
                                 std::string defaultText,
                                 ConfirmCallback onConfirm,
                                 int maxLength) {
    // Replace any currently-open prompt silently.
    if (m_handle.active()) {
        m_dismissingInternally = true;
        m_handle.remove();
        m_dismissingInternally = false;
    }

    m_title     = std::move(title);
    m_onConfirm = std::move(onConfirm);
    m_maxLength = maxLength;
    m_confirmOnClose = false;

    if (m_maxLength > 0 && static_cast<int>(defaultText.size()) > m_maxLength) {
        defaultText.resize(m_maxLength);
    }
    m_input.setText(std::move(defaultText));
    m_input.setCursor(static_cast<int>(m_input.text().size()));
    m_input.beginEdit();

    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;

    OverlayEntry entry;
    entry.debugName             = "TextInputDialog";
    entry.bounds                = ctx.viewport;
    entry.modal                 = true;
    entry.dismissOnOutsideClick = false;
    entry.paint       = [this](UIContext& c)      { this->paintBody(c); };
    entry.onMouseDown = [this](MouseEvent& e)     { return this->onMouseDown(e); };
    entry.onMouseUp   = [this](MouseEvent& e)     { return this->onMouseUp(e);   };
    entry.onMouseMove = [this](MouseMoveEvent& e) { return this->onMouseMove(e); };
    entry.onKey       = [this](KeyEvent& e)       { return this->onKey(e); };
    entry.onDismiss   = [this]()                  { this->onDismiss(); };

    m_handle = ctx.layerStack->push(OverlayLayer::Modal, std::move(entry));
}

void FwTextInputDialog::dismiss() {
    if (!m_handle.active()) return;
    m_confirmOnClose = false;
    m_handle.remove();
}

void FwTextInputDialog::takeTextInput(const std::string& text) {
    if (!isOpen()) return;
    if (m_maxLength > 0) {
        const int remaining = m_maxLength - static_cast<int>(m_input.text().size());
        if (remaining <= 0) return;
        if (static_cast<int>(text.size()) > remaining) {
            m_input.takeTextInput(text.substr(0, remaining));
            return;
        }
    }
    m_input.takeTextInput(text);
}

void FwTextInputDialog::close(bool commit) {
    if (!m_handle.active()) return;
    m_confirmOnClose = commit;
    m_handle.remove();
}

void FwTextInputDialog::onDismiss() {
    m_handle.detach_noRemove();
    if (m_dismissingInternally) { m_dismissingInternally = false; return; }
    if (m_confirmOnClose && m_onConfirm && !m_input.text().empty()) {
        m_onConfirm(m_input.text());
    }
    m_confirmOnClose = false;
}

// ───────────────────────────────────────────────────────────────────
// Geometry
// ───────────────────────────────────────────────────────────────────

Rect FwTextInputDialog::bodyRect(const UIContext& ctx) const {
    const Rect& v = ctx.viewport;
    const float dx = v.x + (v.w - m_preferredW) * 0.5f;
    const float dy = v.y + (v.h - m_preferredH) * 0.5f;
    return Rect{dx, dy, m_preferredW, m_preferredH};
}

Rect FwTextInputDialog::inputRect(const Rect& body) const {
    constexpr float inputH = 30.0f;
    return Rect{body.x + 16.0f,
                 body.y + kTitleBarHeight + 8.0f,
                 body.w - 32.0f, inputH};
}

Rect FwTextInputDialog::okButtonRect(const Rect& body) const {
    constexpr float bw = 90.0f, bh = 32.0f;
    return Rect{body.x + body.w * 0.5f - bw - 8.0f,
                body.y + body.h - bh - 12.0f,
                bw, bh};
}

Rect FwTextInputDialog::cancelButtonRect(const Rect& body) const {
    constexpr float bw = 90.0f, bh = 32.0f;
    return Rect{body.x + body.w * 0.5f + 8.0f,
                body.y + body.h - bh - 12.0f,
                bw, bh};
}

// ───────────────────────────────────────────────────────────────────
// Paint
// ───────────────────────────────────────────────────────────────────

void FwTextInputDialog::paintBody(UIContext& ctx) {
    if (!ctx.renderer || !ctx.textMetrics) return;
    auto& r  = *ctx.renderer;
    auto& tm = *ctx.textMetrics;

    m_body = bodyRect(ctx);
    const float dx = m_body.x, dy = m_body.y;
    const float dw = m_body.w, dh = m_body.h;

    // Body + border.
    r.drawRect(dx, dy, dw, dh, Color{45, 45, 52, 255});
    r.drawRectOutline(dx, dy, dw, dh, Color{75, 75, 85, 255});

    // Title.
    const ThemeMetrics& m = theme().metrics;
    const float titleSize = m.fontSize;
    tm.drawText(r, m_title, dx + 16.0f,
                dy + (kTitleBarHeight - tm.lineHeight(titleSize)) * 0.5f,
                titleSize, ::yawn::ui::Theme::textPrimary);

    // Input field — lay out + render through the widget.
    const Rect ir = inputRect(m_body);
    m_input.measure(Constraints::tight(ir.w, ir.h), ctx);
    m_input.layout(ir, ctx);
    m_input.render(ctx);

    // OK / Cancel buttons.
    const float btnSize = m.fontSize;
    const Rect okR = okButtonRect(m_body);
    r.drawRect(okR.x, okR.y, okR.w, okR.h, Color{50, 130, 50, 255});
    tm.drawText(r, "OK",
                okR.x + (okR.w - tm.textWidth("OK", btnSize)) * 0.5f,
                okR.y + (okR.h - tm.lineHeight(btnSize)) * 0.5f,
                btnSize, ::yawn::ui::Theme::textPrimary);

    const Rect cancelR = cancelButtonRect(m_body);
    r.drawRect(cancelR.x, cancelR.y, cancelR.w, cancelR.h, Color{130, 50, 50, 255});
    tm.drawText(r, "Cancel",
                cancelR.x + (cancelR.w - tm.textWidth("Cancel", btnSize)) * 0.5f,
                cancelR.y + (cancelR.h - tm.lineHeight(btnSize)) * 0.5f,
                btnSize, ::yawn::ui::Theme::textPrimary);
}

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

void FwTextInputDialog::forwardMouse(Widget* w, MouseEvent& e,
                                       bool (Widget::*fn)(MouseEvent&)) {
    if (!w) return;
    const Rect& b = w->bounds();
    e.lx = e.x - b.x;
    e.ly = e.y - b.y;
    (w->*fn)(e);
}

void FwTextInputDialog::forwardMouseMove(Widget* w, MouseMoveEvent& e) {
    if (!w) return;
    const Rect& b = w->bounds();
    e.lx = e.x - b.x;
    e.ly = e.y - b.y;
    w->dispatchMouseMove(e);
}

bool FwTextInputDialog::onMouseDown(MouseEvent& e) {
    const float mx = e.x, my = e.y;

    // Outside body → cancel.
    if (mx < m_body.x || mx > m_body.x + m_body.w ||
        my < m_body.y || my > m_body.y + m_body.h) {
        close(false);
        return true;
    }

    // Buttons.
    const Rect okR = okButtonRect(m_body);
    if (mx >= okR.x && mx <= okR.x + okR.w && my >= okR.y && my <= okR.y + okR.h) {
        close(true);
        return true;
    }
    const Rect cancelR = cancelButtonRect(m_body);
    if (mx >= cancelR.x && mx <= cancelR.x + cancelR.w &&
        my >= cancelR.y && my <= cancelR.y + cancelR.h) {
        close(false);
        return true;
    }

    // Forward to the text input.
    if (m_input.hitTestGlobal(mx, my)) {
        forwardMouse(&m_input, e, &Widget::dispatchMouseDown);
    }
    return true;
}

bool FwTextInputDialog::onMouseUp(MouseEvent& e) {
    Widget* cap = Widget::capturedWidget();
    if (cap) forwardMouse(cap, e, &Widget::dispatchMouseUp);
    return true;
}

bool FwTextInputDialog::onMouseMove(MouseMoveEvent& e) {
    Widget* cap = Widget::capturedWidget();
    if (cap) { forwardMouseMove(cap, e); return true; }
    MouseMoveEvent copy = e;
    forwardMouseMove(&m_input, copy);
    return true;
}

bool FwTextInputDialog::onKey(KeyEvent& e) {
    if (e.consumed) return false;
    if (e.key == Key::Escape) { close(false); return true; }
    if (e.key == Key::Enter)  { close(true);  return true; }
    // Forward everything else to the text input so Backspace / Delete
    // / arrow keys edit the field.
    m_input.dispatchKeyDown(e);
    return true;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
