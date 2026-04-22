// UI v2 — DialogManager / ConfirmDialog tests.
//
// Exercises lifecycle + event state machine without a renderer.
// Real LayerStack is attached to UIContext so push / pop lifecycle
// is observable via entryCount(Modal).

#include <gtest/gtest.h>

#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/LayerStack.h"
#include "ui/framework/v2/UIContext.h"

using namespace yawn::ui::fw2;

class DialogHarness : public ::testing::Test {
protected:
    void SetUp() override {
        ctx.layerStack = &stack;
        ctx.viewport   = {0, 0, 1280, 720};
        UIContext::setGlobal(&ctx);
        DialogManager::instance()._testResetAll();
    }
    void TearDown() override {
        DialogManager::instance()._testResetAll();
        UIContext::setGlobal(nullptr);
    }
    UIContext  ctx;
    LayerStack stack;
};

// Helper — basic two-button spec with record-of-closure callbacks.
struct TestResult {
    int confirms = 0;
    int cancels  = 0;
    int closes   = 0;
    DialogCloseReason lastReason = DialogCloseReason::Dismiss;
};

static DialogSpec makeYesNo(TestResult& r) {
    DialogSpec spec;
    spec.title   = "Confirm";
    spec.message = "Proceed?";

    DialogButton cancel;
    cancel.label   = "No";
    cancel.cancel  = true;
    cancel.onClick = [&r]{ ++r.cancels; };
    spec.buttons.push_back(std::move(cancel));

    DialogButton ok;
    ok.label   = "Yes";
    ok.primary = true;
    ok.onClick = [&r]{ ++r.confirms; };
    spec.buttons.push_back(std::move(ok));

    spec.onClose = [&r](DialogCloseReason reason) {
        ++r.closes;
        r.lastReason = reason;
    };
    return spec;
}

// ─── Lifecycle ─────────────────────────────────────────────────────

TEST_F(DialogHarness, ShowPushesModalEntry) {
    EXPECT_FALSE(Dialog::isOpen());
    TestResult r;
    Dialog::show(makeYesNo(r));
    EXPECT_TRUE(Dialog::isOpen());
    EXPECT_EQ(stack.entryCount(OverlayLayer::Modal), 1);
    EXPECT_TRUE(stack.hasModalActive());
}

TEST_F(DialogHarness, CloseFiresOnCloseWithDismiss) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    Dialog::close();
    EXPECT_FALSE(Dialog::isOpen());
    EXPECT_EQ(r.closes, 1);
    EXPECT_EQ(r.lastReason, DialogCloseReason::Dismiss);
    EXPECT_EQ(r.confirms, 0);
    EXPECT_EQ(r.cancels,  0);
}

TEST_F(DialogHarness, ShowReplacesExisting) {
    TestResult r1, r2;
    Dialog::show(makeYesNo(r1));
    Dialog::show(makeYesNo(r2));
    // r1 was closed (Dismiss) when r2 opened.
    EXPECT_EQ(r1.closes, 1);
    EXPECT_EQ(r1.lastReason, DialogCloseReason::Dismiss);
    EXPECT_EQ(stack.entryCount(OverlayLayer::Modal), 1);
    EXPECT_TRUE(Dialog::isOpen());
}

TEST_F(DialogHarness, NoLayerStackSilentNoOp) {
    UIContext bare;
    UIContext::setGlobal(&bare);
    TestResult r;
    Dialog::show(makeYesNo(r));
    EXPECT_FALSE(Dialog::isOpen());
    EXPECT_EQ(r.closes, 0);
    UIContext::setGlobal(&ctx);
}

// ─── Keyboard ──────────────────────────────────────────────────────

TEST_F(DialogHarness, EscapeFiresCancelButton) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    KeyEvent k; k.key = Key::Escape;
    stack.dispatchKey(k);
    EXPECT_FALSE(Dialog::isOpen());
    EXPECT_EQ(r.cancels,  1);
    EXPECT_EQ(r.confirms, 0);
    EXPECT_EQ(r.closes,   1);
    EXPECT_EQ(r.lastReason, DialogCloseReason::ButtonClicked);
}

