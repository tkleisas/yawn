#include "generate/GeneratorPreset.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace yawn {
namespace generate {

nlohmann::json paramsToJson(const GeneratorParams& p) {
    nlohmann::json j;
    j["seed"] = p.seed;
    nlohmann::json styles = nlohmann::json::array();
    for (const auto& s : p.styles)
        styles.push_back({ { "id", s.id }, { "weight", s.weight } });
    j["styles"] = styles;
    j["blend"] = p.blend;
    j["scale"] = p.scale;
    j["root"] = p.root;
    j["movement"] = p.movement;
    j["generateVisuals"] = p.generateVisuals;
    j["visualTheme"] = p.visualTheme;
    j["visualReactivity"] = p.visualReactivity;
    j["density"] = p.density;
    j["noteLength"] = p.noteLength;
    j["swing"] = p.swing;
    j["tempo"] = p.tempo;
    j["beatsPerBar"] = p.beatsPerBar;
    j["sectionLength"] = p.sectionLength;
    j["repeats"] = p.repeats;
    j["sections"] = p.sections;
    return j;
}

bool paramsFromJson(const nlohmann::json& j, GeneratorParams& out,
                    std::string* error) {
    if (!j.is_object()) {
        if (error) *error = "preset is not an object";
        return false;
    }
    GeneratorParams p;
    p.seed = j.value("seed", static_cast<uint64_t>(1));
    p.blend = j.value("blend", std::string("fuse"));
    p.scale = j.value("scale", std::string{});
    p.root = j.value("root", std::string{});
    p.movement = j.value("movement", std::string("auto"));
    p.generateVisuals = j.value("generateVisuals", false);
    p.visualTheme = j.value("visualTheme", std::string("auto"));
    p.visualReactivity = j.value("visualReactivity", 1.0f);
    p.density = j.value("density", -1.0f);
    p.noteLength = j.value("noteLength", -1.0f);
    p.swing = j.value("swing", -1.0f);
    p.tempo = j.value("tempo", 0);
    p.beatsPerBar = j.value("beatsPerBar", 4);
    p.sectionLength = j.value("sectionLength", 1.0f);
    p.repeats = j.value("repeats", 1);
    p.sections.clear();
    if (j.contains("sections") && j["sections"].is_array()) {
        for (const auto& s : j["sections"])
            if (s.is_string()) p.sections.push_back(s.get<std::string>());
    }
    p.styles.clear();
    if (j.contains("styles") && j["styles"].is_array()) {
        for (const auto& sj : j["styles"]) {
            if (!sj.is_object()) continue;
            StyleMix mix;
            mix.id = sj.value("id", std::string{});
            mix.weight = sj.value("weight", 1.0f);
            if (!mix.id.empty()) p.styles.push_back(mix);
        }
    }
    if (p.styles.empty()) p.styles.push_back({ "pop", 1.0f });
    if (p.tempo < 0 || p.tempo > 999) p.tempo = 0;
    if (p.beatsPerBar < 1 || p.beatsPerBar > 16) p.beatsPerBar = 4;
    out = std::move(p);
    return true;
}

bool saveGeneratorPreset(const std::filesystem::path& file,
                         const GeneratorParams& params, std::string* error) {
    std::error_code ec;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        if (error) *error = "cannot create preset folder: " + ec.message();
        return false;
    }
    std::ofstream f(file);
    if (!f) {
        if (error) *error = "cannot write " + file.string();
        return false;
    }
    f << paramsToJson(params).dump(2) << "\n";
    if (!f.good()) {
        if (error) *error = "write failed: " + file.string();
        return false;
    }
    return true;
}

bool loadGeneratorPreset(const std::filesystem::path& file,
                         GeneratorParams& out, std::string* error) {
    std::ifstream f(file);
    if (!f) {
        if (error) *error = "cannot read " + file.string();
        return false;
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        if (error) *error = std::string("invalid preset JSON: ") + e.what();
        return false;
    }
    return paramsFromJson(j, out, error);
}

std::vector<std::string> listGeneratorPresets(
    const std::vector<std::filesystem::path>& dirs) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            const std::string stem = entry.path().stem().string();
            if (seen.insert(stem).second) out.push_back(stem);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::filesystem::path findGeneratorPreset(
    const std::vector<std::filesystem::path>& dirs, const std::string& name) {
    if (name.empty()) return {};
    for (const auto& dir : dirs) {
        std::error_code ec;
        auto p = dir / (name + ".json");
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return {};
}

} // namespace generate
} // namespace yawn
