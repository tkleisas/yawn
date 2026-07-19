#pragma once

// VST3Scanner — Discovers VST3 plugins from system directories and caches
// the results to a JSON file for fast startup.
//
// Two scan modes:
//   * scan()             — legacy in-process enumeration (a crashy plugin
//                          takes the DAW down with it; kept as a fallback
//                          for environments without the host binary).
//   * scanOutOfProcess() — spawns `yawn_vst3_host --scan <module>` per
//                          module, so a crashing plugin only fails its own
//                          entry. The v2 cache is written incrementally
//                          after every module, so a killed scan resumes
//                          where it left off instead of restarting (and
//                          re-crashing) from zero.

#ifdef YAWN_HAS_VST3

#include "vst3/VST3Host.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yawn {
namespace vst3 {

class VST3Scanner {
public:
    // Legacy in-process scan. Dangerous against crashy plugins —
    // prefer scanOutOfProcess at app startup.
    void scan();

    // Get cached results (call scan() first, or loadCache())
    const std::vector<VST3PluginInfo>& plugins() const { return m_plugins; }
    const std::vector<VST3PluginInfo>& instruments() const { return m_instruments; }
    const std::vector<VST3PluginInfo>& effects() const { return m_effects; }

    // Find a plugin by its classID string
    const VST3PluginInfo* findByClassID(const std::string& classIDString) const;

    // ── v2 resumable scan ──
    //
    // Cache format (v2):
    //   { "version": 2, "scanComplete": bool,
    //     "modules": { "<path>": {"status": "ok"|"failed",
    //                             "plugins": [...] }, ... } }
    //
    // loadCache() accepts both v2 and the legacy v1 array format.
    // scanComplete() is true when every module on the system has an
    // entry — the app then skips scanning entirely.
    bool scanComplete() const { return m_scanComplete; }

    // Per-module scan function (seam for tests): returns true and
    // fills outPlugins on success, false on failure (crash, load
    // error, timeout).
    using ModuleScanFn = std::function<bool(
        const std::string& modulePath,
        std::vector<VST3PluginInfo>& outPlugins)>;

    // Resume an incomplete scan using `fn` per module: modules already
    // in the cache (ok OR failed) are skipped; new modules are scanned
    // one at a time with the cache written after each, so a kill mid-
    // scan resumes next launch. Marks the cache complete when all
    // current module paths have entries. Returns true when the cache
    // is complete at return (including "already was").
    bool scanWith(const std::string& cachePath, ModuleScanFn fn);

    // scanWith with a real subprocess per module:
    // `yawn_vst3_host --scan <module>` → JSON plugin list on stdout.
    // The module's own crashes become a "failed" cache entry (not
    // retried on later launches — no crash loop).
    bool scanOutOfProcess(const std::string& cachePath,
                          const std::string& hostExePath);

    // Locate yawn_vst3_host next to the running app binary
    // (same resolution as VST3EditorWindow).
    static std::string hostExePath();

    // Cache persistence (v2 on write; reads accept v1 arrays too).
    bool saveCache(const std::string& path) const;
    bool loadCache(const std::string& path);

    // Get platform-specific VST3 search directories
    static std::vector<std::string> getSearchPaths();

private:
    void rebuildFilteredLists();
    void rebuildFromEntries();

    struct ModuleEntry {
        std::string status;   // "ok" | "failed"
        std::vector<VST3PluginInfo> plugins;
    };

    std::vector<VST3PluginInfo> m_plugins;      // All discovered plugins
    std::vector<VST3PluginInfo> m_instruments;   // Filtered: instruments only
    std::vector<VST3PluginInfo> m_effects;       // Filtered: effects only

    std::unordered_map<std::string, ModuleEntry> m_entries;
    bool m_scanComplete = false;
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
