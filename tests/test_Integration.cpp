// Integration tests — verify cross-component composition, event routing,
// DPI scaling, layout pipeline, and panel interactions.

#include <gtest/gtest.h>

// Framework
#include "ui/framework/v2/Types.h"
#include "ui/framework/v2/DeviceHeaderWidget.h"
#include "ui/framework/v2/DeviceWidget.h"
#include "ui/framework/v2/SnapScrollContainer.h"

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
#include "audio/Clip.h"
#include "audio/AudioBuffer.h"
#include "instruments/SubtractiveSynth.h"

using namespace yawn;
using namespace yawn::ui;
using namespace yawn::ui::fw;

// (IntegrationLayout suite + FixedPanelWidget / EventTrackingWidget
//  helpers retired with the v1 framework. fw2 layout is covered by
//  test_fw2_FlexBox.cpp + the panel-specific tests below.)

// ═════════════════════════════════════════════════════════════════════════════
// 2. DetailPanelComposition — real DetailPanelWidget + SubtractiveSynth
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationDetailPanel, SetDeviceChainWithSynth) {
    fw2::DetailPanelWidget detail;
    instruments::SubtractiveSynth synth;
    synth.init(44100, 256);

    detail.setOpen(true);
    detail.setDeviceChain(nullptr, &synth, nullptr);

    EXPECT_TRUE(detail.isOpen());
}

TEST(IntegrationDetailPanel, ToggleOpenClose) {
    fw2::DetailPanelWidget detail;
    EXPECT_FALSE(detail.isOpen());
    EXPECT_FLOAT_EQ(detail.height(), fw2::DetailPanelWidget::kCollapsedHeight);

    detail.setOpen(true);
    EXPECT_TRUE(detail.isOpen());

    detail.toggle();
    EXPECT_FALSE(detail.isOpen());
}

TEST(IntegrationDetailPanel, AnimationConvergesToOpen) {
    fw2::DetailPanelWidget detail;
    instruments::SubtractiveSynth synth;
    synth.init(44100, 256);
    detail.setDeviceChain(nullptr, &synth, nullptr);
    detail.setOpen(true);

    fw2::UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    for (int i = 0; i < 100; ++i) {
        detail.tick();
        detail.measure(c, ctx);
    }

    EXPECT_FLOAT_EQ(detail.height(), fw2::DetailPanelWidget::kDefaultPanelHeight);
}

TEST(IntegrationDetailPanel, AnimationConvergesToClosed) {
    fw2::DetailPanelWidget detail;
    detail.setOpen(true);

    fw2::UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // Converge to open
    for (int i = 0; i < 100; ++i) {
        detail.tick();
        detail.measure(c, ctx);
    }
    EXPECT_FLOAT_EQ(detail.height(), fw2::DetailPanelWidget::kDefaultPanelHeight);

    // Close and converge
    detail.setOpen(false);
    for (int i = 0; i < 100; ++i) {
        detail.tick();
        detail.measure(c, ctx);
    }
    EXPECT_FLOAT_EQ(detail.height(), fw2::DetailPanelWidget::kCollapsedHeight);
}

TEST(IntegrationDetailPanel, HeightAnimatesGradually) {
    fw2::DetailPanelWidget detail;
    detail.setOpen(true);

    fw2::UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    detail.tick();
    detail.measure(c, ctx);
    float h = detail.height();
    EXPECT_GT(h, fw2::DetailPanelWidget::kCollapsedHeight);
    EXPECT_LT(h, fw2::DetailPanelWidget::kDefaultPanelHeight);
}

TEST(IntegrationDetailPanel, LayoutAssignsBounds) {
    fw2::DetailPanelWidget detail;
    detail.setOpen(true);

    fw2::UIContext ctx{};
    Constraints c = Constraints::tight(800, 600);

    // Converge animation
    for (int i = 0; i < 100; ++i) {
        detail.tick();
        detail.measure(c, ctx);
    }

    detail.layout(Rect{0, 400, 800, detail.height()}, ctx);
    EXPECT_FLOAT_EQ(detail.bounds().x, 0.0f);
    EXPECT_FLOAT_EQ(detail.bounds().y, 400.0f);
    EXPECT_FLOAT_EQ(detail.bounds().w, 800.0f);
}

TEST(IntegrationDetailPanel, ClearResetsDevices) {
    fw2::DetailPanelWidget detail;
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
    fw2::DetailPanelWidget detail;
    EXPECT_FALSE(detail.isFocused());

    detail.setFocused(true);
    EXPECT_TRUE(detail.isFocused());

    detail.setFocused(false);
    EXPECT_FALSE(detail.isFocused());
}

// ── Audio Clip Detail View Tests ──

TEST(IntegrationDetailPanel, SetAudioClipSwitchesToClipMode) {
    fw2::DetailPanelWidget detail;
    audio::Clip clip;
    clip.name = "Test Audio";
    clip.buffer = std::make_shared<audio::AudioBuffer>(2, 44100);

    detail.setAudioClip(&clip, nullptr, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);
    EXPECT_TRUE(detail.isOpen());
}

