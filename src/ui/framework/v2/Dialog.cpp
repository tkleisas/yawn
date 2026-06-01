#include "Dialog.h"

#include "Theme.h"
#include "UIContext.h"

#include <algorithm>
#include <optional>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// Painter hook + text measurement helpers
// ───────────────────────────────────────────────────────────────────

namespace {
DialogManager::PaintFn& painterSlot() {
    static DialogManager::PaintFn fn = nullptr;
    return fn;
}
constexpr float kFallbackPxPerChar = 8.0f;

int utf8CodepointCount(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    }
    return n;
}

float measureTextW(const std::string& s, UIContext& ctx, float fontSize) {
    if (s.empty()) return 0.0f;
    if (ctx.textMetrics) return ctx.textMetrics->textWidth(s, fontSize);
    return static_cast<float>(utf8CodepointCount(s)) * kFallbackPxPerChar;
}

float measureLineH(UIContext& ctx, float fontSize) {
    if (ctx.textMetrics) return ctx.textMetrics->lineHeight(fontSize);
    return fontSize * 1.2f;
}

// Split on '\n' — keeps empty trailing lines so "a\n\nb" renders with a
// blank row between a and b.
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(std::move(cur)); cur.clear(); }
        else           { cur.push_back(c); }
    }
    out.push_back(std::move(cur));
    return out;
}

// Pending close reason — set by dismissWith() right before the
// OverlayHandle::remove() that will fire our entry.onDismiss. Left
// nullopt to signal "dismiss came from LayerStack itself (scrim
// click), so reason = OutsideClick". Module-local static keeps the
// state off the manager, which gets zeroed inside onDismiss.
std::optional<DialogCloseReason>& pendingReasonSlot() {
    static std::optional<DialogCloseReason> r;
    return r;
}
} // anon

void DialogManager::setPainter(PaintFn fn) { painterSlot() = fn; }
DialogManager::PaintFn DialogManager::painter() { return painterSlot(); }

// ───────────────────────────────────────────────────────────────────
// Singleton
// ───────────────────────────────────────────────────────────────────

DialogManager& DialogManager::instance() {
    static DialogManager g;
    return g;
}

void DialogManager::_testResetAll() {
    // Wipe the handle + state without firing onClose (test fixture —
    // don't let cross-test callbacks touch disposed harnesses).
    if (m_handle.active()) {
        // Detach first so the entry's onDismiss (if it runs during
        // LayerStack teardown) doesn't re-enter.
        m_handle.detach_noRemove();
    }
    m_state = {};
}

// ───────────────────────────────────────────────────────────────────
// Lifecycle
// ───────────────────────────────────────────────────────────────────

void DialogManager::show(DialogSpec spec) {
    // Replace any currently-open dialog — fire its onClose first.
    if (m_handle.active()) dismissWith(DialogCloseReason::Dismiss);

    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;   // nothing to push into

    m_state = {};
    m_state.spec = std::move(spec);
    layoutBody(ctx);
    pushEntry(ctx);
}

void DialogManager::close() {
    if (!m_handle.active()) return;
    dismissWith(DialogCloseReason::Dismiss);
}

void DialogManager::dismissWith(DialogCloseReason r) {
    pendingReasonSlot() = r;
    m_handle.remove();   // fires entry.onDismiss (see pushEntry)
}

// ───────────────────────────────────────────────────────────────────
// Layout
// ───────────────────────────────────────────────────────────────────

