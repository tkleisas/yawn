#include <gtest/gtest.h>
#include "audio/MidiClipEngine.h"
#include "audio/Transport.h"
#include "midi/MidiClip.h"
#include "midi/MidiTypes.h"
#include <cmath>

using namespace yawn;
using namespace yawn::audio;
using namespace yawn::midi;

class MidiClipEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_transport.reset();
        m_transport.setBPM(120.0);
        m_transport.setSampleRate(44100.0);

        m_engine.setTransport(&m_transport);
        m_engine.setSampleRate(44100.0);

        for (int i = 0; i < kMaxTracks; ++i)
            m_buffers[i].clear();
    }

    Transport m_transport;
    MidiClipEngine m_engine;
    MidiBuffer m_buffers[kMaxTracks];
};

TEST_F(MidiClipEngineTest, InitiallyNoTracksPlaying) {
    for (int t = 0; t < kMaxTracks; ++t)
        EXPECT_FALSE(m_engine.isTrackPlaying(t));
}

TEST_F(MidiClipEngineTest, LaunchClipMakesTrackActive) {
    MidiClip clip;
    clip.setLengthBeats(4.0);
    clip.addNote({0.0, 1.0, 60, 0, 16000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();

    EXPECT_TRUE(m_engine.isTrackPlaying(0));
    EXPECT_FALSE(m_engine.isTrackPlaying(1));
}

TEST_F(MidiClipEngineTest, StopClipDeactivatesTrack) {
    MidiClip clip;
    clip.setLengthBeats(4.0);

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    EXPECT_TRUE(m_engine.isTrackPlaying(0));

    m_engine.scheduleStop(0, QuantizeMode::None);
    m_engine.checkAndFirePending();
    EXPECT_FALSE(m_engine.isTrackPlaying(0));
}

TEST_F(MidiClipEngineTest, EmitsNoteOnForNoteInBuffer) {
    MidiClip clip;
    clip.setLengthBeats(4.0);
    // Note at beat 0, velocity ~100 in 7-bit (100 << 9 = 51200)
    clip.addNote({0.0, 1.0, 60, 0, 51200, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();

    int numFrames = 256;
    m_transport.play();
    m_engine.process(m_buffers, numFrames);

    // Should have at least 1 NoteOn
    bool foundNoteOn = false;
    for (int i = 0; i < m_buffers[0].count(); ++i) {
        const auto& msg = m_buffers[0][i];
        if (msg.type == MidiMessage::Type::NoteOn && msg.note == 60) {
            foundNoteOn = true;
            EXPECT_EQ(msg.channel, 0);
            EXPECT_GT(msg.velocity, static_cast<uint16_t>(0));
        }
    }
    EXPECT_TRUE(foundNoteOn);
}

// Regression: two same-pitch notes back-to-back must re-articulate.
// When a buffer boundary doesn't align with a beat (the common case),
// Note-A.endBeat == Note-B.startBeat puts both events at the same
// frame in the same buffer. If the Note-On is emitted before the
// Note-Off, the synth allocates a new voice for B then the delayed
// Note-Off matching pitch P kills the new voice instead of A's
// lingering one — back-to-back notes sound like one held note.
//
// Fix is in MidiClipEngine::scanAndEmit: Note-Offs are emitted in a
// pass before Note-Ons so the buffer always lists offs first at any
// given frame. This test pins that ordering invariant down.
TEST_F(MidiClipEngineTest, BackToBackSamePitchEmitsOffBeforeOn) {
    MidiClip clip;
    clip.setLengthBeats(4.0);
    // Two same-pitch notes joined at beat 0.1.
    //   Note A: [0.0, 0.1)  → noteOff at 0.1
    //   Note B: [0.1, 0.2)  → noteOn  at 0.1
    // We pick a short note span so a single 4096-frame buffer at
    // 120 BPM / 44.1 kHz (≈ 0.186 beats per buffer) covers both
    // events — that's exactly the scenario where the buffer boundary
    // doesn't align with a beat, so the off + on can collide on the
    // same frame.
    clip.addNote({0.0, 0.1, 60, 0, 32000, 0, 0, 0, 0});
    clip.addNote({0.1, 0.1, 60, 0, 32000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    const int numFrames = 4096;
    m_engine.process(m_buffers, numFrames);

    // Find the indices of the offending Note-Off (note 60) and the
    // re-articulation Note-On (also note 60) in the emitted buffer.
    int offIdx = -1, onIdx = -1;
    int offCount = 0, onCount = 0;
    for (int i = 0; i < m_buffers[0].count(); ++i) {
        const auto& msg = m_buffers[0][i];
        if (msg.note != 60) continue;
        if (msg.type == MidiMessage::Type::NoteOff) {
            ++offCount;
            if (offIdx < 0) offIdx = i;
        } else if (msg.type == MidiMessage::Type::NoteOn) {
            ++onCount;
            // The re-articulation is the SECOND note-on (the first is
            // the kick-off at beat 0). Track the index of the last
            // note-on we see — that's the re-articulation.
            onIdx = i;
        }
    }

    // Both events must have been emitted.
    ASSERT_GE(offCount, 1) << "Note A's noteOff was not emitted";
    ASSERT_GE(onCount,  2) << "Expected two noteOns (initial + re-articulation)";

    // The critical invariant: at the boundary, the noteOff (A's end)
    // must come BEFORE the noteOn (B's start) in the buffer so the
    // synth releases A's voice before allocating B's. If on-then-off,
    // the synth would kill the freshly-allocated B voice.
    EXPECT_LT(offIdx, onIdx)
        << "Note-Off must precede the second Note-On in the buffer "
           "for back-to-back same-pitch notes to re-articulate. "
           "offIdx=" << offIdx << " onIdx=" << onIdx;
}

TEST_F(MidiClipEngineTest, EmitsNoteOffWhenNoteEnds) {
    MidiClip clip;
    clip.setLengthBeats(4.0);
    // Short note: starts at beat 0, duration 0.1 beats
    // At 120 BPM, 0.1 beats = 0.05 seconds = 2205 samples
    clip.addNote({0.0, 0.1, 72, 0, 32000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();

    // Process enough frames for note to end
    int numFrames = 4096;
    m_transport.play();
    m_engine.process(m_buffers, numFrames);

    bool foundNoteOff = false;
    for (int i = 0; i < m_buffers[0].count(); ++i) {
        const auto& msg = m_buffers[0][i];
        if (msg.type == MidiMessage::Type::NoteOff && msg.note == 72) {
            foundNoteOff = true;
        }
    }
    EXPECT_TRUE(foundNoteOff);
}

TEST_F(MidiClipEngineTest, LoopWrapsCorrectly) {
    MidiClip clip;
    clip.setLengthBeats(1.0);
    clip.setLoop(true);
    clip.addNote({0.0, 0.25, 60, 0, 32000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    // At 120 BPM, 1 beat = 22050 samples. 4096 frames ≈ 0.186 beats.
    // Process enough buffers to cover several full loops.
    int totalNoteOns = 0;
    for (int buf = 0; buf < 200; ++buf) {
        m_buffers[0].clear();
        m_engine.process(m_buffers, 4096);
        for (int i = 0; i < m_buffers[0].count(); ++i) {
            if (m_buffers[0][i].type == MidiMessage::Type::NoteOn &&
                m_buffers[0][i].note == 60)
                totalNoteOns++;
        }
    }
    // 200 * 4096 = 819200 samples ≈ 37.1 beats → ~37 loops → ~37 note-ons
    EXPECT_GE(totalNoteOns, 20);
}

TEST_F(MidiClipEngineTest, NonLoopingClipStopsAtEnd) {
    MidiClip clip;
    clip.setLengthBeats(1.0);
    clip.setLoop(false);
    clip.addNote({0.0, 0.5, 60, 0, 32000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    // At 120 BPM, 1 beat = 22050 samples. Process 30000 samples to pass end.
    for (int buf = 0; buf < 10; ++buf) {
        m_buffers[0].clear();
        m_engine.process(m_buffers, 4096);
    }

    EXPECT_FALSE(m_engine.isTrackPlaying(0));
}

TEST_F(MidiClipEngineTest, CCEventsEmitted) {
    MidiClip clip;
    clip.setLengthBeats(4.0);
    // CC event at beat 0
    clip.addCC({0.0, 1, 0x40000000, 0}); // CC1, value ~50%

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    m_engine.process(m_buffers, 256);

    bool foundCC = false;
    for (int i = 0; i < m_buffers[0].count(); ++i) {
        const auto& msg = m_buffers[0][i];
        if (msg.type == MidiMessage::Type::ControlChange) {
            foundCC = true;
        }
    }
    EXPECT_TRUE(foundCC);
}

TEST_F(MidiClipEngineTest, MultipleTracksIndependent) {
    MidiClip clipA, clipB;
    clipA.setLengthBeats(4.0);
    clipA.addNote({0.0, 1.0, 60, 0, 32000, 0, 0, 0, 0});
    clipB.setLengthBeats(4.0);
    clipB.addNote({0.0, 1.0, 72, 0, 32000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clipA, QuantizeMode::None);
    m_engine.scheduleClip(1, 0, &clipB, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    m_engine.process(m_buffers, 256);

    // Track 0 should have note 60, track 1 should have note 72
    bool track0Has60 = false, track1Has72 = false;
    for (int i = 0; i < m_buffers[0].count(); ++i)
        if (m_buffers[0][i].type == MidiMessage::Type::NoteOn && m_buffers[0][i].note == 60)
            track0Has60 = true;
    for (int i = 0; i < m_buffers[1].count(); ++i)
        if (m_buffers[1][i].type == MidiMessage::Type::NoteOn && m_buffers[1][i].note == 72)
            track1Has72 = true;

    EXPECT_TRUE(track0Has60);
    EXPECT_TRUE(track1Has72);
}

TEST_F(MidiClipEngineTest, OutOfRangeTrackIndexIgnored) {
    MidiClip clip;
    m_engine.scheduleClip(-1, 0, &clip, QuantizeMode::None);
    m_engine.scheduleClip(kMaxTracks, 0, &clip, QuantizeMode::None);
    m_engine.scheduleStop(-1, QuantizeMode::None);
    m_engine.scheduleStop(kMaxTracks, QuantizeMode::None);
    // Should not crash
    m_engine.checkAndFirePending();
    m_engine.process(m_buffers, 256);
}

TEST_F(MidiClipEngineTest, NoteFrameOffsetIsReasonable) {
    MidiClip clip;
    clip.setLengthBeats(4.0);
    // Note at beat 0.5 — at 120 BPM, 0.5 beats = 0.25s = 11025 samples
    clip.addNote({0.5, 0.5, 64, 0, 32000, 0, 0, 0, 0});

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    // Use large buffer that contains beat 0.5
    int numFrames = 16384;
    m_engine.process(m_buffers, numFrames);

    for (int i = 0; i < m_buffers[0].count(); ++i) {
        const auto& msg = m_buffers[0][i];
        if (msg.type == MidiMessage::Type::NoteOn && msg.note == 64) {
            // At 120 BPM, beat 0.5 ≈ sample 11025
            EXPECT_GE(msg.frameOffset, 10000);
            EXPECT_LE(msg.frameOffset, 12000);
            break;
        }
    }
}

TEST_F(MidiClipEngineTest, LoopRespectsLoopStartBeat) {
    // Clip: 4 beats, loopStart=2. Notes at beat 0 and beat 2.5.
    // After first play-through reaches beat 4, it should wrap to beat 2 (not 0).
    // So on subsequent loops, only the note at beat 2.5 should fire (not beat 0).
    MidiClip clip;
    clip.setLengthBeats(4.0);
    clip.setLoop(true);
    clip.setLoopStartBeat(2.0);
    clip.addNote({0.0, 0.25, 60, 0, 32000, 0, 0, 0, 0});  // intro-only note
    clip.addNote({2.5, 0.25, 72, 0, 32000, 0, 0, 0, 0});  // loop-region note

    m_engine.scheduleClip(0, 0, &clip, QuantizeMode::None);
    m_engine.checkAndFirePending();
    m_transport.play();

    // At 120 BPM, 1 beat = 22050 samples. 4 beats = 88200 samples.
    // Process enough to cover several loops.
    int noteOn60 = 0, noteOn72 = 0;
    for (int buf = 0; buf < 200; ++buf) {
        m_buffers[0].clear();
        m_engine.process(m_buffers, 4096);
        m_transport.advance(4096);
        for (int i = 0; i < m_buffers[0].count(); ++i) {
            if (m_buffers[0][i].type == MidiMessage::Type::NoteOn) {
                if (m_buffers[0][i].note == 60) noteOn60++;
                if (m_buffers[0][i].note == 72) noteOn72++;
            }
        }
    }
    // Note 60 at beat 0 should fire exactly once (first play-through only)
    EXPECT_EQ(noteOn60, 1);
    // Note 72 at beat 2.5 should fire many times (once per loop)
    EXPECT_GE(noteOn72, 10);
}