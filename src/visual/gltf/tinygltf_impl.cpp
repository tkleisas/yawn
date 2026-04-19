// Single translation unit that stamps out tinygltf's implementation.
//
// tinygltf bundles its own copies of stb_image.h and json.hpp. YAWN
// already pulls in both (stb_image via nothings/stb, JSON via
// nlohmann/nlohmann_json), and another TU in the project — IconLoader.cpp
// — already defines STB_IMAGE_IMPLEMENTATION. If we let tinygltf
// re-define it here we'd get duplicate-symbol linker errors, so we
// suppress tinygltf's bundled image + JSON headers and feed it ours.
//
// Guarded by YAWN_HAS_MODEL3D so non-model builds don't need tinygltf on
// the include path.

#if defined(YAWN_HAS_MODEL3D) && YAWN_HAS_MODEL3D

// Don't let tinygltf pull in its bundled image/write headers or
// re-declare the stb_image symbols — we'll #include ours first.
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_JSON

// Cut the filesystem-heavy pieces we don't need: the loader can still
// parse in-memory buffers and plain files, but we don't want the
// experimental URI decoder dragging in extras.
#define TINYGLTF_NO_EXTERNAL_IMAGE

#include "stb_image.h"
#include <nlohmann/json.hpp>

#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

#endif  // YAWN_HAS_MODEL3D
