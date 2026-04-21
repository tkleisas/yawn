#include "LayerStack.h"

#include "Theme.h"

#include <algorithm>

namespace yawn {
namespace ui {
namespace fw2 {

// ───────────────────────────────────────────────────────────────────
// OverlayHandle
// ───────────────────────────────────────────────────────────────────

void OverlayHandle::remove() {
    if (!m_owner || m_id == 0) return;
    LayerStack* owner = m_owner;
    auto  layer = m_layer;
    auto  id    = m_id;
    // Clear first so nested dismiss callbacks that also call remove()
    // on this handle don't re-enter.
    m_owner = nullptr;
    m_id    = 0;
    owner->removeById(layer, id, /*fireDismiss*/ true);
}

// ───────────────────────────────────────────────────────────────────
// LayerStack
// ───────────────────────────────────────────────────────────────────

LayerStack::~LayerStack() {
    // Suppress onDismiss on teardown — callbacks may reference state
    // that's already being destroyed (owning widgets, App members).
    // Explicit dismiss via handle destruction is still safe because
    // handles either outlive us (UB, documented in header) or were
    // reset before us.
    for (auto& layer : m_layers) layer.clear();
}

OverlayHandle LayerStack::push(OverlayLayer layer, OverlayEntry entry) {
    if (layer == OverlayLayer::Main) {
        // Main is the regular widget tree — not our business.
        return OverlayHandle{};
    }
    entry.id             = m_nextId++;
    entry.insertionOrder = m_nextInsertionOrder++;
    auto& vec = m_layers[static_cast<int>(layer)];
    vec.push_back(std::move(entry));
    return OverlayHandle{this, layer, vec.back().id};
}

void LayerStack::remove(OverlayHandle& h) {
    if (!h.active()) return;
    auto  layer = h.layer();
    auto  id    = h.id();
    h.detach_noRemove();
    removeById(layer, id, /*fireDismiss*/ true);
}

void LayerStack::removeById(OverlayLayer layer, std::uint64_t id,
                             bool fireDismiss) {
    auto& vec = m_layers[static_cast<int>(layer)];
    auto it = std::find_if(vec.begin(), vec.end(),
        [id](const OverlayEntry& e) { return e.id == id; });
    if (it == vec.end()) return;

    // Extract + pop before firing the callback — defends against the
    // callback touching LayerStack state (e.g. pushing a replacement
    // overlay).
    OverlayEntry victim = std::move(*it);
    vec.erase(it);
    if (fireDismiss && victim.onDismiss) victim.onDismiss();
}

// ───────────────────────────────────────────────────────────────────
// Paint
// ───────────────────────────────────────────────────────────────────

void LayerStack::paintLayers(UIContext& ctx, Rect viewport) {
    // NB: the shared modal scrim is NOT painted here — that would
    // require calling Renderer2D::drawRect, which would pull glad/GL
    // into yawn_core. Scrim painting lives in the main exe (see
    // Fw2Painters.cpp::paintModalScrim) and is called between the
    // main tree paint and this method. `viewport` is kept in the
    // signature because future per-layer effects (e.g. a tooltip
    // drop shadow that needs to clamp to the screen) will want it.
    (void)viewport;

    // Modal entries first — topmost dialog paints last (on top).
    for (auto& e : m_layers[static_cast<int>(OverlayLayer::Modal)]) {
        if (e.paint) e.paint(ctx);
    }
    // Overlay / Tooltip / Toast — bottom-to-top in enum order.
    for (auto which : { OverlayLayer::Overlay, OverlayLayer::Tooltip,
                         OverlayLayer::Toast }) {
        for (auto& e : m_layers[static_cast<int>(which)]) {
            if (e.paint) e.paint(ctx);
        }
    }
}

// ───────────────────────────────────────────────────────────────────
// Hit-test helpers
// ───────────────────────────────────────────────────────────────────

bool LayerStack::entryContains(const OverlayEntry& e, float sx, float sy) const {
    if (e.customHitTest) return e.customHitTest(sx, sy);
    return e.bounds.contains(sx, sy);
}

// ───────────────────────────────────────────────────────────────────
// Event dispatch
// ───────────────────────────────────────────────────────────────────

bool LayerStack::dispatchMouseDown(MouseEvent& e) {
    // ─── Overlay layer ────────────────────────────────────────────
    // Topmost Overlay entry only is considered for the outside-click
    // dismiss rule (not lower ones — dismissal pops one entry at a
    // time). If an entry contains the click we route to its handler.
    auto& overlays = m_layers[static_cast<int>(OverlayLayer::Overlay)];
    for (int i = static_cast<int>(overlays.size()) - 1; i >= 0; --i) {
        OverlayEntry& entry = overlays[i];
        if (entryContains(entry, e.x, e.y)) {
            if (entry.onMouseDown && entry.onMouseDown(e)) return true;
            if (entry.modal) return true;
            return false;   // non-handling, non-modal — event passes through
        }
    }
    // Click outside all overlay entries → consider dismissing the
    // topmost dismissable one.
    if (!overlays.empty()) {
        OverlayEntry& top = overlays.back();
        if (top.dismissOnOutsideClick) {
            const bool wasModal = top.modal;
            const std::uint64_t id = top.id;
            removeById(OverlayLayer::Overlay, id, /*fireDismiss*/ true);
            if (wasModal) return true;
            // Non-modal: click falls through to next layer.
        }
    }

    // ─── Modal layer ──────────────────────────────────────────────
    // Only topmost modal is interactive; scrim covers everything else.
    auto& modals = m_layers[static_cast<int>(OverlayLayer::Modal)];
    if (!modals.empty()) {
        OverlayEntry& top = modals.back();
        if (entryContains(top, e.x, e.y)) {
            if (top.onMouseDown && top.onMouseDown(e)) return true;
            return true;   // modal always blocks even if no handler
        }
        // Outside the modal body → scrim. Consume always; optionally
        // dismiss if the entry opted into outside-click dismiss
        // (rare — modals usually require explicit action).
        if (top.dismissOnOutsideClick) {
            removeById(OverlayLayer::Modal, top.id, /*fireDismiss*/ true);
        }
        return true;
    }

    return false;   // Main widget tree handles it
}

bool LayerStack::dispatchMouseUp(MouseEvent& e) {
    // MouseUp never dismisses; just routes to whichever entry is
    // under the pointer. Allows drag-release on an overlay widget.
    auto& overlays = m_layers[static_cast<int>(OverlayLayer::Overlay)];
    for (int i = static_cast<int>(overlays.size()) - 1; i >= 0; --i) {
        OverlayEntry& entry = overlays[i];
        if (entryContains(entry, e.x, e.y)) {
            if (entry.onMouseUp && entry.onMouseUp(e)) return true;
            if (entry.modal) return true;
            return false;
        }
    }
    auto& modals = m_layers[static_cast<int>(OverlayLayer::Modal)];
    if (!modals.empty()) {
        OverlayEntry& top = modals.back();
        if (entryContains(top, e.x, e.y)) {
            if (top.onMouseUp && top.onMouseUp(e)) return true;
        }
        return true;   // modal blocks mouse-up too
    }
    return false;
}

bool LayerStack::dispatchMouseMove(MouseMoveEvent& e) {
    // Route to topmost entry whose bounds contain the pointer. Don't
    // dismiss (hover-out dismissal is a per-widget concern, not a
    // global rule).
    auto& overlays = m_layers[static_cast<int>(OverlayLayer::Overlay)];
    for (int i = static_cast<int>(overlays.size()) - 1; i >= 0; --i) {
        OverlayEntry& entry = overlays[i];
        if (entryContains(entry, e.x, e.y)) {
            if (entry.onMouseMove && entry.onMouseMove(e)) return true;
            if (entry.modal) return true;
            return false;
        }
    }
    auto& modals = m_layers[static_cast<int>(OverlayLayer::Modal)];
    if (!modals.empty()) {
        OverlayEntry& top = modals.back();
        if (entryContains(top, e.x, e.y)) {
            if (top.onMouseMove && top.onMouseMove(e)) return true;
        }
        return true;
    }
    return false;
}

bool LayerStack::dispatchScroll(ScrollEvent& e) {
    auto& overlays = m_layers[static_cast<int>(OverlayLayer::Overlay)];
    for (int i = static_cast<int>(overlays.size()) - 1; i >= 0; --i) {
        OverlayEntry& entry = overlays[i];
        if (entryContains(entry, e.x, e.y)) {
            if (entry.onScroll && entry.onScroll(e)) return true;
            if (entry.modal) return true;
            return false;
        }
    }
    auto& modals = m_layers[static_cast<int>(OverlayLayer::Modal)];
    if (!modals.empty()) {
        OverlayEntry& top = modals.back();
        if (entryContains(top, e.x, e.y)) {
            if (top.onScroll && top.onScroll(e)) return true;
        }
        return true;
    }
    return false;
}

bool LayerStack::dispatchKey(KeyEvent& e) {
    // Escape dismisses topmost interactive entry unless an onEscape
    // override vetoes by consuming.
    auto fireEscape = [&](OverlayLayer layer,
                           std::vector<OverlayEntry>& vec) -> bool {
        if (vec.empty()) return false;
        OverlayEntry& top = vec.back();
        if (e.key == Key::Escape && !e.consumed) {
            if (top.onEscape) {
                top.onEscape();
            } else {
                removeById(layer, top.id, /*fireDismiss*/ true);
            }
            return true;
        }
        if (top.onKey && top.onKey(e)) return true;
        if (layer == OverlayLayer::Modal) return true;   // modals swallow keys
        return false;
    };

    // Overlay first (closer to user), then Modal.
    if (fireEscape(OverlayLayer::Overlay,
                    m_layers[static_cast<int>(OverlayLayer::Overlay)])) return true;
    if (fireEscape(OverlayLayer::Modal,
                    m_layers[static_cast<int>(OverlayLayer::Modal)]))  return true;
    return false;
}

// ───────────────────────────────────────────────────────────────────
// Query / debug
// ───────────────────────────────────────────────────────────────────

bool LayerStack::hasModalActive() const {
    return !m_layers[static_cast<int>(OverlayLayer::Modal)].empty();
}

int LayerStack::entryCount(OverlayLayer layer) const {
    return static_cast<int>(m_layers[static_cast<int>(layer)].size());
}

int LayerStack::totalEntryCount() const {
    int n = 0;
    for (const auto& v : m_layers) n += static_cast<int>(v.size());
    return n;
}

void LayerStack::dumpState(std::ostream& os) const {
    static const char* names[] = { "Main", "Modal", "Overlay", "Tooltip", "Toast" };
    for (int i = 0; i < 5; ++i) {
        const auto& vec = m_layers[i];
        if (vec.empty()) continue;
        os << names[i] << " (" << vec.size() << "):\n";
        for (const auto& e : vec) {
            os << "  id=" << e.id
               << " order=" << e.insertionOrder
               << " bounds=[" << e.bounds.x << "," << e.bounds.y << ","
                              << e.bounds.w << "," << e.bounds.h << "]"
               << " modal=" << (e.modal ? "1" : "0")
               << " dismissable=" << (e.dismissOnOutsideClick ? "1" : "0");
            if (!e.debugName.empty()) os << " name='" << e.debugName << "'";
            os << "\n";
        }
    }
}

} // namespace fw2
} // namespace ui
} // namespace yawn
