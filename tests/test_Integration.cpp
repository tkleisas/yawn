// Integration tests — verify cross-component composition, event routing,
// DPI scaling, layout pipeline, and panel interactions.

#include <gtest/gtest.h>

// Framework
#include "ui/framework/Widget.h"
#include "ui/framework/FlexBox.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/DeviceWidget.h"
#include "ui/framework/DeviceHeaderWidget.h"
#include "ui/framework/SnapScrollContainer.h"
#include "ui/framework/VisualizerWidget.h"
#include "ui/framework/FwGrid.h"
#include "ui/framework/Dialog.h"

// Panels (only those that compile under YAWN_TEST_BUILD)
#include "ui/panels/DetailPanelWidget.h"

// Data model / audio / MIDI
#include "ui/Theme.h"
#include "app/Project.h"
#include "audio/Transport.h"
#include "audio/Mixer.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffectChain.h"
#include "midi/MidiClip.h"
#include "instruments/SubtractiveSynth.h"

using namespace yawn;
using namespace yawn::ui;
using namespace yawn::ui::fw;

// ═════════════════════════════════════════════════════════════════════════════
// Helper: Fixed-size test widget used as a panel stand-in
// ═════════════════════════════════════════════════════════════════════════════

class FixedPanelWidget : public Widget {
public:
    Size preferred;
    FixedPanelWidget(float w, float h) : preferred{w, h} {}

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain(preferred);
    }
};

class EventTrackingWidget : public Widget {
public:
    Size preferred{100, 50};
    int mouseDownCount  = 0;
    int mouseUpCount    = 0;
    int mouseMoveCount  = 0;
    int keyDownCount    = 0;
    bool consumeEvents  = true;

    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain(preferred);
    }
    bool onMouseDown(MouseEvent& e) override {
        ++mouseDownCount;
        if (consumeEvents) e.consume();
        return consumeEvents;
    }
    bool onMouseUp(MouseEvent& e) override {
        ++mouseUpCount;
        return consumeEvents;
    }
    bool onMouseMove(MouseMoveEvent& e) override {
        ++mouseMoveCount;
        return consumeEvents;
    }
    bool onKeyDown(KeyEvent& e) override {
        ++keyDownCount;
        return consumeEvents;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
// 1. WidgetTreeLayout — FlexBox composing panel-like children
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationLayout, FullPanelStackLayout) {
    // Simulate the root layout: menu(fixed 32) + session(flex) + mixer(fixed 280)
    // + detail(fixed 28 collapsed) + pianoroll(fixed 0 closed)
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget menuBar(1280, 32);
    FixedPanelWidget session(1280, 0);
    session.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));
    FixedPanelWidget mixer(1280, 280);
    FixedPanelWidget detail(1280, 28);
    FixedPanelWidget pianoRoll(1280, 0);

    root.addChild(&menuBar);
    root.addChild(&session);
    root.addChild(&mixer);
    root.addChild(&detail);
    root.addChild(&pianoRoll);

    UIContext ctx{};
    root.measure(Constraints::tight(1280, 720), ctx);
    root.layout(Rect{0, 0, 1280, 720}, ctx);

    // Menu gets 32px
    EXPECT_FLOAT_EQ(menuBar.bounds().h, 32.0f);
    EXPECT_FLOAT_EQ(menuBar.bounds().y, 0.0f);

    // Session fills remaining: 720 - 32 - 280 - 28 - 0 = 380
    EXPECT_GE(session.bounds().h, 100.0f);
    EXPECT_FLOAT_EQ(session.bounds().h, 380.0f);

    // Mixer fixed at 280
    EXPECT_FLOAT_EQ(mixer.bounds().h, 280.0f);

    // Detail collapsed at 28
    EXPECT_FLOAT_EQ(detail.bounds().h, 28.0f);

    // Piano roll hidden
    EXPECT_FLOAT_EQ(pianoRoll.bounds().h, 0.0f);
}

TEST(IntegrationLayout, AllChildrenStretchToParentWidth) {
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget a(100, 50), b(80, 30), c(60, 20);
    root.addChild(&a);
    root.addChild(&b);
    root.addChild(&c);

    UIContext ctx{};
    root.measure(Constraints::tight(400, 200), ctx);
    root.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().w, 400.0f);
    EXPECT_FLOAT_EQ(b.bounds().w, 400.0f);
    EXPECT_FLOAT_EQ(c.bounds().w, 400.0f);
}

