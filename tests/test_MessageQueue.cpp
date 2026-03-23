#include <gtest/gtest.h>
#include "util/MessageQueue.h"

using namespace yawn::audio;

TEST(MessageQueueTest, CommandQueuePushPop) {
    CommandQueue q;
    EXPECT_TRUE(q.empty());

    AudioCommand cmd = TransportPlayMsg{};
    EXPECT_TRUE(q.push(cmd));
    EXPECT_FALSE(q.empty());

    AudioCommand out;
    EXPECT_TRUE(q.pop(out));
    EXPECT_TRUE(std::holds_alternative<TransportPlayMsg>(out));
}

TEST(MessageQueueTest, EventQueuePushPop) {
    EventQueue q;
    AudioEvent evt = TransportPositionUpdate{44100, 2.0, true};
    EXPECT_TRUE(q.push(evt));

    AudioEvent out;
    EXPECT_TRUE(q.pop(out));
    ASSERT_TRUE(std::holds_alternative<TransportPositionUpdate>(out));

    auto& pos = std::get<TransportPositionUpdate>(out);
    EXPECT_EQ(pos.positionInSamples, 44100);
    EXPECT_DOUBLE_EQ(pos.positionInBeats, 2.0);
    EXPECT_TRUE(pos.isPlaying);
}

TEST(MessageQueueTest, TransportStopMsg) {
    CommandQueue q;
    q.push(AudioCommand{TransportStopMsg{}});
    AudioCommand out;
    q.pop(out);
    EXPECT_TRUE(std::holds_alternative<TransportStopMsg>(out));
}

TEST(MessageQueueTest, SetBPMMsg) {
    CommandQueue q;
    q.push(AudioCommand{TransportSetBPMMsg{140.0}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<TransportSetBPMMsg>(out));
    EXPECT_DOUBLE_EQ(std::get<TransportSetBPMMsg>(out).bpm, 140.0);
}

TEST(MessageQueueTest, SetPositionMsg) {
    CommandQueue q;
    q.push(AudioCommand{TransportSetPositionMsg{88200}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<TransportSetPositionMsg>(out));
    EXPECT_EQ(std::get<TransportSetPositionMsg>(out).positionInSamples, 88200);
}

TEST(MessageQueueTest, TestToneMsg) {
    CommandQueue q;
    q.push(AudioCommand{TestToneMsg{true, 440.0f}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<TestToneMsg>(out));
    EXPECT_TRUE(std::get<TestToneMsg>(out).enabled);
    EXPECT_FLOAT_EQ(std::get<TestToneMsg>(out).frequency, 440.0f);
}

TEST(MessageQueueTest, LaunchClipMsg) {
    Clip clip;
    clip.name = "test";

    CommandQueue q;
    q.push(AudioCommand{LaunchClipMsg{3, &clip}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<LaunchClipMsg>(out));
    EXPECT_EQ(std::get<LaunchClipMsg>(out).trackIndex, 3);
    EXPECT_EQ(std::get<LaunchClipMsg>(out).clip, &clip);
}

TEST(MessageQueueTest, StopClipMsg) {
    CommandQueue q;
    q.push(AudioCommand{StopClipMsg{5}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<StopClipMsg>(out));
    EXPECT_EQ(std::get<StopClipMsg>(out).trackIndex, 5);
}

TEST(MessageQueueTest, SetQuantizeMsg) {
    CommandQueue q;
    q.push(AudioCommand{SetQuantizeMsg{QuantizeMode::NextBeat}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<SetQuantizeMsg>(out));
    EXPECT_EQ(std::get<SetQuantizeMsg>(out).mode, QuantizeMode::NextBeat);
}

TEST(MessageQueueTest, ClipStateUpdateEvent) {
    EventQueue q;
    q.push(AudioEvent{ClipStateUpdate{2, true, 12345}});
    AudioEvent out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<ClipStateUpdate>(out));

    auto& s = std::get<ClipStateUpdate>(out);
    EXPECT_EQ(s.trackIndex, 2);
    EXPECT_TRUE(s.playing);
    EXPECT_EQ(s.playPosition, 12345);
}

TEST(MessageQueueTest, MultipleCommandsFIFO) {
    CommandQueue q;
    q.push(AudioCommand{TransportPlayMsg{}});
    q.push(AudioCommand{TransportSetBPMMsg{180.0}});
    q.push(AudioCommand{TransportStopMsg{}});

    AudioCommand out;
    q.pop(out);
    EXPECT_TRUE(std::holds_alternative<TransportPlayMsg>(out));
    q.pop(out);
    EXPECT_TRUE(std::holds_alternative<TransportSetBPMMsg>(out));
    q.pop(out);
    EXPECT_TRUE(std::holds_alternative<TransportStopMsg>(out));
}

TEST(MessageQueueTest, StartAudioRecordMsg) {
    CommandQueue q;
    q.push(AudioCommand{StartAudioRecordMsg{2, 3}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<StartAudioRecordMsg>(out));
    auto& msg = std::get<StartAudioRecordMsg>(out);
    EXPECT_EQ(msg.trackIndex, 2);
    EXPECT_EQ(msg.sceneIndex, 3);
}

TEST(MessageQueueTest, StopAudioRecordMsg) {
    CommandQueue q;
    q.push(AudioCommand{StopAudioRecordMsg{5}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<StopAudioRecordMsg>(out));
    EXPECT_EQ(std::get<StopAudioRecordMsg>(out).trackIndex, 5);
}

TEST(MessageQueueTest, AudioRecordCompleteEvent) {
    EventQueue q;
    q.push(AudioEvent{AudioRecordCompleteEvent{1, 2, 44100}});
    AudioEvent out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<AudioRecordCompleteEvent>(out));
    auto& evt = std::get<AudioRecordCompleteEvent>(out);
    EXPECT_EQ(evt.trackIndex, 1);
    EXPECT_EQ(evt.sceneIndex, 2);
    EXPECT_EQ(evt.frameCount, 44100);
}

TEST(MessageQueueTest, StartMidiRecordMsg) {
    CommandQueue q;
    q.push(AudioCommand{StartMidiRecordMsg{0, 1, true}});
    AudioCommand out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<StartMidiRecordMsg>(out));
    auto& msg = std::get<StartMidiRecordMsg>(out);
    EXPECT_EQ(msg.trackIndex, 0);
    EXPECT_EQ(msg.sceneIndex, 1);
    EXPECT_TRUE(msg.overdub);
}

TEST(MessageQueueTest, MidiRecordCompleteEvent) {
    EventQueue q;
    q.push(AudioEvent{MidiRecordCompleteEvent{3, 0, 42}});
    AudioEvent out;
    q.pop(out);
    ASSERT_TRUE(std::holds_alternative<MidiRecordCompleteEvent>(out));
    auto& evt = std::get<MidiRecordCompleteEvent>(out);
    EXPECT_EQ(evt.trackIndex, 3);
    EXPECT_EQ(evt.sceneIndex, 0);
    EXPECT_EQ(evt.noteCount, 42);
}
