#include <gtest/gtest.h>

#include "midi/MidiEngine.h"

using namespace yawn::midi;

namespace {
MpeConfig lowerZoneConfig() {
    MpeConfig config;
    config.enableLowerZone(15, 48.0f);
    return config;
}
} // namespace

TEST(Mpe, TranslatesMemberBendToPerNote) {
    MpeConfig config = lowerZoneConfig();
    MidiEngine::MpeExpressionState state;
    MidiBuffer buffer;
    buffer.addMessage(MidiMessage::noteOn(1, 60, 100));
    buffer.addMessage(MidiMessage::pitchBend(1, 12288));
    buffer.addMessage(MidiMessage::noteOff(1, 60));

    MidiEngine::translateMpe(buffer, config, state);

    ASSERT_EQ(3, buffer.count());
    EXPECT_EQ(MidiMessage::Type::NoteOn, buffer[0].type);
    EXPECT_EQ(MidiMessage::Type::PerNotePitchBend, buffer[1].type);
    EXPECT_EQ(1, buffer[1].channel);
    EXPECT_EQ(60, buffer[1].note);
    EXPECT_EQ(MidiMessage::Type::NoteOff, buffer[2].type);
}

TEST(Mpe, DropsBendWithoutActiveNote) {
    MpeConfig config = lowerZoneConfig();
    MidiEngine::MpeExpressionState state;
    MidiBuffer buffer;
    buffer.addMessage(MidiMessage::pitchBend(2, 10000));

    MidiEngine::translateMpe(buffer, config, state);
    EXPECT_EQ(0, buffer.count());
}

TEST(Mpe, MasterChannelBendRemainsChannelBend) {
    MpeConfig config = lowerZoneConfig();
    MidiEngine::MpeExpressionState state;
    MidiBuffer buffer;
    buffer.addMessage(MidiMessage::pitchBend(0, 9000));

    MidiEngine::translateMpe(buffer, config, state);
    ASSERT_EQ(1, buffer.count());
    EXPECT_EQ(MidiMessage::Type::PitchBend, buffer[0].type);
    EXPECT_EQ(0, buffer[0].channel);
}

TEST(Mpe, NoteOffClearsActiveNote) {
    MpeConfig config = lowerZoneConfig();
    MidiEngine::MpeExpressionState state;
    MidiBuffer buffer;
    buffer.addMessage(MidiMessage::noteOn(3, 72, 100));
    buffer.addMessage(MidiMessage::noteOff(3, 72));
    buffer.addMessage(MidiMessage::pitchBend(3, 9000));

    MidiEngine::translateMpe(buffer, config, state);
    ASSERT_EQ(2, buffer.count());
    EXPECT_EQ(MidiMessage::Type::NoteOn, buffer[0].type);
    EXPECT_EQ(MidiMessage::Type::NoteOff, buffer[1].type);
}

TEST(Mpe, UpperZoneMembershipUsesDescendingChannels) {
    MpeZone upper;
    upper.masterChannel = 15;
    upper.enabled = true;
    upper.memberChannelCount = 4;

    EXPECT_TRUE(upper.isMemberChannel(11));
    EXPECT_TRUE(upper.isMemberChannel(14));
    EXPECT_FALSE(upper.isMemberChannel(10));
    EXPECT_FALSE(upper.isMemberChannel(15));
    EXPECT_TRUE(upper.isMasterChannel(15));
}
