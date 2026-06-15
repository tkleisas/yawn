#include <gtest/gtest.h>
#include "app/ArrangementClip.h"
#include "app/Project.h"
#include "visual/VisualClip.h"

using namespace yawn;

// ── ArrangementClip ────────────────────────────────────────────────────────

TEST(ArrangementClip, EndBeat) {
    ArrangementClip c;
    c.startBeat = 4.0;
    c.lengthBeats = 8.0;
    EXPECT_DOUBLE_EQ(c.endBeat(), 12.0);
}

TEST(ArrangementClip, VisualTypeHoldsVisualClip) {
    ArrangementClip c;
    c.type = ArrangementClip::Type::Visual;
    c.startBeat = 2.0;
    c.lengthBeats = 4.0;
    c.visualClip = std::make_unique<visual::VisualClip>();
    c.visualClip->source.shaderPath = "shaders/demo.frag";
    c.visualClip->name       = "demo";

    ASSERT_NE(c.visualClip, nullptr);
    EXPECT_EQ(c.visualClip->source.shaderPath, "shaders/demo.frag");
    EXPECT_EQ(c.type, ArrangementClip::Type::Visual);
}

TEST(ArrangementClip, CopyDeepClonesVisualClip) {
    ArrangementClip a;
    a.type = ArrangementClip::Type::Visual;
    a.visualClip = std::make_unique<visual::VisualClip>();
    a.visualClip->source.shaderPath = "shaders/a.frag";

    ArrangementClip b = a;  // uses the copy constructor
    ASSERT_NE(b.visualClip, nullptr);
    // Deep clone — the two should be distinct instances.
    EXPECT_NE(b.visualClip.get(), a.visualClip.get());
    EXPECT_EQ(b.visualClip->source.shaderPath, "shaders/a.frag");
    // Mutating one must not touch the other.
    b.visualClip->source.shaderPath = "shaders/b.frag";
    EXPECT_EQ(a.visualClip->source.shaderPath, "shaders/a.frag");
}

TEST(ArrangementClip, OverlapsTrue) {
    ArrangementClip c;
    c.startBeat = 4.0;
    c.lengthBeats = 4.0;
    EXPECT_TRUE(c.overlaps(2.0, 6.0));
    EXPECT_TRUE(c.overlaps(5.0, 10.0));
    EXPECT_TRUE(c.overlaps(4.0, 8.0));
}

TEST(ArrangementClip, OverlapsFalse) {
    ArrangementClip c;
    c.startBeat = 4.0;
    c.lengthBeats = 4.0;
    EXPECT_FALSE(c.overlaps(0.0, 4.0));   // touching but not overlapping
    EXPECT_FALSE(c.overlaps(8.0, 12.0));
    EXPECT_FALSE(c.overlaps(10.0, 20.0));
}

// ── Track arrangement ──────────────────────────────────────────────────────

TEST(TrackArrangement, DefaultsToSessionMode) {
    Track t;
    EXPECT_FALSE(t.arrangementActive);
    EXPECT_TRUE(t.arrangementClips.empty());
}

TEST(TrackArrangement, ArrangementClipAt) {
    Track t;
    ArrangementClip c1;
    c1.startBeat = 0.0;
    c1.lengthBeats = 4.0;
    c1.name = "c1";
    ArrangementClip c2;
    c2.startBeat = 8.0;
    c2.lengthBeats = 4.0;
    c2.name = "c2";
    t.arrangementClips = {c1, c2};

    EXPECT_EQ(t.arrangementClipAt(0.0)->name, "c1");
    EXPECT_EQ(t.arrangementClipAt(3.99)->name, "c1");
    EXPECT_EQ(t.arrangementClipAt(4.0), nullptr);  // gap
    EXPECT_EQ(t.arrangementClipAt(8.0)->name, "c2");
    EXPECT_EQ(t.arrangementClipAt(12.0), nullptr);  // past end
}

TEST(TrackArrangement, SortArrangementClips) {
    Track t;
    ArrangementClip a, b, c;
    a.startBeat = 12.0; a.name = "a";
    b.startBeat = 0.0;  b.name = "b";
    c.startBeat = 4.0;  c.name = "c";
    t.arrangementClips = {a, b, c};
    t.sortArrangementClips();

    EXPECT_EQ(t.arrangementClips[0].name, "b");
    EXPECT_EQ(t.arrangementClips[1].name, "c");
    EXPECT_EQ(t.arrangementClips[2].name, "a");
}

// ── Project arrangement ────────────────────────────────────────────────────

TEST(ProjectArrangement, DefaultViewMode) {
    Project p;
    p.init();
    EXPECT_EQ(p.viewMode(), ViewMode::Session);
}

