#pragma once
#include "LibraryDatabase.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>

namespace yawn {
namespace library {

class LibraryScanner {
public:
    explicit LibraryScanner(LibraryDatabase& db);
    ~LibraryScanner();

    // Scan all library paths + presets directory
    void startFullScan();

    // Scan a single library path
    void scanLibraryPath(int64_t pathId, const std::string& path);

    // Scan presets directory only
    void scanPresets();

    void stop();
    bool isScanning() const { return m_running.load(); }
    float progress() const  { return m_progress.load(); }

    // Set the presets root directory (from PresetManager)
    void setPresetsRoot(const std::string& path) { m_presetsRoot = path; }

private:
    void workerFunc();
    void doScanAudioPath(int64_t pathId, const std::filesystem::path& dir);
    void doScanPresets();
    AudioFileRecord probeAudioFile(const std::filesystem::path& file, int64_t pathId);

    static bool isAudioExtension(const std::string& ext);

    LibraryDatabase& m_db;
    std::string      m_presetsRoot;

    std::thread      m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<float> m_progress{0.0f};

    struct ScanJob {
        enum Type { AudioPath, Presets, FullScan };
        Type        type;
        int64_t     pathId = 0;
        std::string path;
    };
    std::queue<ScanJob> m_jobs;
    std::mutex          m_jobMutex;
    std::condition_variable m_jobCv;
};

} // namespace library
} // namespace yawn
