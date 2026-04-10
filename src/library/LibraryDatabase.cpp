#include "LibraryDatabase.h"
#include "util/Logger.h"
#include <sqlite3.h>
#include <cstdlib>
#include <unordered_set>

#ifdef _WIN32
#  include <shlobj.h>
#endif

namespace yawn {
namespace library {

// ── Lifecycle ──────────────────────────────────────────────────────────

LibraryDatabase::~LibraryDatabase() { close(); }

std::filesystem::path LibraryDatabase::databasePath() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        auto dir = fs::path(appData) / "YAWN";
        fs::create_directories(dir);
        return dir / "library.db";
    }
#endif
    auto dir = fs::path(getenv("HOME") ? getenv("HOME") : "/tmp") / ".yawn";
    fs::create_directories(dir);
    return dir / "library.db";
}

bool LibraryDatabase::open() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) return true;

    auto path = databasePath().string();
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("LibraryDB", "Failed to open %s: %s", path.c_str(), sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // Enable WAL mode for concurrent reads during writes
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");

    createTables();
    LOG_INFO("LibraryDB", "Opened %s", path.c_str());
    return true;
}

void LibraryDatabase::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void LibraryDatabase::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        LOG_ERROR("LibraryDB", "SQL error: %s — %s", err, sql);
        sqlite3_free(err);
    }
}

void LibraryDatabase::createTables() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS library_paths (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL
        );
    )");
    exec(R"(
        CREATE TABLE IF NOT EXISTS audio_files (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            path            TEXT UNIQUE NOT NULL,
            name            TEXT NOT NULL,
            extension       TEXT NOT NULL,
            duration        REAL DEFAULT 0,
            sample_rate     INTEGER DEFAULT 0,
            channels        INTEGER DEFAULT 0,
            file_size       INTEGER DEFAULT 0,
            last_modified   INTEGER DEFAULT 0,
            library_path_id INTEGER REFERENCES library_paths(id)
        );
    )");
    exec(R"(
        CREATE TABLE IF NOT EXISTS presets (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            path          TEXT UNIQUE NOT NULL,
            name          TEXT NOT NULL,
            device_id     TEXT NOT NULL,
            device_name   TEXT DEFAULT '',
            genre         TEXT DEFAULT '',
            instrument    TEXT DEFAULT '',
            last_modified INTEGER DEFAULT 0
        );
    )");
    exec("CREATE INDEX IF NOT EXISTS idx_audio_name ON audio_files(name)");
    exec("CREATE INDEX IF NOT EXISTS idx_audio_lib  ON audio_files(library_path_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_preset_dev ON presets(device_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_preset_name ON presets(name)");
}

// ── Library paths ──────────────────────────────────────────────────────

std::vector<LibraryPath> LibraryDatabase::getLibraryPaths() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<LibraryPath> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT id, path FROM library_paths ORDER BY path", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            LibraryPath lp;
            lp.id   = sqlite3_column_int64(stmt, 0);
            lp.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            result.push_back(std::move(lp));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

