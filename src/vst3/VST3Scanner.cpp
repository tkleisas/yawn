#ifdef YAWN_HAS_VST3

#include "vst3/VST3Scanner.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace yawn {
namespace vst3 {

namespace {

nlohmann::json pluginToJson(const VST3PluginInfo& p) {
    nlohmann::json jp;
    jp["name"] = p.name;
    jp["vendor"] = p.vendor;
    jp["version"] = p.version;
    jp["category"] = p.category;
    jp["subcategories"] = p.subcategories;
    jp["classID"] = p.classIDString;
    jp["modulePath"] = p.modulePath;
    jp["isInstrument"] = p.isInstrument;
    return jp;
}

VST3PluginInfo pluginFromJson(const nlohmann::json& jp) {
    VST3PluginInfo info;
    info.name = jp.value("name", "");
    info.vendor = jp.value("vendor", "");
    info.version = jp.value("version", "");
    info.category = jp.value("category", "");
    info.subcategories = jp.value("subcategories", "");
    info.classIDString = jp.value("classID", "");
    info.modulePath = jp.value("modulePath", "");
    info.isInstrument = jp.value("isInstrument", false);
    return info;
}

// Run a command with its stdout captured. Returns true when the
// process launched and exited 0. The child's stderr passes through
// to ours (diagnostics from the scan host / plugin).
bool runCapture(const std::string& cmdline, std::string& out) {
    out.clear();
#ifdef _WIN32
    FILE* pipe = _popen(cmdline.c_str(), "rt");
#else
    FILE* pipe = popen(cmdline.c_str(), "r");
#endif
    if (!pipe) return false;
    std::array<char, 4096> buf;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0)
        out.append(buf.data(), n);
#ifdef _WIN32
    const int rc = _pclose(pipe);
#else
    const int rc = pclose(pipe);
#endif
#ifdef _WIN32
    return rc == 0;
#else
    return rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
#endif
}

// Quote a path for the command line (spaces in module/bundle paths).
std::string shellQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) if (c != '"') out += c;
    out += "\"";
    return out;
}

} // namespace

void VST3Scanner::scan() {
    m_plugins.clear();
    m_entries.clear();
    m_scanComplete = false;

    // Use SDK to get all module paths on the system
    auto modulePaths = VST3::Hosting::Module::getModulePaths();

    for (const auto& modulePath : modulePaths) {
        VST3ModuleHandle handle;
        std::string error;
        if (!handle.load(modulePath, error)) {
            std::cerr << "[VST3] Failed to load " << modulePath << ": " << error << "\n";
            continue;
        }

        auto plugins = handle.enumerate();
        for (auto& info : plugins) {
            m_plugins.push_back(std::move(info));
        }
    }

    // Sort by name for consistent ordering
    std::sort(m_plugins.begin(), m_plugins.end(),
              [](const VST3PluginInfo& a, const VST3PluginInfo& b) {
                  return a.name < b.name;
              });

    rebuildFilteredLists();
}

const VST3PluginInfo* VST3Scanner::findByClassID(const std::string& classIDString) const {
    for (const auto& p : m_plugins) {
        if (p.classIDString == classIDString)
            return &p;
    }
    return nullptr;
}

void VST3Scanner::rebuildFilteredLists() {
    m_instruments.clear();
    m_effects.clear();
    for (const auto& p : m_plugins) {
        if (p.isInstrument)
            m_instruments.push_back(p);
        else
            m_effects.push_back(p);
    }
}

void VST3Scanner::rebuildFromEntries() {
    m_plugins.clear();
    for (const auto& [path, entry] : m_entries)
        for (const auto& p : entry.plugins)
            m_plugins.push_back(p);
    std::sort(m_plugins.begin(), m_plugins.end(),
              [](const VST3PluginInfo& a, const VST3PluginInfo& b) {
                  return a.name < b.name;
              });
    rebuildFilteredLists();
}

// ── v2 resumable scan ─────────────────────────────────────────────

bool VST3Scanner::scanWith(const std::string& cachePath, ModuleScanFn fn) {
    if (m_scanComplete) return true;

    const auto modulePaths = VST3::Hosting::Module::getModulePaths();

    // Prune cache entries for modules that no longer exist on disk.
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (std::find(modulePaths.begin(), modulePaths.end(), it->first)
                == modulePaths.end())
            it = m_entries.erase(it);
        else
            ++it;
    }

    bool allDone = true;
    for (const auto& modulePath : modulePaths) {
        if (m_entries.count(modulePath)) continue;   // already ok/failed

        ModuleEntry entry;
        if (fn(modulePath, entry.plugins)) {
            entry.status = "ok";
        } else {
            // Terminal for this scan lineage — retried only after a
            // cache wipe, so a crashy plugin can't loop every launch.
            entry.status = "failed";
            entry.plugins.clear();
            std::cerr << "[VST3] Skipping " << modulePath
                      << " (scan failed — see host log)\n";
        }
        m_entries[modulePath] = std::move(entry);
        // Incremental write: a kill here loses at most this module,
        // and the next launch resumes instead of restarting.
        saveCache(cachePath);
    }

    // Complete when every current module has an entry.
    for (const auto& modulePath : modulePaths) {
        if (!m_entries.count(modulePath)) { allDone = false; break; }
    }
    m_scanComplete = allDone;
    saveCache(cachePath);
    rebuildFromEntries();
    return m_scanComplete;
}