TEST(ProjectArrangement, SetViewMode) {
    Project p;
    p.init();
    p.setViewMode(ViewMode::Arrangement);
    EXPECT_EQ(p.viewMode(), ViewMode::Arrangement);
}

TEST(ProjectArrangement, ArrangementLength) {
    Project p;
    p.init();
    EXPECT_DOUBLE_EQ(p.arrangementLength(), 64.0);
    p.setArrangementLength(128.0);
    EXPECT_DOUBLE_EQ(p.arrangementLength(), 128.0);
}

TEST(ProjectArrangement, UpdateArrangementLength) {
    Project p;
    p.init(2, 2);

    ArrangementClip c;
    c.startBeat = 100.0;
    c.lengthBeats = 16.0;
    p.track(0).arrangementClips.push_back(c);

    p.updateArrangementLength();
    // 100 + 16 = 116 clip end, plus 16 beats padding = 132
    EXPECT_DOUBLE_EQ(p.arrangementLength(), 132.0);
}

// ── ArrangementPlayback engine ─────────────────────────────────────────────

#include "audio/ArrangementPlayback.h"
#include "audio/Transport.h"
#include <atomic>
#include <thread>

namespace {

// Helper: create a simple audio buffer with a known pattern
std::shared_ptr<yawn::audio::AudioBuffer> makeTestBuffer(int frames, int channels = 1) {
    auto buf = std::make_shared<yawn::audio::AudioBuffer>(channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (int f = 0; f < frames; ++f)
            buf->sample(ch, f) = static_cast<float>(f + 1) * 0.01f;
    return buf;
}

// Helper: create a simple MIDI clip with one note
std::shared_ptr<yawn::midi::MidiClip> makeTestMidiClip() {
    auto mc = std::make_shared<yawn::midi::MidiClip>();
    mc->setLengthBeats(4.0);
    yawn::midi::MidiNote note;
    note.startBeat = 0.0;
    note.duration = 1.0;
    note.pitch = 60;
    note.channel = 0;
    note.velocity = 16384; // ~50% in 16-bit
    mc->addNote(note);
    return mc;
}

} // anonymous namespace

TEST(ArrangementPlayback, TrackActiveDefault) {
    yawn::audio::ArrangementPlayback ap;
    EXPECT_FALSE(ap.isTrackActive(0));
    EXPECT_FALSE(ap.isTrackActive(7));
}

TEST(ArrangementPlayback, SetTrackActive) {
    yawn::audio::ArrangementPlayback ap;
    ap.setTrackActive(0, true);
    EXPECT_TRUE(ap.isTrackActive(0));
    EXPECT_FALSE(ap.isTrackActive(1));
    ap.setTrackActive(0, false);
    EXPECT_FALSE(ap.isTrackActive(0));
}

TEST(ArrangementPlayback, SetTrackClips) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.audioBuffer = makeTestBuffer(44100);
    clips.push_back(c);

    ap.setTrackClips(0, std::move(clips));
    ap.setTrackActive(0, true);
    EXPECT_TRUE(ap.isTrackActive(0));
}

TEST(ArrangementPlayback, AudioPlaybackWritesToBuffer) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    transport.play();
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    // Create a clip at beat 0 with known audio data
    auto audioBuf = makeTestBuffer(44100, 2);
    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.audioBuffer = audioBuf;
    clips.push_back(c);

    ap.setTrackClips(0, std::move(clips));
    ap.setTrackActive(0, true);

    // Render 256 frames
    std::vector<float> buffer(256 * 2, 0.0f);
    ap.processAudioTrack(0, buffer.data(), 256, 2);

    // Should have non-zero output (audio data was written)
    bool hasNonZero = false;
    for (float s : buffer) {
        if (s != 0.0f) { hasNonZero = true; break; }
    }
    EXPECT_TRUE(hasNonZero);
}

TEST(ArrangementPlayback, SilenceInGap) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    transport.play();
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    // Clip starts at beat 8 (after a gap)
    auto audioBuf = makeTestBuffer(44100, 1);
    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 8.0;
    c.lengthBeats = 4.0;
    c.audioBuffer = audioBuf;
    clips.push_back(c);

    ap.setTrackClips(0, std::move(clips));
    ap.setTrackActive(0, true);

    // Transport is at beat 0, clip starts at beat 8 — should be silence
    std::vector<float> buffer(256, 0.0f);
    ap.processAudioTrack(0, buffer.data(), 256, 1);

    bool allZero = true;
    for (float s : buffer) {
        if (s != 0.0f) { allZero = false; break; }
    }
    EXPECT_TRUE(allZero);
}

