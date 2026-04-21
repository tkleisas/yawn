// UI v2 — LayerStack tests.
//
// Covers the contract in docs/ui-v2-layer-stack.md:
//   • push() returns RAII handle; destructor or explicit remove()
//     fires onDismiss exactly once.
//   • paintLayers invokes per-entry paint closures in bottom-up z-order
//     (Modal → Overlay → Tooltip → Toast).
//   • Mouse / scroll / key dispatch scans Overlay (newest-first) then
//     Modal before falling through to the Main tree.
//   • Outside-click dismiss: non-modal overlays pop + pass through;
//     modal overlays consume the click.
//   • Escape dismisses the topmost interactive entry, routing through
//     onEscape when supplied.
//   • Move-only handles survive transfer; stale handles are harmless.

#include <gtest/gtest.h>

#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/UIContext.h"

#include <sstream>

using namespace yawn::ui::fw2;

// Helper: build a minimal entry with bounds + a flag we can observe.
static OverlayEntry makeEntry(Rect b, int* mouseDownCount = nullptr,
                               bool* dismissed = nullptr,
                               bool modal = false) {
    OverlayEntry e;
    e.bounds = b;
    e.modal  = modal;
    if (mouseDownCount) {
        e.onMouseDown = [mouseDownCount](MouseEvent&) {
            ++*mouseDownCount;
            return true;   // consume
        };
    }
    if (dismissed) {
        e.onDismiss = [dismissed]() { *dismissed = true; };
    }
    return e;
}

// ─── push / remove / handle lifecycle ────────────────────────────────

TEST(LayerStack, PushIncreasesEntryCount) {
    LayerStack ls;
    EXPECT_EQ(ls.totalEntryCount(), 0);
    auto h = ls.push(OverlayLayer::Overlay, makeEntry({10, 10, 100, 40}));
    EXPECT_TRUE(h.active());
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 1);
    EXPECT_EQ(ls.totalEntryCount(), 1);
}

TEST(LayerStack, MainLayerRejected) {
    LayerStack ls;
    auto h = ls.push(OverlayLayer::Main, makeEntry({0, 0, 10, 10}));
    EXPECT_FALSE(h.active());
    EXPECT_EQ(ls.totalEntryCount(), 0);
}

TEST(LayerStack, HandleDestructorDismisses) {
    LayerStack ls;
    bool dismissed = false;
    {
        auto h = ls.push(OverlayLayer::Overlay,
                          makeEntry({0, 0, 10, 10}, nullptr, &dismissed));
        EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 1);
    }
    EXPECT_TRUE(dismissed);
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 0);
}

TEST(LayerStack, ExplicitRemoveIsIdempotent) {
    LayerStack ls;
    int dismissCalls = 0;
    OverlayEntry entry;
    entry.bounds = {0, 0, 10, 10};
    entry.onDismiss = [&]() { ++dismissCalls; };
    auto h = ls.push(OverlayLayer::Overlay, std::move(entry));

    ls.remove(h);
    EXPECT_FALSE(h.active());
    EXPECT_EQ(dismissCalls, 1);
    ls.remove(h);   // must not double-fire
    EXPECT_EQ(dismissCalls, 1);
}

TEST(LayerStack, MoveHandleTransfersOwnership) {
    LayerStack ls;
    bool dismissed = false;
    auto h1 = ls.push(OverlayLayer::Overlay,
                       makeEntry({0, 0, 10, 10}, nullptr, &dismissed));
    EXPECT_TRUE(h1.active());
    OverlayHandle h2 = std::move(h1);
    EXPECT_FALSE(h1.active());
    EXPECT_TRUE(h2.active());
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 1);

    h2.remove();
    EXPECT_TRUE(dismissed);
}

TEST(LayerStack, DestroyingLayerStackSuppressesDismiss) {
    // Document the "suppress onDismiss on destruction" behaviour.
    bool dismissed = false;
    {
        LayerStack ls;
        auto h = ls.push(OverlayLayer::Overlay,
                          makeEntry({0, 0, 10, 10}, nullptr, &dismissed));
        (void)h;
        h.detach_noRemove();   // simulate App outliving handles
    }
    EXPECT_FALSE(dismissed);
}

// ─── Query ──────────────────────────────────────────────────────────

TEST(LayerStack, HasModalActive) {
    LayerStack ls;
    EXPECT_FALSE(ls.hasModalActive());
    auto h1 = ls.push(OverlayLayer::Overlay, makeEntry({0, 0, 10, 10}));
    EXPECT_FALSE(ls.hasModalActive());
    auto h2 = ls.push(OverlayLayer::Modal, makeEntry({0, 0, 10, 10}));
    EXPECT_TRUE(ls.hasModalActive());
    h2.remove();
    EXPECT_FALSE(ls.hasModalActive());
}

// ─── Paint order ────────────────────────────────────────────────────

