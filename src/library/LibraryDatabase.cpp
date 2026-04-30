#include "LibraryDatabase.h"
#include "util/Logger.h"
#include <sqlite3.h>
#include <cstdlib>
#include <string>
#include <tuple>
#include <unordered_set>

#ifdef _WIN32
#  include <shlobj.h>
#endif

namespace yawn {
namespace library {

// ── Device type classifier ─────────────────────────────────────────────
//
// Match the factory mapping in App.cpp. Kept in one place so both the
// library scanner (new inserts) and the schema migration (back-fill)
// see the same truth. When a new built-in device ships, add its name
// to the matching list below — existing presets will be re-classified
// the next time a scan runs (scanner calls insertOrUpdatePreset with
// ON CONFLICT DO UPDATE, which refreshes device_type).
//
// VST3s and other dynamic devices currently fall through to "" — they
// show in the "All" view but are filtered out of Instruments/Effects.
// Promoting them is a follow-up: VST3Scanner already knows if a plugin
// is a kInstrument or kFx, we just need to plumb the category into
// the preset JSON on save.

static bool isBuiltInInstrument(const std::string& n) {
    return n == "Subtractive Synth" || n == "FM Synth" || n == "Sampler" ||
           n == "Drum Rack"         || n == "Drum Synth" ||
           n == "DrumSlop"          || n == "Karplus-Strong" ||
           n == "Wavetable Synth"   || n == "Granular Synth" || n == "Vocoder" ||
           n == "Multisampler"      || n == "Instrument Rack";
}

static bool isBuiltInEffect(const std::string& n) {
    // Audio effects.
    if (n == "Reverb"       || n == "Delay"        || n == "EQ" ||
        n == "Compressor"   || n == "Filter"       || n == "Chorus" ||
        n == "Distortion"   || n == "Bitcrusher"   || n == "Noise Gate" ||
        n == "Ping-Pong Delay" || n == "Envelope Follower" ||
        n == "Spline EQ" || n == "Neural Amp" ||
        n == "Tape Emulation" ||
        n == "Amp Simulator" ||
        n == "Oscilloscope" || n == "Spectrum Analyzer" || n == "Spectrum" ||
        n == "Tuner")
        return true;
    // MIDI effects — lumped with audio effects for the 2-bucket filter.
    if (n == "Arpeggiator"  || n == "Chord"        || n == "Scale" ||
        n == "Note Length"  || n == "Velocity"     || n == "Random" ||
        n == "MIDI Random"  || n == "Pitch"        || n == "MIDI Pitch" ||
        n == "LFO")
        return true;
    return false;
}

const char* classifyDeviceType(const std::string& /*deviceId*/,
                                const std::string& deviceName) {
    if (isBuiltInInstrument(deviceName)) return "instrument";
    if (isBuiltInEffect(deviceName))     return "effect";
    return "";
}

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
    migrateSchema();
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
    // device_type is included from schema v1 onward so fresh DBs don't
    // need the ALTER step in migrateSchema().
    exec(R"(
        CREATE TABLE IF NOT EXISTS presets (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            path          TEXT UNIQUE NOT NULL,
            name          TEXT NOT NULL,
            device_id     TEXT NOT NULL,
            device_name   TEXT DEFAULT '',
            device_type   TEXT DEFAULT '',
            genre         TEXT DEFAULT '',
            instrument    TEXT DEFAULT '',
            last_modified INTEGER DEFAULT 0
        );
    )");
    exec("CREATE INDEX IF NOT EXISTS idx_audio_name ON audio_files(name)");
    exec("CREATE INDEX IF NOT EXISTS idx_audio_lib  ON audio_files(library_path_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_preset_dev ON presets(device_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_preset_name ON presets(name)");
    exec("CREATE INDEX IF NOT EXISTS idx_preset_type ON presets(device_type)");
}

// ─── Schema migrations ─────────────────────────────────────────────────
// Invariant: createTables() handles fresh installs; migrateSchema()
// handles upgrades of existing DBs. Each migrate-to-vN is idempotent
// and only touches rows that need it. user_version is the checkpoint —
// a half-finished run restarts cleanly on the next open.

void LibraryDatabase::migrateSchema() {
    // PRECONDITION: the caller (currently only open()) already holds
    // m_mutex. Taking it again here would be a self-deadlock on a
    // non-recursive mutex (MSVC throws resource_deadlock_would_occur,
    // everyone else is UB). Any future entry point that calls into
    // migrateSchema from an unlocked context must acquire the mutex
    // itself first.
    if (!m_db) return;

    int userVersion = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            userVersion = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (userVersion >= kSchemaVersion) return;

    LOG_INFO("LibraryDB", "Migrating schema %d → %d", userVersion, kSchemaVersion);

    // v0 → v1: add device_type column (if missing) and back-fill from
    // the classifier. Fresh DBs already have the column from
    // createTables; the ALTER is only needed when upgrading a pre-v1
    // install.
    if (userVersion < 1) {
        bool hasCol = false;
        if (sqlite3_prepare_v2(m_db, "PRAGMA table_info(presets)", -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(stmt, 1);
                if (name && std::string(reinterpret_cast<const char*>(name)) == "device_type") {
                    hasCol = true;
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);

        if (!hasCol) {
            char* err = nullptr;
            if (sqlite3_exec(m_db,
                    "ALTER TABLE presets ADD COLUMN device_type TEXT DEFAULT ''",
                    nullptr, nullptr, &err) != SQLITE_OK) {
                LOG_ERROR("LibraryDB", "ALTER TABLE failed: %s", err ? err : "?");
                sqlite3_free(err);
                return;  // leave user_version unchanged so we retry next boot
            }
            sqlite3_exec(m_db,
                "CREATE INDEX IF NOT EXISTS idx_preset_type ON presets(device_type)",
                nullptr, nullptr, nullptr);
        }

        // Back-fill device_type for rows that still have the default "".
        std::vector<std::tuple<int64_t, std::string, std::string>> toUpdate;
        if (sqlite3_prepare_v2(m_db,
                "SELECT id, device_id, device_name FROM presets WHERE device_type = ''",
                -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                toUpdate.emplace_back(
                    sqlite3_column_int64(stmt, 0),
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)),
                    reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))
                );
            }
        }
        sqlite3_finalize(stmt);

