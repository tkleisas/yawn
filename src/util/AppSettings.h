#pragma once

#include "audio/ClipEngine.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>

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
    double sampleRate = 44100.0;
    int bufferSize = 256;
    int defaultLaunchQuantize = 2; // 0=None, 1=Beat, 2=Bar
    int defaultRecordQuantize = 2;
    std::vector<int> enabledMidiInputs;
    std::vector<int> enabledMidiOutputs;

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
            s.sampleRate = j.value("sampleRate", 44100.0);
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
        } catch (...) {
            std::fprintf(stderr, "Warning: Failed to parse settings file\n");
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
            std::ofstream out(path);
            if (out.is_open()) {
                out << j.dump(2);
            }
        } catch (...) {
            std::fprintf(stderr, "Warning: Failed to save settings file\n");
        }
    }
};

} // namespace util
} // namespace yawn