TEST(IntegrationLayout, HiddenWidgetGetsZeroHeight) {
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget a(400, 50), b(400, 50), c(400, 50);
    b.setVisible(false);
    root.addChild(&a);
    root.addChild(&b);
    root.addChild(&c);

    UIContext ctx{};
    root.measure(Constraints::tight(400, 200), ctx);
    root.layout(Rect{0, 0, 400, 200}, ctx);

    EXPECT_FLOAT_EQ(a.bounds().h, 50.0f);
    EXPECT_FLOAT_EQ(c.bounds().y, 50.0f);  // c directly follows a
}

TEST(IntegrationLayout, FlexMinRespectedUnderPressure) {
    FlexBox root(Direction::Column);

    FixedPanelWidget fixed1(400, 200);
    FixedPanelWidget fixed2(400, 200);
    FixedPanelWidget flexChild(400, 0);
    flexChild.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));

    root.addChild(&fixed1);
    root.addChild(&flexChild);
    root.addChild(&fixed2);

    UIContext ctx{};
    // Total fixed = 400, available = 450 for flex, but constrained to 450 total
    root.measure(Constraints::tight(400, 450), ctx);
    root.layout(Rect{0, 0, 400, 450}, ctx);

    // Flex child gets remaining: 450 - 200 - 200 = 50, but min is 100
    EXPECT_GE(flexChild.bounds().h, 100.0f);
}

TEST(IntegrationLayout, SessionPanelFlexPolicy) {
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget session(0, 0);
    session.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));
    FixedPanelWidget mixer(0, 280);

    root.addChild(&session);
    root.addChild(&mixer);

    UIContext ctx{};
    root.measure(Constraints::tight(1280, 720), ctx);
    root.layout(Rect{0, 0, 1280, 720}, ctx);

    EXPECT_FLOAT_EQ(session.bounds().h, 440.0f);  // 720 - 280
    EXPECT_FLOAT_EQ(mixer.bounds().h, 280.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. DetailPanelComposition — real DetailPanelWidget + SubtractiveSynth
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationDetailPanel, SetDeviceChainWithSynth) {
    DetailPanelWidget detail;
    instruments::SubtractiveSynth synth;
    synth.init(44100, 256);

    detail.setOpen(true);
    detail.setDeviceChain(nullptr, &synth, nullptr);

    EXPECT_TRUE(detail.isOpen());
}

TEST(IntegrationDetailPanel, ToggleOpenClose) {
    DetailPanelWidget detail;
    EXPECT_FALSE(detail.isOpen());
    EXPECT_FLOAT_EQ(detail.height(), DetailPanelWidget::kCollapsedHeight);

    detail.setOpen(true);
    EXPECT_TRUE(detail.isOpen());

    detail.toggle();
    EXPECT_FALSE(detail.isOpen());
}

TEST(IntegrationDetailPanel, AnimationConvergesToOpen) {
    DetailPanelWidget detail;
    instruments::SubtractiveSynth synth;
    synth.init(44100, 256);
    detail.setDeviceChain(nullptr, &synth, nullptr);
    detail.setOpen(true);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    for (int i = 0; i < 100; ++i)
        detail.measure(c, ctx);

    EXPECT_FLOAT_EQ(detail.height(), DetailPanelWidget::kDefaultPanelHeight);
}

TEST(IntegrationDetailPanel, AnimationConvergesToClosed) {
    DetailPanelWidget detail;
    detail.setOpen(true);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // Converge to open
    for (int i = 0; i < 100; ++i)
        detail.measure(c, ctx);
    EXPECT_FLOAT_EQ(detail.height(), DetailPanelWidget::kDefaultPanelHeight);

    // Close and converge
    detail.setOpen(false);
    for (int i = 0; i < 100; ++i)
        detail.measure(c, ctx);
    EXPECT_FLOAT_EQ(detail.height(), DetailPanelWidget::kCollapsedHeight);
}