        int classified = 0;
        if (sqlite3_prepare_v2(m_db,
                "UPDATE presets SET device_type = ? WHERE id = ?",
                -1, &stmt, nullptr) == SQLITE_OK) {
            for (auto& row : toUpdate) {
                const char* t = classifyDeviceType(std::get<1>(row), std::get<2>(row));
                if (t[0] == '\0') continue;   // leave unknown rows as ''
                sqlite3_bind_text (stmt, 1, t, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(stmt, 2, std::get<0>(row));
                sqlite3_step(stmt);
                sqlite3_reset(stmt);
                ++classified;
            }
        }
        sqlite3_finalize(stmt);

        LOG_INFO("LibraryDB", "v1 migration: classified %d of %zu unknown rows",
                 classified, toUpdate.size());
    }

    // Stamp user_version at the end so partial migrations re-run on
    // next open.
    exec(("PRAGMA user_version = " + std::to_string(kSchemaVersion)).c_str());
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

    // device_type is set by the caller (LibraryScanner via
    // classifyDeviceType). If empty (unknown VST3 etc.), it's stored
    // as "" and filters skip the row.
    const char* sql = R"(
        INSERT INTO presets(path, name, device_id, device_name, device_type,
                             genre, instrument, last_modified)
        VALUES(?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            name=excluded.name, device_id=excluded.device_id, device_name=excluded.device_name,
            device_type=excluded.device_type,
            genre=excluded.genre, instrument=excluded.instrument, last_modified=excluded.last_modified
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text (stmt, 1, rec.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 2, rec.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 3, rec.deviceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 4, rec.deviceName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 5, rec.deviceType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 6, rec.genre.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (stmt, 7, rec.instrument.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 8, rec.lastModified);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

void LibraryDatabase::clearPresets() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;
    exec("DELETE FROM presets");
}

namespace {
// Shared row reader — all preset SELECTs produce the same 9-column
// projection. Centralised so schema changes touch one spot.
void readPresetRow(sqlite3_stmt* stmt, PresetRecord& r) {
    r.id           = sqlite3_column_int64(stmt, 0);
    r.path         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    r.name         = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    r.deviceId     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    r.deviceName   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const unsigned char* devType = sqlite3_column_text(stmt, 5);
    r.deviceType   = devType ? reinterpret_cast<const char*>(devType) : "";
    r.genre        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    r.instrument   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    r.lastModified = sqlite3_column_int64(stmt, 8);
}
constexpr const char* kPresetColumns =
    "id,path,name,device_id,device_name,device_type,genre,instrument,last_modified";
} // anon

std::vector<PresetRecord> LibraryDatabase::getAllPresets() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PresetRecord> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + kPresetColumns +
                      " FROM presets ORDER BY device_name, name";
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            readPresetRow(stmt, r);
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
    std::string sql = std::string("SELECT ") + kPresetColumns +
                      " FROM presets WHERE name LIKE ? OR device_name LIKE ? OR genre LIKE ? OR instrument LIKE ? "
                      "ORDER BY device_name, name LIMIT 200";
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, pattern.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            readPresetRow(stmt, r);
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
    std::string sql = std::string("SELECT ") + kPresetColumns +
                      " FROM presets WHERE device_id=? ORDER BY name";
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, deviceId.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            readPresetRow(stmt, r);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PresetRecord> LibraryDatabase::getFilteredPresets(const std::string& deviceType) {
    // Empty string or the sentinel "all" returns everything.
    if (deviceType.empty() || deviceType == "all")
        return getAllPresets();

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PresetRecord> result;
    if (!m_db) return result;

    sqlite3_stmt* stmt = nullptr;
    std::string sql = std::string("SELECT ") + kPresetColumns +
                      " FROM presets WHERE device_type=? ORDER BY device_name, name";
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, deviceType.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PresetRecord r;
            readPresetRow(stmt, r);
            result.push_back(std::move(r));
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace library
} // namespace yawn
