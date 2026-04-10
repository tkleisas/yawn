#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>

struct sqlite3;

namespace yawn {
namespace library {

struct AudioFileRecord {
    int64_t     id = 0;
    std::string path;
    std::string name;       // filename stem
    std::string extension;  // ".wav", ".flac", etc.
    float       duration = 0;   // seconds
    int         sampleRate = 0;
    int         channels = 0;
    int64_t     fileSize = 0;
    int64_t     lastModified = 0; // epoch seconds
    int64_t     libraryPathId = 0;
};

struct PresetRecord {
    int64_t     id = 0;
    std::string path;
    std::string name;
    std::string deviceId;
    std::string deviceName;
    std::string genre;      // comma-separated tags
    std::string instrument; // comma-separated tags
    int64_t     lastModified = 0;
};

struct LibraryPath {
    int64_t     id = 0;
    std::string path;
};

class LibraryDatabase {
public:
    LibraryDatabase() = default;
    ~LibraryDatabase();

    bool open();   // Opens/creates library.db in app data dir
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // ── Library paths ──────────────────────────────────────────────────
    std::vector<LibraryPath> getLibraryPaths();
    int64_t addLibraryPath(const std::string& path);
    void removeLibraryPath(int64_t id);

    // ── Audio files ────────────────────────────────────────────────────
    void insertOrUpdateAudioFile(const AudioFileRecord& rec);
    void removeAudioFilesForPath(int64_t libraryPathId);
    void removeDeletedFiles(int64_t libraryPathId,
                            const std::vector<std::string>& existingPaths);
    std::vector<AudioFileRecord> getAudioFilesForPath(int64_t libraryPathId);
    std::vector<AudioFileRecord> searchAudioFiles(const std::string& query);
    std::vector<AudioFileRecord> getAllAudioFiles();

    // ── Presets ────────────────────────────────────────────────────────
    void insertOrUpdatePreset(const PresetRecord& rec);
    void clearPresets();
    std::vector<PresetRecord> getAllPresets();
    std::vector<PresetRecord> searchPresets(const std::string& query);
    std::vector<PresetRecord> getPresetsForDevice(const std::string& deviceId);

    // ── Utility ────────────────────────────────────────────────────────
    static std::filesystem::path databasePath();

private:
    void createTables();
    void exec(const char* sql);

    sqlite3*    m_db = nullptr;
    std::mutex  m_mutex;
};

} // namespace library
} // namespace yawn