TEST(IntegrationDetailPanel, HeightAnimatesGradually) {
    DetailPanelWidget detail;
    detail.setOpen(true);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    detail.measure(c, ctx);
    float h = detail.height();
    EXPECT_GT(h, DetailPanelWidget::kCollapsedHeight);
    EXPECT_LT(h, DetailPanelWidget::kDefaultPanelHeight);
}

TEST(IntegrationDetailPanel, LayoutAssignsBounds) {
    DetailPanelWidget detail;
    detail.setOpen(true);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // Converge animation
    for (int i = 0; i < 100; ++i)
        detail.measure(c, ctx);

    detail.layout(Rect{0, 400, 800, detail.height()}, ctx);
    EXPECT_FLOAT_EQ(detail.bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(detail.bounds().y, 400.0f);
    EXPECT_FLOAT_EQ(detail.bounds().w, 800.0f);
}

TEST(IntegrationDetailPanel, ClearResetsDevices) {
    DetailPanelWidget detail;
    instruments::SubtractiveSynth synth;
    synth.init(44100, 256);

    detail.setOpen(true);
    detail.setDeviceChain(nullptr, &synth, nullptr);
    detail.clear();

    // After clear, re-calling setDeviceChain should still work
    detail.setDeviceChain(nullptr, &synth, nullptr);
    EXPECT_TRUE(detail.isOpen());
}

TEST(IntegrationDetailPanel, FocusManagement) {
    DetailPanelWidget detail;
    EXPECT_FALSE(detail.isFocused());

    detail.setFocused(true);
    EXPECT_TRUE(detail.isFocused());

    detail.setFocused(false);
    EXPECT_FALSE(detail.isFocused());
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. PianoRollIntegration — MidiClip + Transport data model
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationPianoRoll, ClipWithNotesIntegration) {
    midi::MidiClip clip(4.0);
    audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);

    midi::MidiNote n1{};
    n1.startBeat = 0.0; n1.duration = 0.5; n1.pitch = 60; n1.velocity = 32512;
    midi::MidiNote n2{};
    n2.startBeat = 1.0; n2.duration = 0.25; n2.pitch = 64; n2.velocity = 32512;
    midi::MidiNote n3{};
    n3.startBeat = 2.0; n3.duration = 1.0; n3.pitch = 67; n3.velocity = 32512;

    clip.addNote(n1);
    clip.addNote(n2);
    clip.addNote(n3);

    EXPECT_EQ(clip.noteCount(), 3);
    EXPECT_TRUE(clip.loop());
    EXPECT_DOUBLE_EQ(clip.lengthBeats(), 4.0);

    // Transport position → beat mapping
    double spb = transport.samplesPerBeat();
    EXPECT_GT(spb, 0.0);
    EXPECT_DOUBLE_EQ(transport.positionInBeats(), 0.0);
}

TEST(IntegrationPianoRoll, NoteQueryWithTransport) {
    midi::MidiClip clip(8.0);
    audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);

    midi::MidiNote n{};
    n.startBeat = 0.0; n.duration = 2.0; n.pitch = 60; n.velocity = 32512;
    clip.addNote(n);

    // Notes active at beat 1.0
    std::vector<int> active;
    clip.getActiveNotesAt(1.0, active);
    EXPECT_EQ(active.size(), 1u);

    // Notes active at beat 3.0 (after note ends)
    active.clear();
    clip.getActiveNotesAt(3.0, active);
    EXPECT_EQ(active.size(), 0u);
}

