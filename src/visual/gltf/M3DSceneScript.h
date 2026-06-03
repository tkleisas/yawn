#pragma once

// M3DSceneScript — per-clip Lua script that returns a list of 3D
// transforms each frame. Layered on top of the static-transform
// pipeline from G.4: absent script = the normal single-instance path
// driven by @range uniforms; present script = multi-instance, where
// the engine draws the clip's primary model once for every entry the
// script yields.
//
// Contract:
//
//   function tick(ctx)
//     -- ctx is a Lua table with read-only fields:
//     --   ctx.time        (float, wall-clock seconds)
//     --   ctx.beat        (float, transport beat position)
//     --   ctx.playing     (bool)
//     --   ctx.audio.level (float, 0..1 smoothed)
//     --   ctx.audio.low / mid / high (float)
//     --   ctx.audio.kick  (float, peak-triggered, decays)
//     --   ctx.knobs.A ..  ctx.knobs.H    (floats, 0..1)
//     --   ctx.notes  — array of recent note-ons (all tracks), each
//     --                { track, channel, pitch, vel (0..1), age (sec) },
//     --                newest last. Filter by pitch/track, fade by age.
//     return { { model    = 0,            -- index into the clip's model list
//                position = {x, y, z},
//                rotation = {x, y, z},     -- euler XYZ degrees
//                scale    = s,             -- number, OR {sx, sy, sz} per-axis
//                color    = {r, g, b},     -- tint, 0..1 (default white)
//                emissive = e,             -- additive glow (default 0)
//                opacity  = o,             -- 0..1 (default 1)
//                anim     = { clip = 1, time = t } },  -- per-instance anim
//               ... },
//            { camera = { pos = {x, y, z},      -- optional 2nd return value
//                         target = {x, y, z},
//                         fov = 50 } }
//   end
//
// Every field is optional. The instance list may also be a single table
// (treated as one instance) for the simplest scripts. A plain `return {}`
// means "draw nothing this frame" — useful for gating on audio peaks. The
// optional second return value carries scene-wide options (currently just
// `camera`); when it sets a camera, that overrides the @range camera
// uniforms for this frame.
//
// No GL dependencies — safe for yawn_core. Lua 5.4 is already vendored
// by YAWN for controller scripting, so no new third-party dep.

#include "visual/gltf/M3DTransform.h"

#include <filesystem>
#include <string>
#include <vector>

struct lua_State;

namespace yawn {
namespace visual {

class M3DSceneScript {
public:
    // A recent note-on, surfaced to the script as one entry in ctx.notes.
    struct Note {
        int   track   = 0;
        int   channel = 0;
        int   pitch   = 0;
        float vel     = 0.0f;   // 0..1
        float age     = 0.0f;   // seconds since the note fired
    };

    struct Inputs {
        float time        = 0.0f;
        float beat        = 0.0f;
        bool  playing     = false;
        float audioLevel  = 0.0f;
        float audioLow    = 0.0f;
        float audioMid    = 0.0f;
        float audioHigh   = 0.0f;
        float kick        = 0.0f;
        float knobs[8]    = { 0, 0, 0, 0, 0, 0, 0, 0 };
        // Recent note-ons (all tracks), newest last. The script filters
        // by track/channel/pitch and fades by age. See VisualNoteBus.
        std::vector<Note> notes;
    };

    M3DSceneScript() = default;
    ~M3DSceneScript();

    M3DSceneScript(const M3DSceneScript&) = delete;
    M3DSceneScript& operator=(const M3DSceneScript&) = delete;

    // Load the script from `path`. On parse / runtime error, state is
    // retained as invalid and error() returns a human-readable message.
    bool load(const std::string& path);

    // Re-check file mtime and reload if it changed. Cheap — stats the
    // file each call, re-parses only when the timestamp differs. Safe
    // to call every frame.
    void pollHotReload();

    bool isValid() const { return m_L != nullptr && m_error.empty(); }
    const std::string& error() const { return m_error; }
    const std::string& path()  const { return m_path; }

    // Invoke tick(ctx) with the given inputs. On success `out` is
    // cleared and filled with the instances the script returned, and if
    // `outCamera` is non-null and the script returned a camera, it is
    // filled and its explicitCam flag set. On error `out` is cleared and
    // error() is set; the script's state is left usable for the next call
    // (runtime errors don't disable the state — reload is controlled
    // explicitly via pollHotReload).
    bool tick(const Inputs& in, std::vector<M3DInstance>& out,
              M3DCamera* outCamera = nullptr);

private:
    void shutdown();

    lua_State*                       m_L = nullptr;
    std::string                      m_path;
    std::filesystem::file_time_type  m_mtime{};
    bool                             m_mtimeValid = false;
    std::string                      m_error;
};

} // namespace visual
} // namespace yawn
