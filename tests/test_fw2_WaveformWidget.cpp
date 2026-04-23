// fw2::WaveformWidget — migration regression tests.
//
// Exercises state transitions and event dispatch on the fw2 class
// directly. No paint coverage — that path is compiled out under
// YAWN_TEST_BUILD.

#include <gtest/gtest.h>

#include "ui/framework/v2/WaveformWidget.h"
#include "ui/framework/v2/UIContext.h"
#include "audio/Clip.h"
#include "audio/AudioBuffer.h"

using ::yawn::ui::fw2::WaveformWidget;
using ::yawn::ui::fw2::UIContext;
using ::yawn::ui::fw2::MouseEvent;
using ::yawn::ui::fw2::MouseMoveEvent;
using ::yawn::ui::fw2::ScrollEvent;
using ::yawn::ui::fw2::MouseButton;
using ::yawn::ui::fw::Constraints;
using ::yawn::ui::fw::Rect;

static std::shared_ptr<::yawn::audio::AudioBuffer> makeBuf(int frames, int ch = 1) {
    auto buf = std::make_shared<::yawn::audio::AudioBuffer>(ch, frames);
    return buf;
}

static ::yawn::audio::Clip makeClip(int frames = 48000, int ch = 1) {
    ::yawn::audio::Clip clip;
    clip.buffer = makeBuf(frames, ch);
    clip.originalBPM = 120.0;
    return clip;
}

static void seat(WaveformWidget& w, float width = 400.0f, float height = 120.0f) {
    UIContext ctx;
    w.measure(Constraints::tight(width, height), ctx);
    w.layout(Rect{0, 0, width, height}, ctx);
}

TEST(Fw2WaveformWidgetTest, DefaultsMatchV1) {
    WaveformWidget w;
    EXPECT_EQ(w.clip(), nullptr);
    EXPECT_FALSE(w.snapToGrid());
    EXPECT_DOUBLE_EQ(w.m_samplesPerPixel, 100.0);
}

TEST(Fw2WaveformWidgetTest, SetClipFitsToWidthOnLayout) {
    auto clip = makeClip(48000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w, 400.0f);
    // fitToWidth runs during first layout: spp = total/(w-4)
    EXPECT_NEAR(w.m_samplesPerPixel, 48000.0 / 396.0, 0.01);
    EXPECT_DOUBLE_EQ(w.m_scrollOffset, 0.0);
}

TEST(Fw2WaveformWidgetTest, ZoomInAndOutAreInverse) {
    auto clip = makeClip(48000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w);
    const double before = w.m_samplesPerPixel;
    w.zoomIn();
    w.zoomOut();
    EXPECT_NEAR(w.m_samplesPerPixel, before, 1e-6);
}

TEST(Fw2WaveformWidgetTest, FitToWidthAfterZoom) {
    auto clip = makeClip(48000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w);
    w.zoomIn(); w.zoomIn(); w.zoomIn();
    w.fitToWidth();
    EXPECT_NEAR(w.m_samplesPerPixel, 48000.0 / 396.0, 0.01);
    EXPECT_DOUBLE_EQ(w.m_scrollOffset, 0.0);
}

TEST(Fw2WaveformWidgetTest, SnapToggle) {
    WaveformWidget w;
    EXPECT_FALSE(w.snapToGrid());
    w.toggleSnapToGrid();
    EXPECT_TRUE(w.snapToGrid());
    w.setSnapToGrid(false);
    EXPECT_FALSE(w.snapToGrid());
}

TEST(Fw2WaveformWidgetTest, ScrollWheelScrollsHorizontally) {
    auto clip = makeClip(480000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w);
    // Zoom in a lot so we have room to scroll
    w.m_samplesPerPixel = 200.0;
    w.m_scrollOffset    = 20000.0;

    ScrollEvent e;
    e.lx = 100; e.ly = 60;
    e.dy = -1.0f;  // scroll "down" → advance right in this widget's mapping
    e.modifiers = 0;
    EXPECT_TRUE(w.dispatchScroll(e));
    EXPECT_GT(w.m_scrollOffset, 20000.0);
    EXPECT_FALSE(w.m_followPlayhead);
}

TEST(Fw2WaveformWidgetTest, CtrlScrollZooms) {
    auto clip = makeClip(480000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w);
    const double initialSpp = w.m_samplesPerPixel;

    ScrollEvent e;
    e.lx = 200; e.ly = 60;
    e.dy = 1.0f;
    e.modifiers = ::yawn::ui::fw2::ModifierKey::Ctrl;
    EXPECT_TRUE(w.dispatchScroll(e));
    EXPECT_LT(w.m_samplesPerPixel, initialSpp);  // scrolled up → zoom in
}

TEST(Fw2WaveformWidgetTest, OverviewClickJumpsScroll) {
    auto clip = makeClip(480000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w, 400.0f, 120.0f);

    MouseEvent e;
    e.x = 300; e.y = 5;       // y<kOverviewH(18) ⇒ overview bar
    e.lx = 300; e.ly = 5;
    e.button = MouseButton::Left;
    EXPECT_TRUE(w.dispatchMouseDown(e));
    EXPECT_TRUE(w.m_draggingOverview);
    EXPECT_FALSE(w.m_followPlayhead);

    MouseEvent up = e;
    w.dispatchMouseUp(up);
    EXPECT_FALSE(w.m_draggingOverview);
}

TEST(Fw2WaveformWidgetTest, LoopMarkerDragSetsState) {
    auto clip = makeClip(48000);
    clip.looping = true;
    clip.loopStart = 10000;
    clip.loopEnd = 30000;

    WaveformWidget w;
    w.setClip(&clip);
    seat(w);
    // Make spp so that 10000 ≈ pixel 80 (if spp = 125 and offset=0)
    w.m_samplesPerPixel = 125.0;
    w.m_scrollOffset = 0.0;

    MouseEvent e;
    // start marker at sample 10000 → pixel 80
    e.x = 80; e.y = 60;
    e.lx = 80; e.ly = 60;
    e.button = MouseButton::Left;
    EXPECT_TRUE(w.dispatchMouseDown(e));
    EXPECT_EQ(w.m_draggingLoopMarker, 1);
    w.dispatchMouseUp(e);
    EXPECT_EQ(w.m_draggingLoopMarker, 0);
}

TEST(Fw2WaveformWidgetTest, ZoomButtonHitTest) {
    auto clip = makeClip(48000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w, 400.0f, 120.0f);
    // Trigger a render-free button-rect update: zoom buttons are set in
    // paintZoomButtons which is not called under YAWN_TEST_BUILD. So the
    // rects are zero and hit-test won't trigger here. This test just
    // exercises that zoomIn() on its own doesn't crash.
    const double before = w.m_samplesPerPixel;
    w.zoomIn();
    EXPECT_LT(w.m_samplesPerPixel, before);
}

TEST(Fw2WaveformWidgetTest, SetPlayingEnablesFollow) {
    WaveformWidget w;
    w.m_followPlayhead = false;
    w.setPlaying(true);
    EXPECT_TRUE(w.m_followPlayhead);
    EXPECT_TRUE(w.m_playing);
}

TEST(Fw2WaveformWidgetTest, ResetViewClearsZoom) {
    auto clip = makeClip(48000);
    WaveformWidget w;
    w.setClip(&clip);
    seat(w);
    w.m_samplesPerPixel = 5000.0;
    w.m_scrollOffset = 12345.0;
    w.resetView();
    EXPECT_DOUBLE_EQ(w.m_samplesPerPixel, 100.0);
    EXPECT_DOUBLE_EQ(w.m_scrollOffset, 0.0);
}