TEST(IntegrationPianoRoll, ClipAutoExtendOnLongNote) {
    midi::MidiClip clip(4.0);

    midi::MidiNote n{};
    n.startBeat = 3.0; n.duration = 3.0; n.pitch = 60;
    clip.addNote(n);

    // Extend clip to fit the note
    double noteEnd = n.startBeat + n.duration;
    if (noteEnd > clip.lengthBeats())
        clip.setLengthBeats(noteEnd);

    EXPECT_DOUBLE_EQ(clip.lengthBeats(), 6.0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. MixerPanelIntegration — audio::Mixer + Project data model
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationMixer, ProjectTracksMixerChannels) {
    Project project;
    project.init(4, 4);
    audio::Mixer mixer;

    // Each project track corresponds to a mixer channel
    for (int t = 0; t < project.numTracks(); ++t) {
        mixer.setTrackVolume(t, project.track(t).volume);
        EXPECT_FLOAT_EQ(mixer.trackChannel(t).volume, 1.0f);
    }
}

TEST(IntegrationMixer, MuteAndSoloWithProject) {
    Project project;
    project.init(4, 4);
    audio::Mixer mixer;

    project.track(0).muted = true;
    project.track(1).soloed = true;

    mixer.setTrackMute(0, project.track(0).muted);
    mixer.setTrackSolo(1, project.track(1).soloed);

    EXPECT_TRUE(mixer.trackChannel(0).muted);
    EXPECT_TRUE(mixer.trackChannel(1).soloed);
    EXPECT_TRUE(mixer.anySoloed());
}

TEST(IntegrationMixer, MeterUpdatesStorePeaks) {
    audio::Mixer mixer;
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    for (int i = 0; i < nf * nc; ++i) trackBuf[i] = 0.5f;

    float output[nf * nc] = {};
    float* ptrs[kMaxTracks] = {};
    ptrs[0] = trackBuf;

    mixer.process(ptrs, 1, output, nf, nc);

    EXPECT_GT(mixer.trackChannel(0).peakL, 0.0f);
    EXPECT_GT(mixer.trackChannel(0).peakR, 0.0f);
    EXPECT_GT(mixer.master().peakL, 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. EventRouting — mouse dispatch through the widget tree
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationEventRouting, MouseDownReachesCorrectChild) {
    Widget root;
    EventTrackingWidget childA, childB;
    root.addChild(&childA);
    root.addChild(&childB);

    UIContext ctx{};
    root.layout(Rect{0, 0, 400, 200}, ctx);
    childA.layout(Rect{0, 0, 200, 200}, ctx);
    childB.layout(Rect{200, 0, 200, 200}, ctx);

    // Click in childA's region
    MouseEvent e;
    e.x = 50; e.y = 50;
    e.button = MouseButton::Left;
    Widget* hit = root.dispatchMouseDown(e);

    EXPECT_EQ(hit, &childA);
    EXPECT_EQ(childA.mouseDownCount, 1);
    EXPECT_EQ(childB.mouseDownCount, 0);
}

TEST(IntegrationEventRouting, MouseDownReachesSecondChild) {
    Widget root;
    EventTrackingWidget childA, childB;
    root.addChild(&childA);
    root.addChild(&childB);

    UIContext ctx{};
    root.layout(Rect{0, 0, 400, 200}, ctx);
    childA.layout(Rect{0, 0, 200, 200}, ctx);
    childB.layout(Rect{200, 0, 200, 200}, ctx);

    MouseEvent e;
    e.x = 300; e.y = 50;
    e.button = MouseButton::Left;
    Widget* hit = root.dispatchMouseDown(e);

    EXPECT_EQ(hit, &childB);
    EXPECT_EQ(childA.mouseDownCount, 0);
    EXPECT_EQ(childB.mouseDownCount, 1);
}

TEST(IntegrationEventRouting, OverlappingChildrenTopmostWins) {
    Widget root;
    EventTrackingWidget bottom, top;
    root.addChild(&bottom);
    root.addChild(&top);

    UIContext ctx{};
    root.layout(Rect{0, 0, 200, 200}, ctx);
    bottom.layout(Rect{0, 0, 200, 200}, ctx);
    top.layout(Rect{0, 0, 200, 200}, ctx);

    MouseEvent e;
    e.x = 100; e.y = 100;
    e.button = MouseButton::Left;
    Widget* hit = root.dispatchMouseDown(e);

    EXPECT_EQ(hit, &top);
    EXPECT_EQ(top.mouseDownCount, 1);
    EXPECT_EQ(bottom.mouseDownCount, 0);
}

TEST(IntegrationEventRouting, MouseCaptureReceivesAllMoves) {
    Widget root;
    EventTrackingWidget child;
    root.addChild(&child);

    UIContext ctx{};
    root.layout(Rect{0, 0, 400, 400}, ctx);
    child.layout(Rect{0, 0, 100, 100}, ctx);

    // Capture mouse on child
    child.captureMouse();
    EXPECT_TRUE(child.hasMouseCapture());

    // Move event at position outside child but should be forwarded via capture
    MouseMoveEvent me;
    me.x = 300; me.y = 300;
    root.dispatchMouseMove(me);

    EXPECT_EQ(child.mouseMoveCount, 1);

    child.releaseMouse();
    EXPECT_FALSE(child.hasMouseCapture());
}

TEST(IntegrationEventRouting, DisabledWidgetSkipped) {
    Widget root;
    EventTrackingWidget child;
    child.setEnabled(false);
    root.addChild(&child);

    UIContext ctx{};
    root.layout(Rect{0, 0, 200, 200}, ctx);
    child.layout(Rect{0, 0, 200, 200}, ctx);

    MouseEvent e;
    e.x = 50; e.y = 50;
    e.button = MouseButton::Left;
    Widget* hit = root.dispatchMouseDown(e);

    EXPECT_EQ(hit, nullptr);
    EXPECT_EQ(child.mouseDownCount, 0);
}

TEST(IntegrationEventRouting, HiddenWidgetSkipped) {
    Widget root;
    EventTrackingWidget child;
    child.setVisible(false);
    root.addChild(&child);

    UIContext ctx{};
    root.layout(Rect{0, 0, 200, 200}, ctx);
    child.layout(Rect{0, 0, 200, 200}, ctx);

    MouseEvent e;
    e.x = 50; e.y = 50;
    e.button = MouseButton::Left;
    Widget* hit = root.dispatchMouseDown(e);

    EXPECT_EQ(hit, nullptr);
}

TEST(IntegrationEventRouting, FindWidgetAtDeepNesting) {
    Widget root;
    Widget mid;
    EventTrackingWidget leaf;
    root.addChild(&mid);
    mid.addChild(&leaf);

    UIContext ctx{};
    root.layout(Rect{0, 0, 400, 400}, ctx);
    mid.layout(Rect{10, 10, 200, 200}, ctx);
    leaf.layout(Rect{5, 5, 50, 50}, ctx);

    // Global coords: root(0,0) + mid(10,10) + leaf(5,5) = (15,15)
    Widget* found = root.findWidgetAt(20, 20);
    EXPECT_EQ(found, &leaf);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. DPIScaling — Theme scale factors
// ═════════════════════════════════════════════════════════════════════════════

class IntegrationDPITest : public ::testing::Test {
protected:
    void TearDown() override {
        Theme::scaleFactor = 1.0f;
        Theme::userScaleOverride = 0.0f;
    }
};

TEST_F(IntegrationDPITest, EffectiveScaleAutoDetect) {
    Theme::scaleFactor = 2.0f;
    Theme::userScaleOverride = 0.0f;
    EXPECT_FLOAT_EQ(Theme::effectiveScale(), 2.0f);
}

TEST_F(IntegrationDPITest, EffectiveScaleUserOverride) {
    Theme::scaleFactor = 2.0f;
    Theme::userScaleOverride = 1.5f;
    EXPECT_FLOAT_EQ(Theme::effectiveScale(), 1.5f);
}

TEST_F(IntegrationDPITest, ScaledPanelHeightsChangeWithDPI) {
    Theme::scaleFactor = 1.0f;
    Theme::userScaleOverride = 0.0f;
    float normalMixer = Theme::scaled(280.0f);
    EXPECT_FLOAT_EQ(normalMixer, 280.0f);

    Theme::scaleFactor = 2.0f;
    float hiDpiMixer = Theme::scaled(280.0f);
    EXPECT_FLOAT_EQ(hiDpiMixer, 560.0f);
}

TEST_F(IntegrationDPITest, ScaleContextConversion) {
    ScaleContext sc;
    sc.scale = 1.5f;
    EXPECT_FLOAT_EQ(sc.dp(100.0f), 150.0f);
    EXPECT_FLOAT_EQ(sc.fromPx(150.0f), 100.0f);
}

TEST_F(IntegrationDPITest, ScaledLayoutConstants) {
    Theme::scaleFactor = 1.5f;
    Theme::userScaleOverride = 0.0f;

    float scaledTBH = Theme::scaled(Theme::kTransportBarHeight);
    EXPECT_FLOAT_EQ(scaledTBH, Theme::kTransportBarHeight * 1.5f);

    float scaledTrackW = Theme::scaled(Theme::kTrackWidth);
    EXPECT_FLOAT_EQ(scaledTrackW, Theme::kTrackWidth * 1.5f);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. DialogIntegration — Dialog with content and event handling
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationDialog, EscapeClosesWithCancel) {
    Dialog dlg("Settings", 400, 300);
    DialogResult captured = DialogResult::None;
    dlg.setOnResult([&](DialogResult r) { captured = r; });

    KeyEvent e;
    e.keyCode = 27; // Escape
    dlg.onKeyDown(e);

    EXPECT_EQ(captured, DialogResult::Cancel);
    EXPECT_FALSE(dlg.visible());
}

TEST(IntegrationDialog, EnterConfirmsWithOKCancel) {
    Dialog dlg("Settings", 400, 300);
    dlg.setShowOKCancel(true);
    DialogResult captured = DialogResult::None;
    dlg.setOnResult([&](DialogResult r) { captured = r; });

    KeyEvent e;
    e.keyCode = 13; // Enter
    dlg.onKeyDown(e);

    EXPECT_EQ(captured, DialogResult::OK);
}

TEST(IntegrationDialog, ContentWidgetLaidOutInsideDialog) {
    Dialog dlg("Test", 400, 300);
    FixedPanelWidget content(350, 200);
    dlg.setContent(&content);

    UIContext ctx{};
    dlg.measure(Constraints::loose(800, 600), ctx);
    dlg.layout(Rect{100, 50, 400, 300}, ctx);

    // Content should be below the title bar
    EXPECT_GT(content.bounds().y, 50.0f);
    EXPECT_GE(content.bounds().x, 100.0f);
}

TEST(IntegrationDialog, TitleBarAndFooterGeometry) {
    Dialog dlg("Test", 400, 300);
    dlg.setShowOKCancel(true);

    UIContext ctx{};
    dlg.measure(Constraints::loose(800, 600), ctx);
    dlg.layout(Rect{0, 0, 400, 300}, ctx);

    Rect tb = dlg.titleBarRect();
    EXPECT_FLOAT_EQ(tb.h, Dialog::kTitleBarHeight);
    EXPECT_FLOAT_EQ(tb.w, 400.0f);

    Rect footer = dlg.footerRect();
    EXPECT_FLOAT_EQ(footer.h, Dialog::kFooterHeight);
    EXPECT_FLOAT_EQ(footer.y, 300.0f - Dialog::kFooterHeight);
}

TEST(IntegrationDialog, CloseMethodSetsResult) {
    Dialog dlg("Test");
    EXPECT_EQ(dlg.result(), DialogResult::None);

    dlg.close(DialogResult::OK);
    EXPECT_EQ(dlg.result(), DialogResult::OK);
    EXPECT_FALSE(dlg.visible());
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. CrossPanelInteraction — layout accommodating multiple animated panels
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationCrossPanel, DetailPanelInFlexBoxLayout) {
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget session(800, 0);
    session.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));

    DetailPanelWidget detail;
    detail.setOpen(true);

    root.addChild(&session);
    root.addChild(&detail);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // Converge detail animation
    for (int i = 0; i < 100; ++i)
        root.measure(c, ctx);

    root.layout(Rect{0, 0, 800, 600}, ctx);

    // Detail should take kDefaultPanelHeight, session gets the rest
    float detailH = detail.height();
    EXPECT_FLOAT_EQ(detailH, DetailPanelWidget::kDefaultPanelHeight);
    EXPECT_GE(session.bounds().h, 100.0f);
    EXPECT_NEAR(session.bounds().h + detailH, 600.0f, 1.0f);
}

TEST(IntegrationCrossPanel, DetailPanelClosedInLayout) {
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget session(800, 0);
    session.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));

    DetailPanelWidget detail;
    // detail starts closed (default)

    root.addChild(&session);
    root.addChild(&detail);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);
    root.measure(c, ctx);
    root.layout(Rect{0, 0, 800, 600}, ctx);

    float detailH = detail.height();
    EXPECT_FLOAT_EQ(detailH, DetailPanelWidget::kCollapsedHeight);
    EXPECT_GT(session.bounds().h, 500.0f); // most of the space
}

