#pragma once

// ShaderLibrary — best-effort enumeration of available .frag files
// suitable as chain passes ("video FX devices") on a visual track.
//
// Two scan roots:
//   • Bundled — assets/shaders/examples/ + assets/shaders/post/
//                (ships with YAWN; serves as the default catalogue)
//   • Project — <projectDir>/shaders/ (per-project user shaders, e.g.
//                imported via "Load Shader…" or "New Shader…")
//
// Project entries shadow bundled entries with the same filename so
// users can override a built-in effect without touching assets/.
//
// Cheap to call refresh() — the menu does it on every open so newly
// added files appear without restarting the app.

#include <filesystem>
#include <string>
#include <vector>

namespace yawn {
namespace visual {

class ShaderLibrary {
public:
    struct Entry {
        std::string label;          // display name (stem of filename)
        std::string absolutePath;   // ready to hand to loadLayer / chain
        std::string category;       // "Effects", "Post", "Project"
    };

    // Set the per-project shaders directory. Pass an empty path when
    // no project is open; refresh() will skip the project scan in
    // that case.
    void setProjectShadersDir(const std::filesystem::path& dir) {
        m_projectDir = dir;
    }

    // Re-scan all configured roots. Sorts entries by category then
    // label so the resulting menu is stable across opens.
    void refresh();

    const std::vector<Entry>& entries() const { return m_entries; }

private:
    void scanDir(const std::filesystem::path& dir, const std::string& category);

    std::filesystem::path m_projectDir;
    std::vector<Entry>    m_entries;
};

} // namespace visual
} // namespace yawn
