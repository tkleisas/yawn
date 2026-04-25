#pragma once

// VisualClip — a session-slot clip that references a fragment shader file on
// disk. Launching the clip tells VisualEngine to load that shader; no audio
// engine involvement. Modeled loosely on audio::Clip / midi::MidiClip.

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <array>
#include <cstdint>

namespace yawn {
namespace visual {

// Persisted per-knob LFO state. Parallel to VisualLFO but without the
// per-frame evaluation state, so it can be serialised cleanly.
struct SavedKnobLFO {
    bool    enabled = false;
    uint8_t shape   = 0;     // VisualLFO::Shape
    float   rate    = 1.0f;
    float   depth   = 0.3f;
    bool    sync    = true;
};

// ShaderPass — one stage in a clip's shader chain. The first pass
// reads the clip's iChannel0..3 inputs (audio / text / video / dummy)
// and a black `iPrev`. Each subsequent pass receives the previous
// pass's output via `iPrev` while still seeing the original iChannel
// inputs unchanged. paramValues are scoped to the pass — only this
// pass's @range uniforms are looked up here.
struct ShaderPass {
    std::string shaderPath;
    std::vector<std::pair<std::string, float>> paramValues;
};

struct VisualClip {
    // Pass-0 storage. Kept as plain fields (rather than tucking them
    // into shaderChain[0]) so the dozens of existing call sites that
    // read or write `vc->shaderPath` / `vc->paramValues` keep working
    // verbatim. Additional passes go in `additionalPasses` below.
    std::string shaderPath;         // filesystem path to the .frag
    std::string name;               // display name (defaults to filename stem)
    int         colorIndex = 0;     // UI accent colour
    double      lengthBeats = 4.0;  // nominal length (for future arrangement use)

    // Which track's audio drives iAudioLevel in the shader.
    //   -1 = master (default)
    //   0..N-1 = track index
    int audioSource = -1;

    // Persisted shader parameter values (by uniform name) for pass 0.
    // Looked up by name when the clip is launched — any param names
    // not in this list start at their shader-declared default.
    std::vector<std::pair<std::string, float>> paramValues;

    // Extra shader passes after pass 0. The full conceptual chain is
    // {shaderPath, paramValues} ++ additionalPasses. Empty (default)
    // means single-pass behaviour, identical to what existed before
    // chains were added. The engine + UI iterate this via the
    // shaderChain() helper below.
    std::vector<ShaderPass> additionalPasses;

    // Conceptual full chain — pass 0 inlined from the legacy fields,
    // then any extras. Returned by value to keep the API consistent
    // regardless of where each pass lives. Cheap (small vector).
    std::vector<ShaderPass> shaderChain() const {
        std::vector<ShaderPass> out;
        out.reserve(1 + additionalPasses.size());
        if (!shaderPath.empty() || !paramValues.empty()) {
            out.push_back(ShaderPass{shaderPath, paramValues});
        } else if (!additionalPasses.empty()) {
            // Legacy field is empty but extras exist (unusual but
            // possible during in-flight edits) — emit an empty pass
            // 0 so chain indices stay stable.
            out.push_back(ShaderPass{});
        }
        for (auto& p : additionalPasses) out.push_back(p);
        return out;
    }
    int passCount() const {
        if (!shaderPath.empty()) return 1 + (int)additionalPasses.size();
        return additionalPasses.empty() ? 0 : 1 + (int)additionalPasses.size();
    }

    // Per-A..H-knob LFO state (8 slots, index = knob index 0..7).
    std::array<SavedKnobLFO, 8> knobLFOs{};

    // Text strip — rendered to an R8 texture and bound as iChannel1.
    // Shaders sample texture(iChannel1, uv).r as alpha and compose freely.
    // Empty text = texture stays blank.
    std::string text;

    // Optional video file path (an imported/transcoded .mp4 in the
    // project's media folder). When non-empty, the layer decodes that
    // file and makes its frames available as iChannel2.
    //
    // If the clip has no shaderPath, the engine auto-falls back to
    // assets/shaders/video_passthrough.frag. Users can also point
    // shaderPath at a custom shader that samples iChannel2 for effects.
    std::string videoPath;
    // 160×90 JPEG snapshot of the video's first frame, produced by the
    // importer. Rendered as a background on the session-grid cell so
    // clips are visually identifiable at a glance.
    std::string thumbnailPath;

    // Absolute path of the original source video the user imported.
    // Kept so "Re-import" can re-transcode without re-prompting the user.
    std::string videoSourcePath;

    // Video playback controls (F.2, F.3).
    //   videoLoopBars  : 0  = free-running at the native 30 fps (loops when
    //                          the source ends); >0 = playback rate is
    //                          stretched so the clip loops exactly every
    //                          `videoLoopBars` bars of transport time.
    //   videoRate      : playback rate multiplier (1.0 = native).
    int   videoLoopBars = 0;
    float videoRate     = 1.0f;
    //   videoIn / videoOut : 0..1 fractions marking the playable sub-range
    //                         within the source. videoIn=0 videoOut=1 = whole clip.
    float videoIn       = 0.0f;
    float videoOut      = 1.0f;

    // Live video input (webcam, RTSP, etc). When `liveInput` is true the
    // layer ignores videoPath/loop/rate/in-out and instead opens `liveUrl`
    // through libavformat (+ libavdevice). Any URL libav can demux works:
    //   v4l2:///dev/video0, avfoundation://..., rtsp://..., http://..., ...
    bool        liveInput = false;
    std::string liveUrl;

    // 3D model (glTF / glb) rendered into the layer's iChannel2 slot —
    // mutually exclusive with videoPath / liveUrl at the engine level.
    // Transform (position, rotation, scale) is driven by shader @range
    // uniforms named modelPosX/Y/Z, modelRotX/Y/Z, modelScale, so A..H
    // knobs and LFOs can modulate it with no extra plumbing.
    std::string modelPath;        // project-relative or absolute
    std::string modelSourcePath;  // original path the user imported from

    // Optional Lua scene script. When non-empty and a model is loaded,
    // the engine calls script.tick() each frame to get the list of
    // transforms used for this layer's model; otherwise the static
    // @range-uniform path runs.
    std::string scenePath;        // project-relative or absolute

    std::unique_ptr<VisualClip> clone() const {
        auto c = std::make_unique<VisualClip>();
        c->shaderPath  = shaderPath;
        c->name        = name;
        c->colorIndex  = colorIndex;
        c->lengthBeats = lengthBeats;
        c->audioSource = audioSource;
        c->paramValues = paramValues;
        c->knobLFOs    = knobLFOs;
        c->text          = text;
        c->videoPath       = videoPath;
        c->thumbnailPath   = thumbnailPath;
        c->videoSourcePath = videoSourcePath;
        c->videoLoopBars   = videoLoopBars;
        c->videoRate       = videoRate;
        c->videoIn         = videoIn;
        c->videoOut        = videoOut;
        c->liveInput       = liveInput;
        c->liveUrl         = liveUrl;
        c->modelPath       = modelPath;
        c->modelSourcePath = modelSourcePath;
        c->scenePath       = scenePath;
        c->additionalPasses = additionalPasses;
        return c;
    }
};

} // namespace visual
} // namespace yawn
