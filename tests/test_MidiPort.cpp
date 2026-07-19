// MidiPort — own-client port-name filter.
//
// Every port we open registers an ALSA client (named "YAWN") whose
// ports show up in the opposite enumeration — our inputs under the
// output list and vice versa (the "RtMidi Input Client" port that
// Preferences → MIDI listed under Outputs). isOwnPortName is what
// filters them back out. Pure function tests only — no RtMidi or
// hardware involved.

#include <gtest/gtest.h>

#include "midi/MidiPort.h"

using yawn::midi::isOwnPortName;

TEST(MidiPortOwnPortFilter, MatchesOwnClientPorts) {
    // ALSA-style "client:port clientNum:portNum" names.
    EXPECT_TRUE(isOwnPortName("YAWN:YAWN 130:0"));
    EXPECT_TRUE(isOwnPortName("YAWN:virtual out 130:1"));
}

TEST(MidiPortOwnPortFilter, KeepsForeignPorts) {
    EXPECT_FALSE(isOwnPortName("RtMidi Input Client:RtMidi Input Client 128:0"));
    EXPECT_FALSE(isOwnPortName("RtMidi Output Client:RtMidi Output Client 129:0"));
    EXPECT_FALSE(isOwnPortName("UM-2:UM-2 MIDI 1 24:0"));
    EXPECT_FALSE(isOwnPortName("Midi Through:Midi Through Port-0 14:0"));
    EXPECT_FALSE(isOwnPortName(""));
}

TEST(MidiPortOwnPortFilter, PrefixBoundaryIsStrict) {
    // "YAWN2" / "YAWNISH" are NOT our client — only "YAWN:" matches.
    EXPECT_FALSE(isOwnPortName("YAWN2:YAWN2 131:0"));
    EXPECT_FALSE(isOwnPortName("YAWNISH:something 131:0"));
}
