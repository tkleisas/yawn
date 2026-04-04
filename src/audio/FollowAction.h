#pragma once

#include <cstdint>

namespace yawn {

// Follow action types for clip launch chaining
enum class FollowActionType : uint8_t {
    None,       // No action — clip loops normally
    Stop,       // Stop playback
    PlayAgain,  // Restart this clip
    Next,       // Launch next clip in track (wraps)
    Previous,   // Launch previous clip in track (wraps)
    First,      // Launch first clip in track
    Last,       // Launch last clip in track
    Random,     // Launch a random clip in track
    Any         // Launch any other clip in track (excludes self)
};

// Follow action configuration for a clip slot
struct FollowAction {
    bool enabled = false;
    int  barCount = 1;               // Play for N bars before triggering
    FollowActionType actionA = FollowActionType::Next;
    FollowActionType actionB = FollowActionType::None;
    int  chanceA = 100;              // Probability 0-100 for action A (B = 100 - A)

    int chanceB() const { return 100 - chanceA; }
};

} // namespace yawn