bool VST3Scanner::scanOutOfProcess(const std::string& cachePath,
                                   const std::string& hostExePath) {
    return scanWith(cachePath,
        [&hostExePath](const std::string& modulePath,
                       std::vector<VST3PluginInfo>& outPlugins) -> bool {
            const std::string cmd =
                shellQuote(hostExePath) + " --scan " + shellQuote(modulePath);
            std::string out;
            if (!runCapture(cmd, out)) return false;

            // The plugin's own printfs land on the same stdout — parse
            // only the outermost [..] span.
            const auto open = out.find('[');
            const auto close = out.rfind(']');
            if (open == std::string::npos || close == std::string::npos ||
                close <= open)
                return false;
            try {
                auto j = nlohmann::json::parse(
                    out.substr(open, close - open + 1));
                outPlugins.clear();
                for (const auto& jp : j)
                    outPlugins.push_back(pluginFromJson(jp));
                return true;
            } catch (const nlohmann::json::exception&) {
                return false;
            }
        });
}

std::string VST3Scanner::hostExePath() {
#ifdef _WIN32
    wchar_t exePathW[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    std::wstring hostExe(exePathW);
    const auto slash = hostExe.find_last_of(L"\\/");
    hostExe = (slash != std::wstring::npos)
        ? hostExe.substr(0, slash + 1) + L"yawn_vst3_host.exe"
        : L"yawn_vst3_host.exe";
    // Narrow it (UTF-8 paths are rare enough on Windows for the
    // ANSI fallback to be acceptable here).
    std::string out(hostExe.begin(), hostExe.end());
    return out;
#else
    std::array<char, 4096> exePath{};
    const ssize_t len = readlink("/proc/self/exe", exePath.data(),
                                 exePath.size() - 1);
    std::string exe = (len > 0)
        ? std::string(exePath.data(), static_cast<size_t>(len))
        : std::string();
    if (!exe.empty()) {
        const auto slash = exe.find_last_of('/');
        return (slash != std::string::npos)
            ? exe.substr(0, slash + 1) + "yawn_vst3_host"
            : "yawn_vst3_host";
    }
    return "yawn_vst3_host";
#endif
}

// ── Cache persistence ──

bool VST3Scanner::saveCache(const std::string& path) const {
    nlohmann::json j;
    j["version"] = 2;
    j["scanComplete"] = m_scanComplete;
    nlohmann::json modules = nlohmann::json::object();
    for (const auto& [modulePath, entry] : m_entries) {
        nlohmann::json me;
        me["status"] = entry.status;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : entry.plugins)
            arr.push_back(pluginToJson(p));
        me["plugins"] = std::move(arr);
        modules[modulePath] = std::move(me);
    }
    j["modules"] = std::move(modules);

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return file.good();
}

bool VST3Scanner::loadCache(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;

        m_plugins.clear();
        m_entries.clear();
        m_scanComplete = false;

        if (j.is_array()) {
            // Legacy v1 cache: a flat plugin array from a completed
            // in-process scan. No per-module provenance, so any NEW
            // module on disk still triggers a subprocess scan (which
            // will then re-cache everything per-module).
            for (const auto& jp : j)
                m_plugins.push_back(pluginFromJson(jp));
            std::sort(m_plugins.begin(), m_plugins.end(),
                      [](const VST3PluginInfo& a, const VST3PluginInfo& b) {
                          return a.name < b.name;
                      });
            rebuildFilteredLists();
            return true;
        }

        m_scanComplete = j.value("scanComplete", false);
        if (j.contains("modules") && j["modules"].is_object()) {
            for (const auto& [modulePath, me] : j["modules"].items()) {
                ModuleEntry entry;
                entry.status = me.value("status", "ok");
                if (me.contains("plugins") && me["plugins"].is_array())
                    for (const auto& jp : me["plugins"])
                        entry.plugins.push_back(pluginFromJson(jp));
                m_entries[modulePath] = std::move(entry);
            }
        }
        rebuildFromEntries();
        return true;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[VST3] Failed to parse cache: " << e.what() << "\n";
        return false;
    }
}

// ── Platform search paths ──

std::vector<std::string> VST3Scanner::getSearchPaths() {
    std::vector<std::string> paths;

#ifdef _WIN32
    // Standard Windows VST3 paths
    char programFiles[MAX_PATH] = {};
    if (SHGetFolderPathA(nullptr, CSIDL_PROGRAM_FILES_COMMON, nullptr, 0, programFiles) == S_OK) {
        paths.push_back(std::string(programFiles) + "\\VST3");
    }
    // User-local VST3 path
    char localAppData[MAX_PATH] = {};
    if (SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData) == S_OK) {
        paths.push_back(std::string(localAppData) + "\\Programs\\Common\\VST3");
    }
#elif defined(__APPLE__)
    paths.push_back("/Library/Audio/Plug-Ins/VST3");
    const char* home = getenv("HOME");
    if (home)
        paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
#else
    // Linux
    paths.push_back("/usr/lib/vst3");
    paths.push_back("/usr/local/lib/vst3");
    const char* home = getenv("HOME");
    if (home)
        paths.push_back(std::string(home) + "/.vst3");
#endif

    return paths;
}

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
