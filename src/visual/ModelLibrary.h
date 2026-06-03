#pragma once

// ModelLibrary — a flat catalogue of 3D model files (.glb / .gltf) found
// across a set of directories, for the browser's Models tab. No GL, no
// database: it just stats directories (bundled examples + a user library
// folder) and exposes name-filterable entries. Cheap enough to refresh on
// demand; there are only ever a handful of models.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace yawn {
namespace visual {

struct ModelLibraryEntry {
    std::string path;     // absolute path to the .glb/.gltf
    std::string name;     // display name (filename stem)
    bool        bundled = false;
};

class ModelLibrary {
public:
    // Directories scanned in order; entries from earlier dirs are marked
    // `bundled = true` when that dir is flagged bundled. Scans one level
    // deep (the dir itself), case-insensitive on the extension.
    void setDirectories(std::vector<std::string> dirs,
                        int bundledCount = 0) {
        m_dirs = std::move(dirs);
        m_bundledCount = bundledCount;
    }

    void refresh() {
        m_entries.clear();
        for (size_t d = 0; d < m_dirs.size(); ++d) {
            std::error_code ec;
            std::filesystem::path dir(m_dirs[d]);
            if (!std::filesystem::is_directory(dir, ec)) continue;
            for (auto& de : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!de.is_regular_file(ec)) continue;
                std::string ext = de.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (ext != ".glb" && ext != ".gltf") continue;
                ModelLibraryEntry e;
                e.path    = de.path().string();
                e.name    = de.path().stem().string();
                e.bundled = (static_cast<int>(d) < m_bundledCount);
                m_entries.push_back(std::move(e));
            }
        }
        std::sort(m_entries.begin(), m_entries.end(),
                  [](const ModelLibraryEntry& a, const ModelLibraryEntry& b) {
                      if (a.bundled != b.bundled) return a.bundled > b.bundled;
                      return a.name < b.name;
                  });
    }

    const std::vector<ModelLibraryEntry>& entries() const { return m_entries; }

    // Entries whose name contains `query` (case-insensitive). Empty query
    // returns everything.
    std::vector<ModelLibraryEntry> filtered(const std::string& query) const {
        if (query.empty()) return m_entries;
        std::string q = lower(query);
        std::vector<ModelLibraryEntry> out;
        for (const auto& e : m_entries)
            if (lower(e.name).find(q) != std::string::npos) out.push_back(e);
        return out;
    }

private:
    static std::string lower(const std::string& s) {
        std::string r = s;
        std::transform(r.begin(), r.end(), r.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return r;
    }

    std::vector<std::string>       m_dirs;
    int                            m_bundledCount = 0;
    std::vector<ModelLibraryEntry> m_entries;
};

} // namespace visual
} // namespace yawn
