#pragma once
// fw2::MultisamplerZoneRow — view-model struct shared by the
// MultisamplerDisplayPanel and the MultisamplerZoneMapWidget. Lives
// in its own header purely to break the otherwise-circular include
// (panel hosts the map widget; map widget needs the row type).
//
// Field-for-field mirror of `Multisampler::Zone` minus the audio
// payload — we don't want a full Zone copy in the host's per-frame
// snapshot, just the metadata that the editors care about.

#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

struct MultisamplerZoneRow {
    int   rootNote     = 60;
    int   lowKey       = 0;
    int   highKey      = 127;
    int   lowVel       = 1;
    int   highVel      = 127;
    float tune         = 0.0f;
    float volume       = 1.0f;
    float pan          = 0.0f;
    bool  loop         = false;
    // Display-only — basename of the source file or "<auto-sampled>".
    std::string filename;
    int   sampleFrames = 0;
};

inline bool operator==(const MultisamplerZoneRow& a,
                       const MultisamplerZoneRow& b) {
    return a.rootNote == b.rootNote && a.lowKey == b.lowKey &&
           a.highKey  == b.highKey  && a.lowVel == b.lowVel &&
           a.highVel  == b.highVel  && a.tune   == b.tune   &&
           a.volume   == b.volume   && a.pan    == b.pan    &&
           a.loop     == b.loop     && a.filename == b.filename &&
           a.sampleFrames == b.sampleFrames;
}
inline bool operator!=(const MultisamplerZoneRow& a,
                       const MultisamplerZoneRow& b) {
    return !(a == b);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
