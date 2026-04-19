#pragma once

// Slim accessor functions over VisualEngine that avoid pulling SDL/GL
// headers. Lets yawn_core (ProjectSerializer) read/write VisualEngine
// state without picking up the transitive SDL3/OpenGL dependency.

#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace visual {

class VisualEngine;

int         postFX_count(const VisualEngine& e);
std::string postFX_path (const VisualEngine& e, int index);
void        postFX_clear(VisualEngine& e);
bool        postFX_add  (VisualEngine& e, const std::string& path);

// Parameter accessors — used by the serializer to round-trip knob values
// without depending on the full VisualEngine header (SDL/GL free).
std::vector<std::pair<std::string, float>>
            postFX_getParamValues(const VisualEngine& e, int index);
void        postFX_applyParamValues(VisualEngine& e, int index,
                const std::vector<std::pair<std::string, float>>& values);

} // namespace visual
} // namespace yawn
