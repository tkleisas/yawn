#include "DragManager.h"

#include "UIContext.h"

// NOTE: renderDropHighlight is defined in Fw2Painters.cpp (which
// lives in the main exe target and has Renderer2D's full
// definition available). yawn_core, where this .cpp lives, can't
// pull in glad/gl.h.

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
DragManager::GhostPaintFn& painterSlot() {
    static DragManager::GhostPaintFn fn = nullptr;
    return fn;
}
} // anon

DragManager& DragManager::instance() {
    static DragManager g;
    return g;
}

void DragManager::setPainter(GhostPaintFn fn)         { painterSlot() = fn; }
DragManager::GhostPaintFn DragManager::painter()      { return painterSlot(); }

void DragManager::start(DragPayload payload, float sx, float sy) {
    // If we're already mid-drag for some reason, tear that down
    // silently first — sources shouldn't normally re-enter, but
    // bouncing twice on the same long-press is harmless this way.
    if (m_active) cancel();

    m_payload = std::move(payload);
    m_curX    = sx;
    m_curY    = sy;
    m_active  = true;

    pushOverlay();
}

void DragManager::updatePos(float sx, float sy) {
    if (!m_active) return;
    m_curX = sx;
    m_curY = sy;
    // Ghost overlay is constantly repainted from the current
    // position field — no need to bump anything else.
}

void DragManager::cancel() {
    if (!m_active) return;
    if (m_handle.active()) m_handle.remove();
    m_active   = false;
    m_payload  = {};
    m_curX = m_curY = 0.0f;
    m_ctrlHeld = false;
}

void DragManager::_testResetAll() {
    if (m_handle.active()) m_handle.detach_noRemove();
    m_active   = false;
    m_payload  = {};
    m_curX = m_curY = 0.0f;
    m_ctrlHeld = false;
}

void DragManager::pushOverlay() {
    UIContext& ctx = UIContext::global();
    if (!ctx.layerStack) return;

    OverlayEntry entry;
    entry.debugName             = "DragGhost";
    // The ghost is informational — a tiny bounds keeps it from
    // intercepting clicks. Painters may draw outside this rect via
    // the renderer's clip semantics.
    entry.bounds                = Rect{m_curX, m_curY, 1.0f, 1.0f};
    entry.modal                 = false;
    entry.dismissOnOutsideClick = false;
    // No hit testing — the ghost is decoration only. Returning
    // false from customHitTest tells the LayerStack "events pass
    // through me".
    entry.customHitTest = [](float, float) { return false; };
    entry.paint = [this](UIContext& c) {
        if (auto fn = painterSlot()) fn(m_payload, m_curX, m_curY, c);
    };
    m_handle = ctx.layerStack->push(OverlayLayer::Tooltip, std::move(entry));
}

} // namespace fw2
} // namespace ui
} // namespace yawn