int64_t LibraryDatabase::addLibraryPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return -1;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT OR IGNORE INTO library_paths(path) VALUES(?)", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    // Return the id (whether just inserted or already existed)
    int64_t id = -1;
    if (sqlite3_prepare_v2(m_db, "SELECT id FROM library_paths WHERE path=?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

void LibraryDatabase::removeLibraryPath(int64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    // Remove associated audio files first
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM audio_files WHERE library_path_id=?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(m_db, "DELETE FROM library_paths WHERE id=?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

// ── Audio files ────────────────────────────────────────────────────────

void LibraryDatabase::insertOrUpdateAudioFile(const AudioFileRecord& rec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    const char* sql = R"(
        INSERT INTO audio_files(path, name, extension, duration, sample_rate, channels, file_size, last_modified, library_path_id)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            name=excluded.name, extension=excluded.extension, duration=excluded.duration,
            sample_rate=excluded.sample_rate, channels=excluded.channels,
            file_size=excluded.file_size, last_modified=excluded.last_modified
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text (stmt, 1, rec.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 2, rec.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 3, rec.extension.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, rec.duration);
        sqlite3_bind_int  (stmt, 5, rec.sampleRate);
        sqlite3_bind_int  (stmt, 6, rec.channels);
        sqlite3_bind_int64(stmt, 7, rec.fileSize);
        sqlite3_bind_int64(stmt, 8, rec.lastModified);
        sqlite3_bind_int64(stmt, 9, rec.libraryPathId);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

void LibraryDatabase::removeAudioFilesForPath(int64_t libraryPathId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM audio_files WHERE library_path_id=?", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, libraryPathId);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

void LibraryDatabase::removeDeletedFiles(int64_t libraryPathId,
                                          const std::vector<std::string>& existingPaths) {
    // Get all paths currently in DB for this library path
    auto current = getAudioFilesForPath(libraryPathId);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    // Build a set of existing paths for fast lookup
    std::unordered_set<std::string> existing(existingPaths.begin(), existingPaths.end());

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM audio_files WHERE path=?", -1, &stmt, nullptr) == SQLITE_OK) {
        for (auto& rec : current) {
            if (existing.find(rec.path) == existing.end()) {
                sqlite3_bind_text(stmt, 1, rec.path.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
            }
        }
    }
    sqlite3_finalize(stmt);
}

std::vector<AudioFileRecord> LibraryDatabase::getAudioFilesForPath(int64_t libraryPathId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AudioFileRecord> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,path,name,extension,duration,sample_rate,channels,file_size,last_modified,library_path_id "
                      "FROM audio_files WHERE library_path_id=? ORDER BY path";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, libraryPathId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AudioFileRecord r;
            r.id            = sqlite3_column_int64(stmt, 0);
            r.path          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.name          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            r.extension     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            r.duration      = static_cast<float>(sqlite3_column_double(stmt, 4));
            r.sampleRate    = sqlite3_column_int(stmt, 5);
            r.channels      = sqlite3_column_int(stmt, 6);
            r.fileSize      = sqlite3_column_int64(stmt, 7);
            r.lastModified  = sqlite3_column_int64(stmt, 8);
            r.libraryPathId = sqlite3_column_int64(stmt, 9);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<AudioFileRecord> LibraryDatabase::getAllAudioFiles() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AudioFileRecord> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,path,name,extension,duration,sample_rate,channels,file_size,last_modified,library_path_id "
                      "FROM audio_files ORDER BY path";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AudioFileRecord r;
            r.id            = sqlite3_column_int64(stmt, 0);
            r.path          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.name          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            r.extension     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            r.duration      = static_cast<float>(sqlite3_column_double(stmt, 4));
            r.sampleRate    = sqlite3_column_int(stmt, 5);
            r.channels      = sqlite3_column_int(stmt, 6);
            r.fileSize      = sqlite3_column_int64(stmt, 7);
            r.lastModified  = sqlite3_column_int64(stmt, 8);
            r.libraryPathId = sqlite3_column_int64(stmt, 9);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<AudioFileRecord> LibraryDatabase::searchAudioFiles(const std::string& query) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AudioFileRecord> result;
    if (!m_db || query.empty()) return result;

    // Search in both name and full path
    std::string pattern = "%" + query + "%";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,path,name,extension,duration,sample_rate,channels,file_size,last_modified,library_path_id "
                      "FROM audio_files WHERE name LIKE ? OR path LIKE ? ORDER BY name LIMIT 200";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AudioFileRecord r;
            r.id            = sqlite3_column_int64(stmt, 0);
            r.path          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.name          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            r.extension     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            r.duration      = static_cast<float>(sqlite3_column_double(stmt, 4));
            r.sampleRate    = sqlite3_column_int(stmt, 5);
            r.channels      = sqlite3_column_int(stmt, 6);
            r.fileSize      = sqlite3_column_int64(stmt, 7);
            r.lastModified  = sqlite3_column_int64(stmt, 8);
            r.libraryPathId = sqlite3_column_int64(stmt, 9);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

// ── Presets ────────────────────────────────────────────────────────────

void LibraryDatabase::insertOrUpdatePreset(const PresetRecord& rec) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    const char* sql = R"(
        INSERT INTO presets(path, name, device_id, device_name, genre, instrument, last_modified)
        VALUES(?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            name=excluded.name, device_id=excluded.device_id, device_name=excluded.device_name,
            genre=excluded.genre, instrument=excluded.instrument, last_modified=excluded.last_modified
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text (stmt, 1, rec.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 2, rec.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 3, rec.deviceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 4, rec.deviceName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 5, rec.genre.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 6, rec.instrument.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, rec.lastModified);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

void LibraryDatabase::clearPresets() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;
    exec("DELETE FROM presets");
}

std::vector<PresetRecord> LibraryDatabase::getAllPresets() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PresetRecord> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,path,name,device_id,device_name,genre,instrument,last_modified "
                      "FROM presets ORDER BY device_name, name";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            r.id           = sqlite3_column_int64(stmt, 0);
            r.path         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.name         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            r.deviceId     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            r.deviceName   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            r.genre        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            r.instrument   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            r.lastModified = sqlite3_column_int64(stmt, 7);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PresetRecord> LibraryDatabase::searchPresets(const std::string& query) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PresetRecord> result;
    if (!m_db || query.empty()) return result;

    std::string pattern = "%" + query + "%";
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,path,name,device_id,device_name,genre,instrument,last_modified "
                      "FROM presets WHERE name LIKE ? OR device_name LIKE ? OR genre LIKE ? OR instrument LIKE ? "
                      "ORDER BY device_name, name LIMIT 200";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, pattern.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            r.id           = sqlite3_column_int64(stmt, 0);
            r.path         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.name         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            r.deviceId     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            r.deviceName   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            r.genre        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            r.instrument   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            r.lastModified = sqlite3_column_int64(stmt, 7);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PresetRecord> LibraryDatabase::getPresetsForDevice(const std::string& deviceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PresetRecord> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id,path,name,device_id,device_name,genre,instrument,last_modified "
                      "FROM presets WHERE device_id=? ORDER BY name";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            r.id           = sqlite3_column_int64(stmt, 0);
            r.path         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            r.name         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            r.deviceId     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            r.deviceName   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            r.genre        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            r.instrument   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            r.lastModified = sqlite3_column_int64(stmt, 7);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace library
} // namespace yawn
