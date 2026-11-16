#pragma once

// GeneratorPreset — JSON persistence for GeneratorParams. Presets live
// under project/user generator script roots: <root>/presets/<name>.json.

#include "generate/GeneratorTypes.h"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace yawn {
namespace generate {

nlohmann::json paramsToJson(const GeneratorParams& params);
bool paramsFromJson(const nlohmann::json& j, GeneratorParams& out,
                    std::string* error);

bool saveGeneratorPreset(const std::filesystem::path& file,
                         const GeneratorParams& params, std::string* error);
bool loadGeneratorPreset(const std::filesystem::path& file,
                         GeneratorParams& out, std::string* error);

// Unique preset stems across the preset dirs, sorted; earlier dirs win.
std::vector<std::string> listGeneratorPresets(
    const std::vector<std::filesystem::path>& dirs);

// Resolves a preset name to the first existing file, or empty.
std::filesystem::path findGeneratorPreset(
    const std::vector<std::filesystem::path>& dirs, const std::string& name);

} // namespace generate
} // namespace yawn
