#pragma once

// M3DTransform / M3DInstance / M3DCamera — value types used to pose a 3D
// scene each frame. Extracted into their own header so non-GL code
// (M3DSceneScript in yawn_core) can emit them without pulling in glad.

namespace yawn {
namespace visual {

// Geometric pose of a single instance. Kept as a distinct type because
// the static (@range-uniform) path and older call sites speak in terms
// of a plain pos/rot/scale triple.
struct M3DTransform {
    float position[3]    = { 0.0f, 0.0f, 0.0f };
    float rotationDeg[3] = { 0.0f, 0.0f, 0.0f };   // euler XYZ
    float scale          = 1.0f;                    // uniform scale
};

// A full scene instance: a transform plus per-instance appearance and
// model/animation selection. Scene scripts emit a list of these; the
// static single-model path builds exactly one. All appearance fields
// default to "no change" so a script that only sets a position behaves
// exactly like the old M3DTransform contract.
struct M3DInstance {
    float position[3]    = { 0.0f, 0.0f, 0.0f };
    float rotationDeg[3] = { 0.0f, 0.0f, 0.0f };   // euler XYZ
    float scale          = 1.0f;                    // uniform scale
    float scale3[3]      = { 1.0f, 1.0f, 1.0f };    // per-axis multiplier

    float color[3]       = { 1.0f, 1.0f, 1.0f };    // tint (multiplies base color)
    float emissive       = 0.0f;                    // additive glow (0 = none)
    float opacity        = 1.0f;                    // 1 = opaque

    int   model          = 0;     // index into the clip's model list
    int   animClip       = -1;    // -1 = renderer's active clip
    float animTime       = -1.0f; // < 0 = use the shared frame clock
};

// Camera pose. `explicitCam == false` means "auto-frame the model's
// bounding box" — the historical behaviour, used whenever neither the
// scene script nor the camera @range uniforms opt in.
struct M3DCamera {
    bool  explicitCam = false;
    float pos[3]      = { 0.0f, 0.0f, 3.0f };
    float target[3]   = { 0.0f, 0.0f, 0.0f };
    float fov         = 50.0f;    // vertical field of view, degrees
};

} // namespace visual
} // namespace yawn
