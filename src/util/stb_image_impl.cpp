// Single translation unit that stamps out stb_image's implementation.
//
// Moved out of IconLoader.cpp into yawn_core so every consumer of
// yawn_core (main exe + test binary + tinygltf) can resolve the stbi_*
// symbols. Without this, the test binary — which links tinygltf via
// yawn_core — gets "undefined reference to stbi_load_from_memory" at
// link time, because IconLoader.cpp lives only in the main exe.
//
// Keep the codec subset tight: PNG + JPEG covers both our use cases
// (app icon / thumbnails, glTF embedded textures).

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
