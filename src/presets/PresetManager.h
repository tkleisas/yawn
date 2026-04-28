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
    // Instrument-specific state outside the parameter list (Multisampler
    // zones, DrumRack pads, Sampler buffer, …). May reference asset
    // files in <preset-folder>/<preset-stem>/ — see PresetManager
    // ::presetAssetDir().
    json        extraState;                // empty json{} when absent
    std::vector<uint8_t> vst3State;        // optional binary processor state
    std::vector<uint8_t> vst3ControllerState; // optional binary controller state
    std::string genre;                     // comma-separated tags (e.g. "ambient,pad")
    std::string instrument;                // comma-separated tags (e.g. "synth,pad")
};

class PresetManager {
public:
    // ── Directory resolution ────────────────────────────────────────────

    // Returns the root presets directory, creating it if needed.
    static fs::path presetsRootDir();

    // Returns the device-specific presets directory, creating it if needed.
    static fs::path presetsDir(const std::string& deviceId);

    // ── Project-local presets ───────────────────────────────────────────
    // When a project is open, presets save to both the global library
    // (so they're reusable in any future project) AND a per-project
    // copy (so the project folder is self-contained for sharing /
    // archiving). The Browser's device-level "Preset" menu unions
    // both lists; project-local wins on a name collision.
    //
    // App calls setProjectRoot when the project changes (new / open /
    // save-as / close). An empty path disables the project-local
    // path entirely — saves go to the global library only.
    static void   setProjectRoot(fs::path root);
    static fs::path projectRoot();
    static fs::path projectPresetsDir(const std::string& deviceId);
    static fs::path projectPresetAssetDir(const std::string& deviceId,
                                            const std::string& presetName);

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
                               const json& extraState = {},
                               const std::vector<uint8_t>& vst3State = {},
                               const std::vector<uint8_t>& vst3ControllerState = {});

    // Per-preset asset directory: <presetsDir>/<deviceId>/<sanitized-name>/.
    // Used by instruments with non-parametric state (Multisampler zones,
    // DrumRack pads) to store sample WAVs alongside the .json. Returns
    // a path even when the directory doesn't yet exist; saveExtraState
    // is responsible for creating it on save.
    static fs::path presetAssetDir(const std::string& deviceId,
                                    const std::string& presetName);

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
