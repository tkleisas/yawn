#pragma once

// ShaderParamParse — the `@range` annotation parser shared by the visual
// engine (per-clip custom knobs) and the look-iteration harness
// (yawn_visual_probe — which must start from the same defaults the app
// does, or a shader like 14_kaleidoscope renders black at segments=0).
//
// Parses user-declared `uniform float NAME;` lines plus the optional
// annotation `// @range min..max default=N`. Anything not matching keeps
// the 0..1 / default 0 convention. Built-in uniform names are skipped so
// they never surface as user parameters.

#include <algorithm>
#include <regex>
#include <string>
#include <vector>

namespace yawn {
namespace visual {

struct ParsedParam {
    std::string name;
    float min, max, defaultValue;
};

// Built-in uniform names that the parser skips so they don't show up as
// user parameters. Kept sorted alphabetically for readability.
inline bool isBuiltinShaderUniform(const std::string& name) {
    static const std::vector<std::string> builtins = {
        "iAudioHigh", "iAudioLevel", "iAudioLow", "iAudioMid",
        "iBeat", "iChannelResolution", "iChannelTime",
        "iChannel0", "iChannel1", "iChannel2", "iChannel3",
        "iDate", "iFrame", "iKick", "iMouse", "iResolution",
        "iSampleRate", "iTime", "iTimeDelta",
        "iTransportPlaying", "iTransportTime",
    };
    return std::find(builtins.begin(), builtins.end(), name) != builtins.end();
}

// Returns (name, min, max, defaultValue) tuples for the caller to build
// into engine/harness parameter state.
inline std::vector<ParsedParam> parseShaderParams(const std::string& source) {
    std::vector<ParsedParam> out;
    // Pull the "lazy skip" inside the optional annotation group so the
    // engine is actually motivated to advance past whitespace between ";"
    // and "//". With the skip outside the group, a lazy quantifier prefers
    // zero chars and the annotation never matches.
    std::regex re(
        R"(uniform\s+float\s+([A-Za-z_][A-Za-z0-9_]*)\s*;(?:[^\n]*?//[^\n]*@range\s+([-0-9.eE+]+)\s*\.\.\s*([-0-9.eE+]+)[^\n]*default\s*=\s*([-0-9.eE+]+))?)",
        std::regex::ECMAScript);

    auto begin = std::sregex_iterator(source.begin(), source.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string name = (*it)[1].str();
        if (isBuiltinShaderUniform(name)) continue;

        ParsedParam p;
        p.name = name;
        if ((*it)[2].matched && (*it)[3].matched && (*it)[4].matched) {
            try {
                p.min          = std::stof((*it)[2].str());
                p.max          = std::stof((*it)[3].str());
                p.defaultValue = std::stof((*it)[4].str());
            } catch (...) {
                p.min = 0.0f; p.max = 1.0f; p.defaultValue = 0.0f;
            }
        } else {
            p.min = 0.0f; p.max = 1.0f; p.defaultValue = 0.0f;
        }
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace visual
} // namespace yawn