TEST(LayerStack, PaintOrderIsBottomUp) {
    LayerStack ls;
    UIContext  ctx;   // renderer null — paint closures observe only

    std::vector<const char*> order;
    auto mkPaint = [&](const char* tag) {
        return [&, tag](UIContext&) { order.push_back(tag); };
    };

    OverlayEntry toast;   toast.paint   = mkPaint("toast");
    OverlayEntry tooltip; tooltip.paint = mkPaint("tooltip");
    OverlayEntry overlay; overlay.paint = mkPaint("overlay");
    OverlayEntry modal;   modal.paint   = mkPaint("modal");

    // Push in non-ordered sequence to verify layer-sort, not push-order.
    auto h1 = ls.push(OverlayLayer::Toast,   std::move(toast));
    auto h2 = ls.push(OverlayLayer::Modal,   std::move(modal));
    auto h3 = ls.push(OverlayLayer::Overlay, std::move(overlay));
    auto h4 = ls.push(OverlayLayer::Tooltip, std::move(tooltip));

    ls.paintLayers(ctx, Rect{0, 0, 800, 600});

    ASSERT_EQ(order.size(), 4u);
    EXPECT_STREQ(order[0], "modal");
    EXPECT_STREQ(order[1], "overlay");
    EXPECT_STREQ(order[2], "tooltip");
    EXPECT_STREQ(order[3], "toast");
}

// ─── Mouse dispatch ─────────────────────────────────────────────────

TEST(LayerStack, MouseDownInsideOverlayRoutes) {
    LayerStack ls;
    int hits = 0;
    auto h = ls.push(OverlayLayer::Overlay,
                      makeEntry({10, 10, 100, 40}, &hits));

    MouseEvent e;
    e.x = 50; e.y = 30;
    EXPECT_TRUE(ls.dispatchMouseDown(e));
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 1);   // still there
}

TEST(LayerStack, MouseDownOutsideNonModalOverlayDismissesAndFallsThrough) {
    LayerStack ls;
    bool dismissed = false;
    auto h = ls.push(OverlayLayer::Overlay,
                      makeEntry({10, 10, 100, 40}, nullptr, &dismissed));

    MouseEvent e;
    e.x = 500; e.y = 500;   // way outside
    const bool consumed = ls.dispatchMouseDown(e);
    EXPECT_FALSE(consumed);            // non-modal: pass through
    EXPECT_TRUE(dismissed);
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 0);
}

TEST(LayerStack, MouseDownOutsideModalOverlayDismissesAndConsumes) {
    LayerStack ls;
    bool dismissed = false;
    auto h = ls.push(OverlayLayer::Overlay,
                      makeEntry({10, 10, 100, 40}, nullptr, &dismissed,
                                 /*modal*/ true));

    MouseEvent e;
    e.x = 500; e.y = 500;
    const bool consumed = ls.dispatchMouseDown(e);
    EXPECT_TRUE(consumed);             // modal: click swallowed
    EXPECT_TRUE(dismissed);
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 0);
}

TEST(LayerStack, OverlayTopmostFirstHitTest) {
    LayerStack ls;
    int firstHits = 0, secondHits = 0;
    auto h1 = ls.push(OverlayLayer::Overlay,
                       makeEntry({0, 0, 100, 100}, &firstHits));
    // Second, overlapping — newer, should win.
    auto h2 = ls.push(OverlayLayer::Overlay,
                       makeEntry({50, 50, 100, 100}, &secondHits));

    MouseEvent e;
    e.x = 75; e.y = 75;   // inside both
    EXPECT_TRUE(ls.dispatchMouseDown(e));
    EXPECT_EQ(firstHits, 0);
    EXPECT_EQ(secondHits, 1);
}

TEST(LayerStack, ModalBlocksMainTreeOnOutsideClick) {
    LayerStack ls;
    auto h = ls.push(OverlayLayer::Modal,
                      makeEntry({100, 100, 200, 200}));

    MouseEvent e;
    e.x = 10; e.y = 10;   // outside modal body → scrim
    EXPECT_TRUE(ls.dispatchMouseDown(e));   // always consumed
    // Modal entry survives since dismissOnOutsideClick default is true
    // but we overrode? No — default is true, so it DOES dismiss.
    // The fact that the click was consumed is the main-tree block.
    EXPECT_EQ(ls.entryCount(OverlayLayer::Modal), 0);
}

TEST(LayerStack, ModalOutsideClickWithoutDismissStaysOpen) {
    LayerStack ls;
    OverlayEntry entry;
    entry.bounds = {100, 100, 200, 200};
    entry.modal = true;
    entry.dismissOnOutsideClick = false;
    auto h = ls.push(OverlayLayer::Modal, std::move(entry));

    MouseEvent e;
    e.x = 10; e.y = 10;
    EXPECT_TRUE(ls.dispatchMouseDown(e));   // scrim still blocks
    EXPECT_EQ(ls.entryCount(OverlayLayer::Modal), 1);   // didn't dismiss
}

