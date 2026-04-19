#pragma once

// Slim accessor functions over VisualEngine that avoid pulling SDL/GL
// headers. Lets yawn_core (ProjectSerializer) read/write VisualEngine
// state without picking up the transitive SDL3/OpenGL dependency.
//
// Implementation uses a function-pointer dispatch table. The main
// executable registers real callbacks from VisualEngine.cpp at startup
// via a static initialiser; yawn_tests never registers any, so the
// wrappers below fall through to safe defaults (count=0, path="",
// add returns false, etc.). The serializer's code paths already guard
// on `if (visualEngine)`, so skipping post-fx serialization in tests
// is correct behaviour.

#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace visual {

class VisualEngine;

struct PostFXAPI {
    int         (*count)         (const VisualEngine&)              = nullptr;
    std::string (*path)          (const VisualEngine&, int)         = nullptr;
    void        (*clear)         (VisualEngine&)                    = nullptr;
    bool        (*add)           (VisualEngine&, const std::string&)= nullptr;
    std::vector<std::pair<std::string, float>>
                (*getParamValues)(const VisualEngine&, int)         = nullptr;
    void        (*applyParamValues)
                                 (VisualEngine&, int,
                                  const std::vector<std::pair<std::string, float>>&) = nullptr;
};

// Mutable singleton — main exe writes into it during static init; other
// targets see the all-nullptr default.
PostFXAPI& postFXAPITable();

// Convenience wrappers. Safe defaults when the main exe hasn't
// registered (i.e. in the test binary).
int         postFX_count         (const VisualEngine& e);
std::string postFX_path          (const VisualEngine& e, int index);
void        postFX_clear         (VisualEngine& e);
bool        postFX_add           (VisualEngine& e, const std::string& path);
std::vector<std::pair<std::string, float>>
            postFX_getParamValues(const VisualEngine& e, int index);
void        postFX_applyParamValues(VisualEngine& e, int index,
                const std::vector<std::pair<std::string, float>>& values);

} // namespace visual
} // namespace yawn