void DialogManager::layoutBody(UIContext& ctx) {
    const ThemeMetrics& m = theme().metrics;
    const float fontSize  = m.fontSize;
    const float titleSize = m.fontSizeLarge;
    const float padX      = m.baseUnit * 4.0f;   // 16 px side padding
    const float padY      = m.baseUnit * 3.0f;   // 12 px top/bot padding
    const float rowGap    = m.baseUnit * 2.0f;   // between title/message/buttons
    const float btnGap    = m.baseUnit * 2.0f;   // between buttons
    const float btnPadX   = m.baseUnit * 3.0f;   // per-button horizontal
    const float btnH      = m.controlHeight;
    const float lineH     = measureLineH(ctx, fontSize);

    // Auto-sized width: max of title (at large size), longest message
    // line, and the combined button-row width — plus side padding.
    float contentW = 0.0f;
    if (!m_state.spec.title.empty())
        contentW = std::max(contentW, measureTextW(m_state.spec.title, ctx, titleSize));
    const auto lines = splitLines(m_state.spec.message);
    for (const auto& l : lines)
        contentW = std::max(contentW, measureTextW(l, ctx, fontSize));

    float buttonsRowW = 0.0f;
    for (size_t i = 0; i < m_state.spec.buttons.size(); ++i) {
        if (i) buttonsRowW += btnGap;
        buttonsRowW += measureTextW(m_state.spec.buttons[i].label, ctx, fontSize)
                        + btnPadX * 2.0f;
    }
    contentW = std::max(contentW, buttonsRowW);

    float w = (m_state.spec.width > 0.0f)
              ? m_state.spec.width
              : contentW + padX * 2.0f;
    // Sensible minimum so ConfirmDialog doesn't feel pinched.
    w = std::max(w, 360.0f);

    // Height: accumulate icon + header + message + button row + padding.
    float contentH = padY;
    if (m_state.spec.iconTextureId != 0 && m_state.spec.iconSize > 0.0f) {
        contentH += m_state.spec.iconSize;
        contentH += rowGap;
        // Also widen the dialog so the icon isn't squeezed.
        const float wantW = m_state.spec.iconSize + padX * 2.0f;
        if (m_state.spec.width <= 0.0f && wantW > w) w = wantW;
    }
    if (!m_state.spec.title.empty()) {
        contentH += measureLineH(ctx, titleSize);
        contentH += rowGap;
    }
    if (!lines.empty()) {
        contentH += lines.size() * lineH;
        contentH += rowGap;
    }
    if (!m_state.spec.buttons.empty()) {
        contentH += btnH;
    }
    contentH += padY;
    float h = (m_state.spec.height > 0.0f) ? m_state.spec.height : contentH;
    h = std::max(h, 120.0f);

    // Centre in viewport, clamped so the dialog never exceeds the screen.
    // A too-tall dialog keeps its title + buttons pinned and scrolls the
    // message region (see msgRect below); a too-wide dialog clips to body.
    const Rect& v = ctx.viewport;
    h = std::min(h, v.h * 0.92f);
    w = std::min(w, v.w * 0.96f);
    m_state.bounds = {
        v.x + (v.w - w) * 0.5f,
        v.y + (v.h - h) * 0.5f,
        w, h
    };

    // Compute button rects — right-aligned along the bottom row.
    m_state.buttonRects.clear();
    const float rowY = m_state.bounds.y + m_state.bounds.h - padY - btnH;
    float x = m_state.bounds.x + m_state.bounds.w - padX;
    // Walk right-to-left so the last-declared button sits furthest right
    // (typical OK / Cancel convention: OK rightmost). Record the
    // resulting rects in declaration order by reversing later.
    std::vector<Rect> revRects;
    revRects.reserve(m_state.spec.buttons.size());
    for (int i = static_cast<int>(m_state.spec.buttons.size()) - 1; i >= 0; --i) {
        const float bw = measureTextW(m_state.spec.buttons[i].label, ctx, fontSize)
                          + btnPadX * 2.0f;
        x -= bw;
        revRects.push_back(Rect{x, rowY, bw, btnH});
        x -= btnGap;
    }
    m_state.buttonRects.resize(revRects.size());
    for (size_t i = 0; i < revRects.size(); ++i) {
        m_state.buttonRects[revRects.size() - 1 - i] = revRects[i];
    }

    // Message scroll region — between the title (top) and the button
    // row (bottom). When the dialog was clamped to the viewport and the
    // message is taller than this region, it scrolls (title + buttons
    // stay pinned). The painter clips to this rect and offsets by
    // scrollOffset; onScroll/onKey adjust it.
    float msgTop = m_state.bounds.y + padY;
    if (m_state.spec.iconTextureId != 0 && m_state.spec.iconSize > 0.0f)
        msgTop += m_state.spec.iconSize + rowGap;
    if (!m_state.spec.title.empty())
        msgTop += measureLineH(ctx, titleSize) + rowGap;
    const float msgBottom = m_state.spec.buttons.empty()
                                ? (m_state.bounds.y + m_state.bounds.h - padY)
                                : (rowY - rowGap);
    m_state.msgRect = { m_state.bounds.x, msgTop,
                        m_state.bounds.w, std::max(0.0f, msgBottom - msgTop) };
    m_state.msgContentH = static_cast<float>(lines.size()) * lineH;
    clampScroll();
}