// ─── Custom hit-test ───────────────────────────────────────────────

TEST(LayerStack, CustomHitTestOverridesBounds) {
    LayerStack ls;
    int hits = 0;
    OverlayEntry entry;
    entry.bounds = {0, 0, 100, 100};
    entry.onMouseDown = [&](MouseEvent&) { ++hits; return true; };
    // Only count a hit on a small inner circle — everything else
    // "looks inside" by bounds but customHitTest rejects it.
    entry.customHitTest = [](float sx, float sy) {
        const float dx = sx - 50, dy = sy - 50;
        return dx * dx + dy * dy <= 10.0f * 10.0f;
    };
    auto h = ls.push(OverlayLayer::Overlay, std::move(entry));

    MouseEvent e;
    e.x = 50; e.y = 50;
    EXPECT_TRUE(ls.dispatchMouseDown(e));
    EXPECT_EQ(hits, 1);

    // Inside bounds but outside custom shape → treated as outside.
    e.x = 5; e.y = 5;
    ls.dispatchMouseDown(e);
    EXPECT_EQ(hits, 1);   // still 1; outside click dismissed it
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 0);
}

// ─── Escape ────────────────────────────────────────────────────────

TEST(LayerStack, EscapeDismissesTopmostOverlay) {
    LayerStack ls;
    bool dismissed = false;
    auto h = ls.push(OverlayLayer::Overlay,
                      makeEntry({0, 0, 10, 10}, nullptr, &dismissed));

    KeyEvent k;
    k.key = Key::Escape;
    EXPECT_TRUE(ls.dispatchKey(k));
    EXPECT_TRUE(dismissed);
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 0);
}

TEST(LayerStack, OnEscapeOverrideReplacesDefault) {
    LayerStack ls;
    int escCalls = 0;
    OverlayEntry entry;
    entry.bounds = {0, 0, 10, 10};
    entry.onEscape = [&]() { ++escCalls; };   // swallow without dismissing
    auto h = ls.push(OverlayLayer::Overlay, std::move(entry));

    KeyEvent k;
    k.key = Key::Escape;
    EXPECT_TRUE(ls.dispatchKey(k));
    EXPECT_EQ(escCalls, 1);
    // Entry NOT removed — override took responsibility.
    EXPECT_EQ(ls.entryCount(OverlayLayer::Overlay), 1);
}

TEST(LayerStack, EscapePrefersOverlayOverModal) {
    LayerStack ls;
    bool overlayDismissed = false;
    bool modalDismissed   = false;
    auto hm = ls.push(OverlayLayer::Modal,
                       makeEntry({0, 0, 100, 100}, nullptr, &modalDismissed));
    auto ho = ls.push(OverlayLayer::Overlay,
                       makeEntry({0, 0, 50, 50}, nullptr, &overlayDismissed));

    KeyEvent k;
    k.key = Key::Escape;
    EXPECT_TRUE(ls.dispatchKey(k));
    EXPECT_TRUE(overlayDismissed);
    EXPECT_FALSE(modalDismissed);
}

// ─── Scroll / move ────────────────────────────────────────────────

TEST(LayerStack, ScrollRoutesToOverlayUnderPointer) {
    LayerStack ls;
    int scrolls = 0;
    OverlayEntry entry;
    entry.bounds = {10, 10, 100, 100};
    entry.onScroll = [&](ScrollEvent&) { ++scrolls; return true; };
    auto h = ls.push(OverlayLayer::Overlay, std::move(entry));

    ScrollEvent s;
    s.x = 50; s.y = 50;
    s.dy = 3.0f;
    EXPECT_TRUE(ls.dispatchScroll(s));
    EXPECT_EQ(scrolls, 1);
}

TEST(LayerStack, MouseMoveRoutesToOverlayUnderPointer) {
    LayerStack ls;
    int moves = 0;
    OverlayEntry entry;
    entry.bounds = {10, 10, 100, 100};
    entry.onMouseMove = [&](MouseMoveEvent&) { ++moves; return true; };
    auto h = ls.push(OverlayLayer::Overlay, std::move(entry));

    MouseMoveEvent m;
    m.x = 50; m.y = 50;
    EXPECT_TRUE(ls.dispatchMouseMove(m));
    EXPECT_EQ(moves, 1);
}

// ─── Dump ─────────────────────────────────────────────────────────

TEST(LayerStack, DumpStateIncludesEntries) {
    LayerStack ls;
    OverlayEntry entry;
    entry.bounds = {10, 20, 100, 50};
    entry.debugName = "myPopup";
    auto h = ls.push(OverlayLayer::Overlay, std::move(entry));

    std::ostringstream os;
    ls.dumpState(os);
    const std::string out = os.str();
    EXPECT_NE(out.find("Overlay"), std::string::npos);
    EXPECT_NE(out.find("myPopup"), std::string::npos);
}
