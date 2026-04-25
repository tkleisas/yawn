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

    // Two semantically distinct buckets — confusingly bundled in
    // sibling folders historically. Keep them separate in the menu so
    // users compose chains correctly (Source first, then Effects):
    //   • assets/shaders/examples/  → "Sources"  — standalone generators
    //                                  that synthesize from iTime/audio/
    //                                  iChannel*. They IGNORE iPrev, so
    //                                  using one as a chain pass > 0 will
    //                                  blank whatever was rendered before.
    //   • assets/shaders/post/       → "Effects" — written to sample iPrev
    //                                  and process the previous output.
    //                                  These are the right pick for any
    //                                  pass after the first.
    scanDir("assets/shaders/examples", "Sources");
    scanDir("assets/shaders/post",     "Effects");

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

    // Stable order: Project first (most specific), then Effects (the
    // common pick for chain pass > 0), then Sources (typically the
    // first pass only).
    auto rank = [](const std::string& c) {
        if (c == "Project") return 0;
        if (c == "Effects") return 1;
        if (c == "Sources") return 2;
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
