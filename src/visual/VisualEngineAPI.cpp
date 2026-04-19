#include "visual/VisualEngineAPI.h"

// This file is compiled into yawn_core and provides the default
// implementations of the slim VisualEngine accessor API. The main
// executable links this and also pulls in VisualEngine.cpp, which
// registers real callback pointers from a static initialiser. The
// test binary links only this stub version (no SDL/GL), so post-fx
// queries harmlessly return empty/false.

namespace yawn {
namespace visual {

PostFXAPI& postFXAPITable() {
    static PostFXAPI t{};
    return t;
}

int postFX_count(const VisualEngine& e) {
    auto& t = postFXAPITable();
    return t.count ? t.count(e) : 0;
}

std::string postFX_path(const VisualEngine& e, int index) {
    auto& t = postFXAPITable();
    return t.path ? t.path(e, index) : std::string();
}

void postFX_clear(VisualEngine& e) {
    auto& t = postFXAPITable();
    if (t.clear) t.clear(e);
}

bool postFX_add(VisualEngine& e, const std::string& path) {
    auto& t = postFXAPITable();
    return t.add ? t.add(e, path) : false;
}

std::vector<std::pair<std::string, float>>
postFX_getParamValues(const VisualEngine& e, int index) {
    auto& t = postFXAPITable();
    return t.getParamValues ? t.getParamValues(e, index)
                             : std::vector<std::pair<std::string, float>>{};
}

void postFX_applyParamValues(VisualEngine& e, int index,
        const std::vector<std::pair<std::string, float>>& values) {
    auto& t = postFXAPITable();
    if (t.applyParamValues) t.applyParamValues(e, index, values);
}

} // namespace visual
} // namespace yawn
