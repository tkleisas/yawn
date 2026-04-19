#pragma once

// M3DSceneScript — per-clip Lua script that returns a list of 3D
// transforms each frame. Layered on top of the static-transform
// pipeline from G.4: absent script = the normal single-instance path
// driven by @range uniforms; present script = multi-instance, where
// the engine draws the clip's primary model once for every entry the
// script yields.
//
// Contract (for now):
//
//   function tick(ctx)
//     -- ctx is a Lua table with read-only fields:
//     --   ctx.time        (float, wall-clock seconds)
//     --   ctx.beat        (float, transport beat position)
//     --   ctx.playing     (bool)
//     --   ctx.audio.level (float, 0..1 smoothed)
//     --   ctx.audio.low   (float)
//     --   ctx.audio.mid   (float)
//     --   ctx.audio.high  (float)
//     --   ctx.audio.kick  (float, peak-triggered, decays)
//     --   ctx.knobs.A ..  ctx.knobs.H    (floats, 0..1)
//     return { { position = {x, y, z},
//               rotation = {x, y, z},     -- euler XYZ degrees
//               scale    = s } ,
//               ... }
//   end
//
// Omitted fields default to position={0,0,0}, rotation={0,0,0}, scale=1.
// A plain `return { ... }` with no entries means "draw nothing this
// frame" — useful for gating on audio peaks.
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
    // cleared and filled with the transforms the script returned. On
    // error `out` is cleared and error() is set; the script's state is
    // left usable for the next call (runtime errors don't disable the
    // state — reload is controlled explicitly via pollHotReload).
    bool tick(const Inputs& in, std::vector<M3DTransform>& out);

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
