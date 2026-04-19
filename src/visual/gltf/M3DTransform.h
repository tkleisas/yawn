#pragma once

// M3DTransform — pos/rot/scale triple used to pose a model instance.
// Extracted into its own header so non-GL code (M3DSceneScript in
// yawn_core) can emit them without pulling in glad.

namespace yawn {
namespace visual {

struct M3DTransform {
    float position[3]    = { 0.0f, 0.0f, 0.0f };
    float rotationDeg[3] = { 0.0f, 0.0f, 0.0f };   // euler XYZ
    float scale          = 1.0f;
};

} // namespace visual
} // namespace yawn
