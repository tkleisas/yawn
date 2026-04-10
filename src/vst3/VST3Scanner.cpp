#ifdef YAWN_HAS_VST3

#include "vst3/VST3Scanner.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <shlobj.h>
#endif

namespace yawn {
namespace vst3 {

void VST3Scanner::scan() {
    m_plugins.clear();

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

// ── Cache persistence ──

bool VST3Scanner::saveCache(const std::string& path) const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : m_plugins) {
        nlohmann::json jp;
        jp["name"] = p.name;
        jp["vendor"] = p.vendor;
        jp["version"] = p.version;
        jp["category"] = p.category;
        jp["subcategories"] = p.subcategories;
        jp["classID"] = p.classIDString;
        jp["modulePath"] = p.modulePath;
        jp["isInstrument"] = p.isInstrument;
        j.push_back(jp);
    }

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
        for (const auto& jp : j) {
            VST3PluginInfo info;
            info.name = jp.value("name", "");
            info.vendor = jp.value("vendor", "");
            info.version = jp.value("version", "");
            info.category = jp.value("category", "");
            info.subcategories = jp.value("subcategories", "");
            info.classIDString = jp.value("classID", "");
            info.modulePath = jp.value("modulePath", "");
            info.isInstrument = jp.value("isInstrument", false);
            m_plugins.push_back(std::move(info));
        }

        rebuildFilteredLists();
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