TEST(IntegrationDetailPanel, SetDeviceChainSwitchesBackToDevicesMode) {
    fw2::DetailPanelWidget detail;
    audio::Clip clip;
    clip.name = "Test Audio";
    clip.buffer = std::make_shared<audio::AudioBuffer>(2, 44100);

    detail.setAudioClip(&clip, nullptr, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);

    instruments::SubtractiveSynth synth;
    synth.init(44100, 256);
    detail.setDeviceChain(nullptr, &synth, nullptr);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::Devices);
}

TEST(IntegrationDetailPanel, ClearResetsClipMode) {
    fw2::DetailPanelWidget detail;
    audio::Clip clip;
    clip.name = "Test Audio";
    clip.buffer = std::make_shared<audio::AudioBuffer>(2, 44100);

    detail.setAudioClip(&clip, nullptr, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);

    detail.clear();
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::Devices);
}

TEST(IntegrationDetailPanel, AudioClipWithEffectChain) {
    fw2::DetailPanelWidget detail;
    audio::Clip clip;
    clip.name = "FX Test";
    clip.buffer = std::make_shared<audio::AudioBuffer>(2, 44100);
    clip.gain = 0.5f;
    clip.looping = true;
    clip.warpMode = audio::WarpMode::Beats;
    clip.originalBPM = 128.0;

    effects::EffectChain chain;
    chain.init(44100.0, 256);

    detail.setAudioClip(&clip, &chain, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);
    EXPECT_TRUE(detail.isOpen());
}

TEST(IntegrationDetailPanel, AudioClipFingerprintSkipsRedundantRebuilds) {
    fw2::DetailPanelWidget detail;
    audio::Clip clip;
    clip.name = "Test";
    clip.buffer = std::make_shared<audio::AudioBuffer>(1, 1000);

    // First call — builds
    detail.setAudioClip(&clip, nullptr, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);

    // Second call with same pointers — should be a no-op (fingerprint match)
    detail.setAudioClip(&clip, nullptr, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);
}

TEST(IntegrationDetailPanel, AudioClipNullBuffer) {
    fw2::DetailPanelWidget detail;
    audio::Clip clip;
    clip.name = "Empty Clip";
    // No buffer set

    detail.setAudioClip(&clip, nullptr, 44100);
    EXPECT_EQ(detail.viewMode(), fw2::DetailPanelWidget::ViewMode::AudioClip);
    EXPECT_TRUE(detail.isOpen());
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

// (IntegrationEventRouting suite retired with the v1 framework — fw2's
//  event routing is covered by the per-widget gesture tests in
//  test_fw2_*.cpp.)

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
// 7. CrossPanelInteraction — layout accommodating multiple animated panels
// ═════════════════════════════════════════════════════════════════════════════
// (Dialog integration tests retired along with v1 Dialog.h — see
//  tests/test_fw2_Dialog.cpp for the fw2 equivalents.)
//
// (DetailPanelInFlexBoxLayout / DetailPanelClosedInLayout / ToggleDetailAnd
//  Relayout retired with the fw1→fw2 DetailPanel migration: DetailPanel is
//  now an fw2 widget and can no longer be stuffed directly inside a v1
//  fw::FlexBox. Cross-framework layout integration is now provided by
//  DetailPanelWrapper in PanelWrappers.h. Panel-only animation behaviour
//  is still covered by the IntegrationDetailPanel tests above.)

// ═════════════════════════════════════════════════════════════════════════════
// 9. DeviceWidget + SnapScroll composition
// ═════════════════════════════════════════════════════════════════════════════

TEST(IntegrationDeviceWidget, ParameterSetupAndMeasure) {
    ::yawn::ui::fw2::DeviceWidget dw;
    dw.setDeviceName("SubSynth");
    dw.setDeviceType(::yawn::ui::fw2::DeviceHeaderWidget::DeviceType::Instrument);

    std::vector<::yawn::ui::fw2::DeviceWidget::ParamInfo> params = {
        {0, "Volume", "dB", 0, 1, 0.7f, false},
        {1, "Cutoff", "Hz", 20, 20000, 1000, false},
        {2, "Reso",   "%",  0, 1, 0.5f, false},
        {3, "On",     "",   0, 1, 1, true},
    };
    dw.setParameters(params);

    ::yawn::ui::fw2::UIContext ctx{};
    Size s = dw.measure(Constraints::loose(400, 200), ctx);
    EXPECT_GT(s.w, 0.0f);
    EXPECT_GT(s.h, 0.0f);
}

TEST(IntegrationDeviceWidget, DeviceWidgetsInSnapScroll) {
    ::yawn::ui::fw2::SnapScrollContainer scroll;

    ::yawn::ui::fw2::DeviceWidget dw1, dw2;
    dw1.setDeviceName("Synth");
    dw2.setDeviceName("Reverb");

    std::vector<::yawn::ui::fw2::DeviceWidget::ParamInfo> params = {
        {0, "Mix", "%", 0, 1, 0.5f, false},
    };
    dw1.setParameters(params);
    dw2.setParameters(params);

    scroll.addChild(&dw1);
    scroll.addChild(&dw2);

    ::yawn::ui::fw2::UIContext ctx{};
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