TEST_F(DialogHarness, EnterFiresPrimaryButton) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    KeyEvent k; k.key = Key::Enter;
    stack.dispatchKey(k);
    EXPECT_FALSE(Dialog::isOpen());
    EXPECT_EQ(r.confirms, 1);
    EXPECT_EQ(r.cancels,  0);
    EXPECT_EQ(r.closes,   1);
}

TEST_F(DialogHarness, SpaceFiresPrimaryButton) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    KeyEvent k; k.key = Key::Space;
    stack.dispatchKey(k);
    EXPECT_EQ(r.confirms, 1);
    EXPECT_FALSE(Dialog::isOpen());
}

TEST_F(DialogHarness, EscapeWithoutCancelButtonClosesWithEscape) {
    DialogSpec spec;
    spec.title   = "No buttons";
    spec.message = "Just text";
    TestResult r;
    spec.onClose = [&r](DialogCloseReason reason) {
        ++r.closes;
        r.lastReason = reason;
    };
    Dialog::show(std::move(spec));
    KeyEvent k; k.key = Key::Escape;
    stack.dispatchKey(k);
    EXPECT_EQ(r.closes, 1);
    EXPECT_EQ(r.lastReason, DialogCloseReason::Escape);
}

// ─── Mouse ────────────────────────────────────────────────────────

TEST_F(DialogHarness, MouseClickOnPrimaryButtonConfirms) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    const auto& s = DialogManager::instance().state();
    ASSERT_EQ(s.buttonRects.size(), 2u);
    const Rect& primaryBtn = s.buttonRects[1];   // second button is the primary "Yes"
    MouseEvent down;
    down.x = primaryBtn.x + primaryBtn.w * 0.5f;
    down.y = primaryBtn.y + primaryBtn.h * 0.5f;
    down.button = MouseButton::Left;
    stack.dispatchMouseDown(down);
    MouseEvent up = down;
    stack.dispatchMouseUp(up);
    EXPECT_EQ(r.confirms, 1);
    EXPECT_FALSE(Dialog::isOpen());
}

TEST_F(DialogHarness, MouseUpOffButtonDoesNotFire) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    const auto& s = DialogManager::instance().state();
    const Rect& primaryBtn = s.buttonRects[1];

    MouseEvent down;
    down.x = primaryBtn.x + primaryBtn.w * 0.5f;
    down.y = primaryBtn.y + primaryBtn.h * 0.5f;
    down.button = MouseButton::Left;
    stack.dispatchMouseDown(down);
    // Release off the button.
    MouseEvent up = down;
    up.x = s.bounds.x + 10;   // inside body, not on button
    up.y = s.bounds.y + 10;
    stack.dispatchMouseUp(up);
    EXPECT_EQ(r.confirms, 0);
    EXPECT_TRUE(Dialog::isOpen());
}

TEST_F(DialogHarness, MouseMoveTracksHoverButton) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    const auto& s = DialogManager::instance().state();
    const Rect& primaryBtn = s.buttonRects[1];
    MouseMoveEvent m;
    m.x = primaryBtn.x + primaryBtn.w * 0.5f;
    m.y = primaryBtn.y + primaryBtn.h * 0.5f;
    stack.dispatchMouseMove(m);
    EXPECT_EQ(DialogManager::instance().state().hoveredButton, 1);
}

TEST_F(DialogHarness, ScrimClickSwallowedByDefault) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    MouseEvent e;
    e.x = 10; e.y = 10;   // outside body
    e.button = MouseButton::Left;
    const bool consumed = stack.dispatchMouseDown(e);
    EXPECT_TRUE(consumed);            // modal blocks
    EXPECT_TRUE(Dialog::isOpen());   // still open
    EXPECT_EQ(r.closes, 0);
}

