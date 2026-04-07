#include "presets/PresetManager.h"
#include "util/Logger.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace yawn {

// ─── Directory resolution ────────────────────────────────────────────────────

fs::path PresetManager::presetsRootDir()
{
    fs::path root;
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        root = fs::path(appData) / "YAWN" / "presets";
    } else {
        root = fs::path(".") / "presets"; // fallback
    }
#else
    const char* home = getenv("HOME");
    root = fs::path(home ? home : "/tmp") / ".yawn" / "presets";
#endif
    std::error_code ec;
    fs::create_directories(root, ec);
    return root;
}

fs::path PresetManager::presetsDir(const std::string& deviceId)
{
    fs::path dir = presetsRootDir() / sanitizeName(deviceId);
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// ─── Listing ─────────────────────────────────────────────────────────────────

std::vector<PresetInfo> PresetManager::listPresetsForDevice(const std::string& deviceId)
{
    std::vector<PresetInfo> result;
    fs::path dir = presetsDir(deviceId);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return result;

    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        PresetInfo info;
        info.name     = entry.path().stem().string();
        info.deviceId = deviceId;
        info.filePath = entry.path();
        result.push_back(std::move(info));
    }
    std::sort(result.begin(), result.end(),
              [](const PresetInfo& a, const PresetInfo& b) { return a.name < b.name; });
    return result;
}

std::vector<PresetInfo> PresetManager::listAllPresets()
{
    std::vector<PresetInfo> result;
    fs::path root = presetsRootDir();
    std::error_code ec;
    if (!fs::exists(root, ec)) return result;

    for (auto& deviceDir : fs::directory_iterator(root, ec)) {
        if (!deviceDir.is_directory()) continue;
        std::string deviceId = deviceDir.path().filename().string();
        for (auto& entry : fs::directory_iterator(deviceDir.path(), ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            PresetInfo info;
            info.name     = entry.path().stem().string();
            info.deviceId = deviceId;
            info.filePath = entry.path();
            result.push_back(std::move(info));
        }
    }
    std::sort(result.begin(), result.end(),
              [](const PresetInfo& a, const PresetInfo& b) {
                  if (a.deviceId != b.deviceId) return a.deviceId < b.deviceId;
                  return a.name < b.name;
              });
    return result;
}

// ─── Save / Load ─────────────────────────────────────────────────────────────

fs::path PresetManager::savePreset(const std::string& name,
                                   const std::string& deviceId,
                                   const std::string& deviceName,
                                   const json& params,
                                   const std::vector<uint8_t>& vst3State,
                                   const std::vector<uint8_t>& vst3ControllerState)
{
    std::string safeName = sanitizeName(name);
    if (safeName.empty()) {
        LOG_ERROR("Preset", "empty preset name after sanitization");
        return {};
    }

    fs::path dir  = presetsDir(deviceId);
    fs::path path = dir / (safeName + ".json");

    json j;
    j["name"]       = name;
    j["deviceId"]   = deviceId;
    j["deviceName"] = deviceName;
    j["params"]     = params;

    if (!vst3State.empty())
        j["vst3State"] = toHex(vst3State);
    if (!vst3ControllerState.empty())
        j["vst3ControllerState"] = toHex(vst3ControllerState);

    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Preset", "failed to write %s", path.string().c_str());
        return {};
    }
    file << j.dump(2);
    file.close();

    LOG_INFO("Preset", "saved preset '%s' to %s", name.c_str(), path.string().c_str());
    return path;
}

bool PresetManager::loadPreset(const fs::path& filePath, PresetData& outData)
{
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("Preset", "cannot open %s", filePath.string().c_str());
        return false;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_ERROR("Preset", "parse error in %s: %s",
                  filePath.string().c_str(), e.what());
        return false;
    }

    outData.name       = j.value("name", filePath.stem().string());
    outData.deviceId   = j.value("deviceId", "");
    outData.deviceName = j.value("deviceName", "");
    outData.params     = j.value("params", json::object());

    if (j.contains("vst3State"))
        outData.vst3State = fromHex(j["vst3State"].get<std::string>());
    else
        outData.vst3State.clear();

    if (j.contains("vst3ControllerState"))
        outData.vst3ControllerState = fromHex(j["vst3ControllerState"].get<std::string>());
    else
        outData.vst3ControllerState.clear();

    return true;
}

// ─── Delete / Rename ─────────────────────────────────────────────────────────

bool PresetManager::deletePreset(const fs::path& filePath)
{
    std::error_code ec;
    if (fs::remove(filePath, ec)) {
        LOG_INFO("Preset", "deleted %s", filePath.string().c_str());
        return true;
    }
    LOG_ERROR("Preset", "failed to delete %s", filePath.string().c_str());
    return false;
}

fs::path PresetManager::renamePreset(const fs::path& filePath, const std::string& newName)
{
    std::string safeName = sanitizeName(newName);
    if (safeName.empty()) return {};

    fs::path newPath = filePath.parent_path() / (safeName + ".json");

    // Don't overwrite an existing preset
    std::error_code ec;
    if (fs::exists(newPath, ec)) {
        LOG_ERROR("Preset", "preset '%s' already exists", newName.c_str());
        return {};
    }

    // Also update the name inside the JSON file
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) return {};
    json j;
    try { inFile >> j; } catch (...) { return {}; }
    inFile.close();

    j["name"] = newName;
    std::ofstream outFile(newPath);
    if (!outFile.is_open()) return {};
    outFile << j.dump(2);
    outFile.close();

    fs::remove(filePath, ec);
    LOG_INFO("Preset", "renamed to '%s'", newPath.string().c_str());
    return newPath;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

std::string PresetManager::sanitizeName(const std::string& name)
{
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        // Strip characters illegal in Windows/macOS/Linux filenames
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"'  || c == '<' || c == '>' || c == '|')
            continue;
        // Replace control characters with underscore
        if (static_cast<unsigned char>(c) < 32) {
            result += '_';
            continue;
        }
        result += c;
    }
    // Trim leading/trailing whitespace and dots
    size_t start = result.find_first_not_of(" .");
    size_t end   = result.find_last_not_of(" .");
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

std::string PresetManager::toHex(const std::vector<uint8_t>& data)
{
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (uint8_t b : data) {
        result += hex[b >> 4];
        result += hex[b & 0xF];
    }
    return result;
}

std::vector<uint8_t> PresetManager::fromHex(const std::string& hex)
{
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return 0;
        };
        result.push_back((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
    }
    return result;
}

} // namespace yawn