void DialogManager::clampScroll() {
    const float maxS = m_state.scrollMax();
    if (m_state.scrollOffset < 0.0f)  m_state.scrollOffset = 0.0f;
    if (m_state.scrollOffset > maxS)  m_state.scrollOffset = maxS;
}

// ───────────────────────────────────────────────────────────────────
// Overlay entry
// ───────────────────────────────────────────────────────────────────

void DialogManager::pushEntry(UIContext& ctx) {
    OverlayEntry entry;
    entry.debugName             = "Dialog";
    entry.bounds                = m_state.bounds;   // body only
    entry.modal                 = true;             // scrim blocks Main layer
    // Let LayerStack auto-dismiss on outside-click when the caller
    // opted in. When that path fires, pendingReasonSlot() is nullopt
    // (because we didn't set it ourselves), and our onDismiss infers
    // OutsideClick — see below.
    entry.dismissOnOutsideClick = m_state.spec.dismissOnScrimClick;

    entry.customHitTest = [this](float sx, float sy) {
        return m_state.bounds.contains(sx, sy);
    };
    entry.paint       = [this](UIContext& c) {
        if (PaintFn fn = painterSlot()) fn(m_state, c);
    };
    entry.onMouseDown = [this](MouseEvent& e)     { return onMouseDown(e); };
    entry.onMouseUp   = [this](MouseEvent& e)     { return onMouseUp(e);   };
    entry.onMouseMove = [this](MouseMoveEvent& e) { return onMouseMove(e); };
    entry.onScroll    = [this](ScrollEvent& e)    { return onScroll(e);    };
    entry.onKey       = [this](KeyEvent& e)       { return onKey(e);        };
    entry.onEscape    = [this]()                  { onEscape(); };
    entry.onDismiss   = [this]() {
        // LayerStack removed the entry. Two sources:
        //   • dismissWith() set pendingReasonSlot() explicitly.
        //   • LayerStack auto-dismiss on scrim click — no pending
        //     reason; infer OutsideClick.
        auto cb      = m_state.spec.onClose;
        auto pending = pendingReasonSlot();
        const DialogCloseReason reason =
            pending.value_or(DialogCloseReason::OutsideClick);
        pendingReasonSlot() = std::nullopt;
        m_handle.detach_noRemove();
        m_state = {};
        if (cb) cb(reason);
    };

    m_handle = ctx.layerStack->push(OverlayLayer::Modal, std::move(entry));
}

// ───────────────────────────────────────────────────────────────────
// Events
// ───────────────────────────────────────────────────────────────────

bool DialogManager::onMouseDown(MouseEvent& e) {
    // LayerStack only routes inside-body clicks here. Outside clicks
    // either auto-dismiss (dismissOnScrimClick) or get swallowed
    // silently by LayerStack — no work for us.
    const int idx = hitTestButton(e.x, e.y);
    m_state.pressedButton = idx;
    return true;
}

bool DialogManager::onMouseUp(MouseEvent& e) {
    const int pressed = m_state.pressedButton;
    m_state.pressedButton = -1;
    if (pressed < 0) return true;

    const int hit = hitTestButton(e.x, e.y);
    if (hit == pressed) {
        fireButton(pressed);   // dismisses
    }
    return true;
}

bool DialogManager::onMouseMove(MouseMoveEvent& e) {
    const int hit = hitTestButton(e.x, e.y);
    m_state.hoveredButton = hit;
    return true;
}

bool DialogManager::onScroll(ScrollEvent& e) {
    if (m_state.scrollMax() <= 0.0f) return false;
    // dy > 0 = wheel up (content moves down → offset decreases),
    // matching ScrollView's convention.
    m_state.scrollOffset -= e.dy * 40.0f;
    clampScroll();
    return true;
}

