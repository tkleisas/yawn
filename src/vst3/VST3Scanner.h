#pragma once

// VST3Scanner — Discovers VST3 plugins from system directories and caches
// the results to a JSON file for fast startup.

#ifdef YAWN_HAS_VST3

#include "vst3/VST3Host.h"
#include <string>
#include <vector>

namespace yawn {
namespace vst3 {

class VST3Scanner {
public:
    // Scan all system VST3 directories and enumerate plugins
    void scan();

    // Get cached results (call scan() first, or loadCache())
    const std::vector<VST3PluginInfo>& plugins() const { return m_plugins; }
    const std::vector<VST3PluginInfo>& instruments() const { return m_instruments; }
    const std::vector<VST3PluginInfo>& effects() const { return m_effects; }

    // Find a plugin by its classID string
    const VST3PluginInfo* findByClassID(const std::string& classIDString) const;

    // Cache persistence
    bool saveCache(const std::string& path) const;
    bool loadCache(const std::string& path);

    // Get platform-specific VST3 search directories
    static std::vector<std::string> getSearchPaths();

private:
    void rebuildFilteredLists();

    std::vector<VST3PluginInfo> m_plugins;      // All discovered plugins
    std::vector<VST3PluginInfo> m_instruments;   // Filtered: instruments only
    std::vector<VST3PluginInfo> m_effects;       // Filtered: effects only
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