TEST(IntegrationCrossPanel, ToggleDetailAndRelayout) {
    FlexBox root(Direction::Column);
    root.setAlign(Align::Stretch);

    FixedPanelWidget session(800, 0);
    session.setSizePolicy(SizePolicy::flexMin(1.0f, 100.0f));

    DetailPanelWidget detail;
    root.addChild(&session);
    root.addChild(&detail);

    UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // Initial: detail closed
    root.measure(c, ctx);
    root.layout(Rect{0, 0, 800, 600}, ctx);
    float sessionH_closed = session.bounds().h;

    // Open detail and converge
    detail.setOpen(true);
    for (int i = 0; i < 100; ++i)
        root.measure(c, ctx);
    root.layout(Rect{0, 0, 800, 600}, ctx);
    float sessionH_open = session.bounds().h;

    // Session should shrink when detail opens
    EXPECT_LT(sessionH_open, sessionH_closed);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. DeviceWidget + SnapScroll composition
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationDeviceWidget, ParameterSetupAndMeasure) {
    DeviceWidget dw;
    dw.setDeviceName("SubSynth");
    dw.setDeviceType(DeviceHeaderWidget::DeviceType::Instrument);

    std::vector<DeviceWidget::ParamInfo> params = {
        {0, "Volume", "dB", 0, 1, 0.7f, false},
        {1, "Cutoff", "Hz", 20, 20000, 1000, false},
        {2, "Reso",   "%",  0, 1, 0.5f, false},
        {3, "On",     "",   0, 1, 1, true},
    };
    dw.setParameters(params);

    UIContext ctx{};
    Size s = dw.measure(Constraints::loose(400, 200), ctx);
    EXPECT_GT(s.w, 0.0f);
    EXPECT_GT(s.h, 0.0f);
}

