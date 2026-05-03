#pragma once

#include "audio/ClipEngine.h"
#include "util/Logger.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace yawn {
namespace util {

struct AppSettings {
    int outputDevice = -1;
    int inputDevice = -1;
    double sampleRate = kDefaultSampleRate;
    int bufferSize = 256;
    int defaultLaunchQuantize = 2; // 0=None, 1=Beat, 2=Bar
    int defaultRecordQuantize = 2;
    std::vector<int> enabledMidiInputs;
    std::vector<int> enabledMidiOutputs;

    // Metronome
    float metronomeVolume = 0.7f;   // 0.0–1.0
    int metronomeMode = 0;          // 0=Always, 1=RecordOnly, 2=PlayOnly, 3=Off
    int countInBars = 0;            // 0, 1, 2, or 4
    int metronomeVisualStyle = 0;  // 0=Dots, 1=Beat Number

    // Theme — UI typography scale applied to fw2 theme.metrics.fontSize*
    // at startup. 1.0 keeps the baked-in 15/13/18 defaults; 1.25 makes
    // everything 25% larger; 0.85 tightens it. Controlled via the
    // Preferences → Theme tab.
    float fontScale = 1.25f;   // default bumped per user feedback

    // Plugin Delay Compensation — Edit → Preferences → Audio.
    // Off by default (most chains have no latency-introducing
    // effects; users with serious mixes flip it on once).
    bool latencyCompensation = false;

    // Ableton Link — network beat/tempo sync. Edit → Preferences →
    // Link. Off by default; opt-in once you actually have peers on
    // the LAN. The transport panel's Link button toggles the same
    // engine flag, but we persist whatever the user last set so
    // restarts don't silently leave Link enabled.
    bool linkEnabled = false;
    // Start/stop transport sync — separate per Link convention.
    // Lets peers' play/stop state propagate alongside tempo. Off by
    // default; users that only want tempo sync leave this off.
    bool linkStartStopSync = false;

    static std::filesystem::path settingsPath() {
#ifdef _WIN32
        char appData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            auto dir = std::filesystem::path(appData) / "YAWN";
            std::filesystem::create_directories(dir);
            return dir / "settings.json";
        }
#endif
        auto dir = std::filesystem::path(getenv("HOME") ? getenv("HOME") : "/tmp") / ".yawn";
        std::filesystem::create_directories(dir);
        return dir / "settings.json";
    }

    static AppSettings load() {
        AppSettings s;
        auto path = settingsPath();
        std::ifstream in(path);
        if (!in.is_open()) return s;
        try {
            auto j = nlohmann::json::parse(in);
            s.outputDevice = j.value("outputDevice", -1);
            s.inputDevice = j.value("inputDevice", -1);
            s.sampleRate = j.value("sampleRate", kDefaultSampleRate);
            s.bufferSize = j.value("bufferSize", 256);
            s.defaultLaunchQuantize = j.value("defaultLaunchQuantize", 2);
            s.defaultRecordQuantize = j.value("defaultRecordQuantize", 2);
            if (j.contains("enabledMidiInputs") && j["enabledMidiInputs"].is_array()) {
                for (auto& v : j["enabledMidiInputs"])
                    s.enabledMidiInputs.push_back(v.get<int>());
            }
            if (j.contains("enabledMidiOutputs") && j["enabledMidiOutputs"].is_array()) {
                for (auto& v : j["enabledMidiOutputs"])
                    s.enabledMidiOutputs.push_back(v.get<int>());
            }
            s.metronomeVolume = j.value("metronomeVolume", 0.7f);
            s.metronomeMode = j.value("metronomeMode", 0);
            s.countInBars = j.value("countInBars", 0);
            s.metronomeVisualStyle = j.value("metronomeVisualStyle", 0);
            s.fontScale = j.value("fontScale", 1.25f);
            s.latencyCompensation = j.value("latencyCompensation", false);
            s.linkEnabled = j.value("linkEnabled", false);
            s.linkStartStopSync = j.value("linkStartStopSync", false);
        } catch (...) {
            LOG_WARN("App", "Failed to parse settings file");
        }
        return s;
    }

    static void save(const AppSettings& s) {
        auto path = settingsPath();
        try {
            nlohmann::json j;
            j["outputDevice"] = s.outputDevice;
            j["inputDevice"] = s.inputDevice;
            j["sampleRate"] = s.sampleRate;
            j["bufferSize"] = s.bufferSize;
            j["defaultLaunchQuantize"] = s.defaultLaunchQuantize;
            j["defaultRecordQuantize"] = s.defaultRecordQuantize;
            j["enabledMidiInputs"] = s.enabledMidiInputs;
            j["enabledMidiOutputs"] = s.enabledMidiOutputs;
            j["metronomeVolume"] = s.metronomeVolume;
            j["metronomeMode"] = s.metronomeMode;
            j["countInBars"] = s.countInBars;
            j["metronomeVisualStyle"] = s.metronomeVisualStyle;
            j["fontScale"] = s.fontScale;
            j["latencyCompensation"] = s.latencyCompensation;
            j["linkEnabled"] = s.linkEnabled;
            j["linkStartStopSync"] = s.linkStartStopSync;
            std::ofstream out(path);
            if (out.is_open()) {
                out << j.dump(2);
            }
        } catch (...) {
            LOG_WARN("App", "Failed to save settings file");
        }
    }
};

} // namespace util
} // namespace yawn