TEST(ArrangementPlayback, InactiveTrackNoOutput) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    transport.play();
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    auto audioBuf = makeTestBuffer(44100, 1);
    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.audioBuffer = audioBuf;
    clips.push_back(c);

    ap.setTrackClips(0, std::move(clips));
    // Track NOT active — should produce no output

    std::vector<float> buffer(256, 0.0f);
    ap.processAudioTrack(0, buffer.data(), 256, 1);

    bool allZero = true;
    for (float s : buffer) {
        if (s != 0.0f) { allZero = false; break; }
    }
    EXPECT_TRUE(allZero);
}

TEST(ArrangementPlayback, MidiClipEmitsNotes) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    transport.play();
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    auto mc = makeTestMidiClip();
    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Midi;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.midiClip = mc;
    clips.push_back(c);

    ap.setTrackClips(0, std::move(clips));
    ap.setTrackActive(0, true);

    // Process enough frames to cover beat 0 (note-on at beat 0)
    yawn::midi::MidiBuffer midiBuffer;
    midiBuffer.clear();
    ap.processMidiTrack(0, midiBuffer, 256);

    // Should have at least one MIDI message (note-on at beat 0)
    EXPECT_GT(midiBuffer.count(), 0);
}

// ── Loop vs silence on extend (v0.84.3 / v0.84.4) ──

TEST(ArrangementPlayback, AudioLoopWrapsPastSourceEnd) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    transport.play();
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    // 100-frame source, extended to 4 beats, looping. A single 256-frame
    // render crosses the source end ~2.5×, so positions past the source
    // must wrap and keep producing sound.
    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.loop = true;
    c.audioBuffer = makeTestBuffer(100, 1);
    clips.push_back(c);
    ap.setTrackClips(0, std::move(clips));
    ap.setTrackActive(0, true);

    std::vector<float> buffer(256, 0.0f);
    ap.processAudioTrack(0, buffer.data(), 256, 1);
    EXPECT_NE(buffer[50], 0.0f);    // source plays
    EXPECT_NE(buffer[200], 0.0f);   // and keeps playing past its end (looped)
}

TEST(ArrangementPlayback, AudioNoLoopSilentPastSourceEnd) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    transport.setBPM(120.0);
    transport.play();
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.loop = false;            // one-shot → silent past the source
    c.audioBuffer = makeTestBuffer(100, 1);
    clips.push_back(c);
    ap.setTrackClips(0, std::move(clips));
    ap.setTrackActive(0, true);

    std::vector<float> buffer(256, 0.0f);
    ap.processAudioTrack(0, buffer.data(), 256, 1);
    EXPECT_NE(buffer[50], 0.0f);    // source plays
    EXPECT_EQ(buffer[200], 0.0f);   // then silence past the 100-frame source
}

TEST(ArrangementPlayback, MidiLoopRepeatsVsOneShot) {
    // A 1-beat MIDI loop placed in a 4-beat slot: looping re-fires the note
    // each beat (~4×), a one-shot fires it once. Render the whole 4 beats.
    auto mc = std::make_shared<yawn::midi::MidiClip>();
    mc->setLengthBeats(1.0);
    yawn::midi::MidiNote note;
    note.startBeat = 0.0; note.duration = 0.5;
    note.pitch = 60; note.channel = 0; note.velocity = 16384;
    mc->addNote(note);

    auto run = [&](bool loop) {
        yawn::audio::ArrangementPlayback ap;
        yawn::audio::Transport transport;
        transport.setSampleRate(44100.0);
        transport.setBPM(120.0);
        transport.play();
        ap.setTransport(&transport);
        ap.setSampleRate(44100.0);
        std::vector<yawn::audio::ArrClipRef> clips;
        yawn::audio::ArrClipRef c;
        c.type = yawn::audio::ArrClipRef::Type::Midi;
        c.startBeat = 0.0; c.lengthBeats = 4.0; c.loop = loop; c.midiClip = mc;
        clips.push_back(c);
        ap.setTrackClips(0, std::move(clips));
        ap.setTrackActive(0, true);
        yawn::midi::MidiBuffer mb; mb.clear();
        ap.processMidiTrack(0, mb, 4 * 22050);  // 4 beats at 120 BPM / 44.1k
        return mb.count();
    };
    EXPECT_GT(run(true), run(false));   // looping emits more events
    EXPECT_GT(run(true), 2);            // multiple iterations, not just one
}

TEST(ArrangementPlayback, SubmitTrackClipsThreadSafe) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    auto audioBuf = makeTestBuffer(1000, 1);
    std::vector<yawn::audio::ArrClipRef> clips;
    yawn::audio::ArrClipRef c;
    c.type = yawn::audio::ArrClipRef::Type::Audio;
    c.startBeat = 0.0;
    c.lengthBeats = 4.0;
    c.audioBuffer = audioBuf;
    clips.push_back(c);

    // Submit from "UI thread"
    ap.submitTrackClips(0, std::move(clips));

    // Apply on "audio thread"
    ap.applyPendingClips();
    ap.setTrackActive(0, true);
    EXPECT_TRUE(ap.isTrackActive(0));
}