TEST(IntegrationDeviceWidget, DeviceWidgetsInSnapScroll) {
    SnapScrollContainer scroll;

    DeviceWidget dw1, dw2;
    dw1.setDeviceName("Synth");
    dw2.setDeviceName("Reverb");

    std::vector<DeviceWidget::ParamInfo> params = {
        {0, "Mix", "%", 0, 1, 0.5f, false},
    };
    dw1.setParameters(params);
    dw2.setParameters(params);

    scroll.addChild(&dw1);
    scroll.addChild(&dw2);

    UIContext ctx{};
    scroll.measure(Constraints::loose(600, 200), ctx);
    scroll.layout(Rect{0, 0, 600, 200}, ctx);

    // Both device widgets should have been laid out
    EXPECT_GT(dw1.bounds().w, 0.0f);
    EXPECT_GT(dw2.bounds().w, 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Full pipeline: Project + Transport + Mixer + EffectChain
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationFullPipeline, ProjectMidiClipToTransport) {
    Project project;
    project.init(2, 2);

    // Set up a MIDI clip on track 0, scene 0
    auto* slot = project.getSlot(0, 0);
    ASSERT_NE(slot, nullptr);
    slot->midiClip = std::make_unique<midi::MidiClip>(4.0);

    midi::MidiNote n{};
    n.startBeat = 0.0; n.duration = 1.0; n.pitch = 60; n.velocity = 32512;
    slot->midiClip->addNote(n);

    auto* clip = project.getMidiClip(0, 0);
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->noteCount(), 1);

    // Transport can track position
    audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    EXPECT_FALSE(transport.isPlaying());

    transport.play();
    EXPECT_TRUE(transport.isPlaying());

    transport.advance(1024);
    EXPECT_GT(transport.positionInSamples(), 0);
}

TEST(IntegrationFullPipeline, EffectChainOnMixerChannel) {
    audio::Mixer mixer;
    effects::EffectChain fxChain;
    fxChain.init(44100.0, 256);

    // Effect chain is empty but initialized
    EXPECT_EQ(fxChain.count(), 0);
    EXPECT_TRUE(fxChain.empty());

    // Mixer processes audio correctly even without effects
    constexpr int nf = 64;
    constexpr int nc = 2;
    float trackBuf[nf * nc];
    for (int i = 0; i < nf * nc; ++i) trackBuf[i] = 0.5f;

    float output[nf * nc] = {};
    float* ptrs[kMaxTracks] = {};
    ptrs[0] = trackBuf;
    mixer.process(ptrs, 1, output, nf, nc);

    EXPECT_GT(mixer.master().peakL, 0.0f);
}
