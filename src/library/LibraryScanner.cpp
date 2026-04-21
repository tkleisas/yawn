#include "LibraryScanner.h"
#include "presets/PresetManager.h"
#include "util/Logger.h"
#include <sndfile.h>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace yawn {
namespace library {

namespace fs = std::filesystem;

static const std::unordered_set<std::string> kAudioExts = {
    ".wav", ".flac", ".ogg", ".aiff", ".aif", ".mp3"
};

bool LibraryScanner::isAudioExtension(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return kAudioExts.count(lower) > 0;
}

LibraryScanner::LibraryScanner(LibraryDatabase& db) : m_db(db) {}

LibraryScanner::~LibraryScanner() { stop(); }

void LibraryScanner::startFullScan() {
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_jobs.push({ScanJob::FullScan, 0, ""});
    }
    m_jobCv.notify_one();

    // Start worker if not running
    if (!m_running.load()) {
        m_stopRequested = false;
        m_running = true;
        if (m_thread.joinable()) m_thread.join();
        m_thread = std::thread(&LibraryScanner::workerFunc, this);
    }
}

void LibraryScanner::scanLibraryPath(int64_t pathId, const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_jobs.push({ScanJob::AudioPath, pathId, path});
    }
    m_jobCv.notify_one();

    if (!m_running.load()) {
        m_stopRequested = false;
        m_running = true;
        if (m_thread.joinable()) m_thread.join();
        m_thread = std::thread(&LibraryScanner::workerFunc, this);
    }
}

void LibraryScanner::scanPresets() {
    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_jobs.push({ScanJob::Presets, 0, ""});
    }
    m_jobCv.notify_one();

    if (!m_running.load()) {
        m_stopRequested = false;
        m_running = true;
        if (m_thread.joinable()) m_thread.join();
        m_thread = std::thread(&LibraryScanner::workerFunc, this);
    }
}

void LibraryScanner::stop() {
    m_stopRequested = true;
    m_jobCv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
    m_running = false;
}

void LibraryScanner::workerFunc() {
    while (!m_stopRequested.load()) {
        ScanJob job;
        {
            std::unique_lock<std::mutex> lock(m_jobMutex);
            m_jobCv.wait(lock, [this] { return !m_jobs.empty() || m_stopRequested.load(); });
            if (m_stopRequested.load()) break;
            if (m_jobs.empty()) continue;
            job = m_jobs.front();
            m_jobs.pop();
        }

        m_progress = 0.0f;

        if (job.type == ScanJob::FullScan) {
            // Scan all library paths
            auto paths = m_db.getLibraryPaths();
            float total = static_cast<float>(paths.size()) + 1.0f; // +1 for presets
            int done = 0;
            for (auto& lp : paths) {
                if (m_stopRequested.load()) break;
                doScanAudioPath(lp.id, fs::path(lp.path));
                m_progress = static_cast<float>(++done) / total;
            }
            if (!m_stopRequested.load()) {
                doScanPresets();
                m_progress = 1.0f;
            }
        } else if (job.type == ScanJob::AudioPath) {
            doScanAudioPath(job.pathId, fs::path(job.path));
            m_progress = 1.0f;
        } else if (job.type == ScanJob::Presets) {
            doScanPresets();
            m_progress = 1.0f;
        }

        // Check if more jobs; if not, exit
        {
            std::lock_guard<std::mutex> lock(m_jobMutex);
            if (m_jobs.empty()) {
                m_running = false;
                return;
            }
        }
    }
    m_running = false;
}

void LibraryScanner::doScanAudioPath(int64_t pathId, const fs::path& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) return;

    std::error_code ec;
    std::vector<std::string> foundPaths;

    for (auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (m_stopRequested.load()) return;
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        if (!isAudioExtension(ext)) continue;

        auto filePath = entry.path().string();
        foundPaths.push_back(filePath);

        // Check if file needs updating (compare last_modified)
        auto ftime = fs::last_write_time(entry.path(), ec);
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            ftime.time_since_epoch()).count();

        auto rec = probeAudioFile(entry.path(), pathId);
        rec.lastModified = epoch;
        m_db.insertOrUpdateAudioFile(rec);
    }

    // Clean up deleted files
    m_db.removeDeletedFiles(pathId, foundPaths);
}

AudioFileRecord LibraryScanner::probeAudioFile(const fs::path& file, int64_t pathId) {
    AudioFileRecord rec;
    rec.path          = file.string();
    rec.name          = file.stem().string();
    rec.extension     = file.extension().string();
    rec.libraryPathId = pathId;

    // Try to get file size
    std::error_code ec;
    rec.fileSize = static_cast<int64_t>(fs::file_size(file, ec));

    // Probe with libsndfile for metadata
    SF_INFO info = {};
    SNDFILE* sf = sf_open(file.string().c_str(), SFM_READ, &info);
    if (sf) {
        rec.sampleRate = info.samplerate;
        rec.channels   = info.channels;
        if (info.samplerate > 0)
            rec.duration = static_cast<float>(info.frames) / static_cast<float>(info.samplerate);
        sf_close(sf);
    }
    // If sf_open fails (e.g., MP3 not supported by this sndfile build), keep defaults
    return rec;
}

void LibraryScanner::doScanPresets() {
    auto root = m_presetsRoot.empty()
        ? PresetManager::presetsRootDir().string()
        : m_presetsRoot;

    if (!fs::exists(root)) return;

    std::error_code ec;
    for (auto& deviceDir : fs::directory_iterator(root, ec)) {
        if (m_stopRequested.load()) return;
        if (!deviceDir.is_directory()) continue;

        std::string deviceId = deviceDir.path().filename().string();

        for (auto& presetFile : fs::directory_iterator(deviceDir.path(), ec)) {
            if (m_stopRequested.load()) return;
            if (!presetFile.is_regular_file()) continue;
            if (presetFile.path().extension() != ".json") continue;

            // Load the preset to get metadata
            PresetData data;
            if (!PresetManager::loadPreset(presetFile.path(), data)) continue;

            auto ftime = fs::last_write_time(presetFile.path(), ec);
            auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                ftime.time_since_epoch()).count();

            PresetRecord rec;
            rec.path         = presetFile.path().string();
            rec.name         = data.name;
            rec.deviceId     = data.deviceId;
            rec.deviceName   = data.deviceName;
            rec.deviceType   = classifyDeviceType(data.deviceId, data.deviceName);
            rec.genre        = data.genre;
            rec.instrument   = data.instrument;
            rec.lastModified = epoch;
            m_db.insertOrUpdatePreset(rec);
        }
    }
}

} // namespace library
} // namespace yawn
