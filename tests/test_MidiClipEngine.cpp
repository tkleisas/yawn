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