TEST_F(DialogHarness, ScrimClickDismissesWhenFlagSet) {
    DialogSpec spec;
    spec.message = "Click anywhere to close";
    spec.dismissOnScrimClick = true;
    TestResult r;
    spec.onClose = [&r](DialogCloseReason reason) {
        ++r.closes;
        r.lastReason = reason;
    };
    Dialog::show(std::move(spec));
    MouseEvent e;
    e.x = 10; e.y = 10;
    e.button = MouseButton::Left;
    stack.dispatchMouseDown(e);
    EXPECT_FALSE(Dialog::isOpen());
    EXPECT_EQ(r.closes, 1);
    EXPECT_EQ(r.lastReason, DialogCloseReason::OutsideClick);
}

// ─── Geometry ─────────────────────────────────────────────────────

TEST_F(DialogHarness, CentersInViewport) {
    TestResult r;
    Dialog::show(makeYesNo(r));
    const auto& s = DialogManager::instance().state();
    // Allow 2 px of rounding slop.
    EXPECT_NEAR(s.bounds.x + s.bounds.w * 0.5f, 640.0f, 2.0f);
    EXPECT_NEAR(s.bounds.y + s.bounds.h * 0.5f, 360.0f, 2.0f);
}

TEST_F(DialogHarness, AutoSizeExpandsForLongMessage) {
    DialogSpec spec;
    spec.title   = "A";
    spec.message = "This is a very very very very very long message line.";
    DialogButton ok; ok.label = "OK"; ok.primary = true;
    spec.buttons.push_back(std::move(ok));
    Dialog::show(std::move(spec));
    const auto& s = DialogManager::instance().state();
    EXPECT_GT(s.bounds.w, 360.0f);   // wider than the enforced minimum
}

TEST_F(DialogHarness, RespectsExplicitSize) {
    DialogSpec spec;
    spec.message = "x";
    spec.width   = 800.0f;
    spec.height  = 500.0f;
    Dialog::show(std::move(spec));
    const auto& s = DialogManager::instance().state();
    EXPECT_FLOAT_EQ(s.bounds.w, 800.0f);
    EXPECT_FLOAT_EQ(s.bounds.h, 500.0f);
}

// ─── ConfirmDialog facade ─────────────────────────────────────────

TEST_F(DialogHarness, ConfirmDialogPromptBuildsYesNo) {
    int confirms = 0, cancels = 0;
    ConfirmDialog::prompt("Are you sure?",
                           [&]{ ++confirms; },
                           [&]{ ++cancels; });
    ASSERT_TRUE(Dialog::isOpen());
    const auto& s = DialogManager::instance().state();
    ASSERT_EQ(s.spec.buttons.size(), 2u);
    EXPECT_EQ(s.spec.buttons[0].label, "No");
    EXPECT_EQ(s.spec.buttons[1].label, "Yes");
    EXPECT_TRUE(s.spec.buttons[0].cancel);
    EXPECT_TRUE(s.spec.buttons[1].primary);

    // Fire Enter → confirm.
    KeyEvent k; k.key = Key::Enter;
    stack.dispatchKey(k);
    EXPECT_EQ(confirms, 1);
    EXPECT_EQ(cancels,  0);
}

TEST_F(DialogHarness, ConfirmDialogEscapeFiresCancel) {
    int confirms = 0, cancels = 0;
    ConfirmDialog::prompt("Confirm", "Ok?",
                           [&]{ ++confirms; },
                           [&]{ ++cancels; });
    KeyEvent k; k.key = Key::Escape;
    stack.dispatchKey(k);
    EXPECT_EQ(cancels,  1);
    EXPECT_EQ(confirms, 0);
}

TEST_F(DialogHarness, ConfirmDialogPromptCustomLabels) {
    int confirms = 0;
    ConfirmDialog::promptCustom("Danger", "Delete this?",
                                 "Delete", "Keep",
                                 [&]{ ++confirms; });
    const auto& s = DialogManager::instance().state();
    EXPECT_EQ(s.spec.buttons[0].label, "Keep");
    EXPECT_EQ(s.spec.buttons[1].label, "Delete");
}