bool DialogManager::onKey(KeyEvent& e) {
    if (e.consumed) return false;
    // Keyboard scrolling when the message overflows. Guarded so a
    // non-scrolling dialog still lets Enter/Escape through unchanged.
    if (m_state.scrollMax() > 0.0f) {
        switch (e.key) {
            case Key::PageDown:
                m_state.scrollOffset += m_state.msgRect.h * 0.9f; clampScroll(); return true;
            case Key::PageUp:
                m_state.scrollOffset -= m_state.msgRect.h * 0.9f; clampScroll(); return true;
            case Key::Down:
                m_state.scrollOffset += 30.0f; clampScroll(); return true;
            case Key::Up:
                m_state.scrollOffset -= 30.0f; clampScroll(); return true;
            case Key::Home: m_state.scrollOffset = 0.0f; return true;
            case Key::End:  m_state.scrollOffset = m_state.scrollMax(); return true;
            default: break;
        }
    }
    switch (e.key) {
        case Key::Escape:
            onEscape();
            return true;
        case Key::Enter:
        case Key::Space: {
            const int idx = primaryButtonIndex();
            if (idx >= 0) fireButton(idx);
            return true;
        }
        default:
            return false;
    }
}

void DialogManager::onEscape() {
    // Prefer firing a cancel-tagged button's onClick if one exists;
    // otherwise just close with Escape reason.
    const int idx = cancelButtonIndex();
    if (idx >= 0) {
        fireButton(idx);
    } else {
        dismissWith(DialogCloseReason::Escape);
    }
}

// ───────────────────────────────────────────────────────────────────
// Button activation
// ───────────────────────────────────────────────────────────────────

void DialogManager::fireButton(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_state.spec.buttons.size())) return;
    // Capture before dismissing — m_state is wiped inside dismissWith's
    // path.
    auto cb = m_state.spec.buttons[idx].onClick;
    dismissWith(DialogCloseReason::ButtonClicked);
    if (cb) cb();
}

int DialogManager::hitTestButton(float sx, float sy) const {
    for (size_t i = 0; i < m_state.buttonRects.size(); ++i) {
        if (m_state.buttonRects[i].contains(sx, sy))
            return static_cast<int>(i);
    }
    return -1;
}

int DialogManager::primaryButtonIndex() const {
    for (size_t i = 0; i < m_state.spec.buttons.size(); ++i) {
        if (m_state.spec.buttons[i].primary)
            return static_cast<int>(i);
    }
    return -1;
}

int DialogManager::cancelButtonIndex() const {
    for (size_t i = 0; i < m_state.spec.buttons.size(); ++i) {
        if (m_state.spec.buttons[i].cancel)
            return static_cast<int>(i);
    }
    return -1;
}

// ───────────────────────────────────────────────────────────────────
// ConfirmDialog convenience facade
// ───────────────────────────────────────────────────────────────────

void ConfirmDialog::prompt(std::string message,
                            std::function<void()> onConfirm,
                            std::function<void()> onCancel) {
    prompt("Confirm", std::move(message), std::move(onConfirm), std::move(onCancel));
}

void ConfirmDialog::prompt(std::string title, std::string message,
                            std::function<void()> onConfirm,
                            std::function<void()> onCancel) {
    promptCustom(std::move(title), std::move(message),
                  "Yes", "No",
                  std::move(onConfirm), std::move(onCancel));
}

void ConfirmDialog::promptCustom(std::string title, std::string message,
                                  std::string confirmLabel,
                                  std::string cancelLabel,
                                  std::function<void()> onConfirm,
                                  std::function<void()> onCancel) {
    DialogSpec spec;
    spec.title   = std::move(title);
    spec.message = std::move(message);

    DialogButton cancelBtn;
    cancelBtn.label   = std::move(cancelLabel);
    cancelBtn.cancel  = true;
    cancelBtn.onClick = std::move(onCancel);
    spec.buttons.push_back(std::move(cancelBtn));

    DialogButton confirmBtn;
    confirmBtn.label   = std::move(confirmLabel);
    confirmBtn.primary = true;
    confirmBtn.onClick = std::move(onConfirm);
    spec.buttons.push_back(std::move(confirmBtn));

    Dialog::show(std::move(spec));
}

} // namespace fw2
} // namespace ui
} // namespace yawn
