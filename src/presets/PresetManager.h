#pragma once

// PresetManager — Save, load, list, rename, and delete device presets.
// Presets are stored as JSON files in a user-specific directory:
//   Windows:    %APPDATA%\YAWN\presets\<deviceId>\<name>.json
//   macOS/Linux: ~/.yawn/presets/<deviceId>/<name>.json

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace yawn {

using json = nlohmann::json;
namespace fs = std::filesystem;

// Metadata about a single preset file
struct PresetInfo {
    std::string name;       // Display name (stem of the filename)
    std::string deviceId;   // e.g. "fm_synth" or "vst3:ABCD..."
    fs::path    filePath;   // Full path to .json file
};

// Parsed preset data returned by loadPreset()
struct PresetData {
    std::string name;
    std::string deviceId;
    std::string deviceName;
    json        params;                    // name → float
    std::vector<uint8_t> vst3State;        // optional binary processor state
    std::vector<uint8_t> vst3ControllerState; // optional binary controller state
};

class PresetManager {
public:
    // ── Directory resolution ────────────────────────────────────────────

    // Returns the root presets directory, creating it if needed.
    static fs::path presetsRootDir();

    // Returns the device-specific presets directory, creating it if needed.
    static fs::path presetsDir(const std::string& deviceId);

    // ── Listing ─────────────────────────────────────────────────────────

    // List all presets available for a given device, sorted by name.
    static std::vector<PresetInfo> listPresetsForDevice(const std::string& deviceId);

    // List all presets across all devices.
    static std::vector<PresetInfo> listAllPresets();

    // ── Save / Load ─────────────────────────────────────────────────────

    // Save a preset. Returns the path of the written file (empty on error).
    static fs::path savePreset(const std::string& name,
                               const std::string& deviceId,
                               const std::string& deviceName,
                               const json& params,
                               const std::vector<uint8_t>& vst3State = {},
                               const std::vector<uint8_t>& vst3ControllerState = {});

    // Load a preset from disk. Returns true on success.
    static bool loadPreset(const fs::path& filePath, PresetData& outData);

    // ── Delete / Rename ─────────────────────────────────────────────────

    // Delete a preset file. Returns true on success.
    static bool deletePreset(const fs::path& filePath);

    // Rename a preset (moves the file). Returns the new path (empty on error).
    static fs::path renamePreset(const fs::path& filePath, const std::string& newName);

    // ── Helpers ─────────────────────────────────────────────────────────

    // Sanitize a preset name for use as a filename (strips illegal chars).
    static std::string sanitizeName(const std::string& name);

private:
    static std::string toHex(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> fromHex(const std::string& hex);
};

} // namespace yawn
