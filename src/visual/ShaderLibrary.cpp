#include "visual/ShaderLibrary.h"

#include <algorithm>
#include <unordered_set>

namespace yawn {
namespace visual {
namespace fs = std::filesystem;

void ShaderLibrary::scanDir(const fs::path& dir, const std::string& category) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;

    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        const std::string ext = p.extension().string();
        if (ext != ".frag" && ext != ".glsl" && ext != ".fs") continue;

        Entry e;
        e.label        = p.stem().string();
        e.absolutePath = fs::absolute(p).string();
        e.category     = category;
        m_entries.push_back(std::move(e));
    }
}

void ShaderLibrary::refresh() {
    m_entries.clear();

    // Bundled effects + post-FX. Two separate categories so the menu
    // can group them visually (and so a user can tell them apart).
    scanDir("assets/shaders/examples", "Effects");
    scanDir("assets/shaders/post",     "Post");

    if (!m_projectDir.empty()) {
        scanDir(m_projectDir, "Project");
    }

    // Project entries shadow bundled ones with the same label —
    // remove the bundled duplicate so the menu shows the user's
    // override instead.
    std::unordered_set<std::string> projectLabels;
    for (const auto& e : m_entries)
        if (e.category == "Project") projectLabels.insert(e.label);
    if (!projectLabels.empty()) {
        m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(),
                [&](const Entry& e) {
                    return e.category != "Project" &&
                           projectLabels.count(e.label) > 0;
                }),
            m_entries.end());
    }

    // Stable order: Project first (most specific), then Effects, then Post.
    auto rank = [](const std::string& c) {
        if (c == "Project") return 0;
        if (c == "Effects") return 1;
        if (c == "Post")    return 2;
        return 3;
    };
    std::sort(m_entries.begin(), m_entries.end(),
              [&](const Entry& a, const Entry& b) {
                  if (rank(a.category) != rank(b.category))
                      return rank(a.category) < rank(b.category);
                  return a.label < b.label;
              });
}

} // namespace visual
} // namespace yawn