// Hammer the lock-free submit/apply/retire path from two threads.
// ASan/LSan verifies every node and clip list is freed exactly once.
TEST(ArrangementPlayback, ConcurrentSubmitAndApply) {
    yawn::audio::ArrangementPlayback ap;
    yawn::audio::Transport transport;
    transport.setSampleRate(44100.0);
    ap.setTransport(&transport);
    ap.setSampleRate(44100.0);

    transport.setBPM(120.0);
    transport.play();
    auto audioBuf = makeTestBuffer(44100, 1);
    constexpr int kSubmits = 5000;
    std::atomic<bool> done{false};

    std::thread uiThread([&] {
        for (int i = 0; i < kSubmits; ++i) {
            std::vector<yawn::audio::ArrClipRef> clips;
            yawn::audio::ArrClipRef c;
            c.type = yawn::audio::ArrClipRef::Type::Audio;
            c.startBeat = 0.0;
            c.lengthBeats = 4.0;
            c.audioBuffer = audioBuf;
            clips.push_back(c);
            ap.submitTrackClips(i % yawn::kMaxTracks, std::move(clips));
        }
        done.store(true, std::memory_order_release);
    });

    // "Audio thread": apply until the submitter finishes, then once
    // more to drain any submission that landed after the last apply.
    while (!done.load(std::memory_order_acquire))
        ap.applyPendingClips();
    ap.applyPendingClips();
    uiThread.join();
    ap.collectRetired();

    // Every track received submissions and the last one was applied —
    // each must render the clip's audio at beat 0.
    for (int t = 0; t < yawn::kMaxTracks && t < kSubmits; ++t) {
        ap.setTrackActive(t, true);
        std::vector<float> buffer(256 * 2, 0.0f);
        ap.processAudioTrack(t, buffer.data(), 256, 2);
        bool hasNonZero = false;
        for (float s : buffer)
            if (s != 0.0f) { hasNonZero = true; break; }
        EXPECT_TRUE(hasNonZero) << "track " << t << " lost its clip list";
    }
}

// ── Transport loop ──────────────────────────────────────────────────────

TEST(TransportLoop, LoopDisabledByDefault) {
    yawn::audio::Transport t;
    EXPECT_FALSE(t.isLoopEnabled());
    EXPECT_DOUBLE_EQ(t.loopStartBeats(), 0.0);
    EXPECT_DOUBLE_EQ(t.loopEndBeats(), 0.0);
}

TEST(TransportLoop, SetLoopRange) {
    yawn::audio::Transport t;
    t.setLoopRange(4.0, 12.0);
    EXPECT_DOUBLE_EQ(t.loopStartBeats(), 4.0);
    EXPECT_DOUBLE_EQ(t.loopEndBeats(), 12.0);
}

TEST(TransportLoop, LoopWrapsPosition) {
    yawn::audio::Transport t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.setLoopEnabled(true);
    t.setLoopRange(0.0, 4.0); // loop 0-4 beats

    // At 120 BPM, 4 beats = 2 seconds = 88200 samples
    // Position near loop end
    double spb = t.samplesPerBeat(); // 22050
    t.setPositionInSamples(static_cast<int64_t>(3.99 * spb));
    t.play();
    t.advance(512); // should push past beat 4.0 and wrap

    double beat = t.positionInBeats();
    EXPECT_LT(beat, 1.0); // should have wrapped back near start
    EXPECT_GE(beat, 0.0);
    EXPECT_TRUE(t.didLoopWrap());
}

TEST(TransportLoop, NoWrapWhenDisabled) {
    yawn::audio::Transport t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.setLoopEnabled(false);
    t.setLoopRange(0.0, 4.0);

    double spb = t.samplesPerBeat();
    t.setPositionInSamples(static_cast<int64_t>(3.99 * spb));
    t.play();
    t.advance(512);

    EXPECT_GT(t.positionInBeats(), 4.0); // no wrap
    EXPECT_FALSE(t.didLoopWrap());
}

TEST(TransportLoop, ResetClearsLoop) {
    yawn::audio::Transport t;
    t.setLoopEnabled(true);
    t.setLoopRange(4.0, 8.0);
    t.reset();
    EXPECT_FALSE(t.isLoopEnabled());
    EXPECT_DOUBLE_EQ(t.loopStartBeats(), 0.0);
    EXPECT_DOUBLE_EQ(t.loopEndBeats(), 0.0);
}
