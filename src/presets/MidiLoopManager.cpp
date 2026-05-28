#include "presets/MidiLoopManager.h"

#include "util/MidiFileIO.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace yawn {

namespace fs = std::filesystem;

const std::vector<std::string>& MidiLoopManager::categories() {
    // Canonical order — also the order the Browser dropdown shows them.
    static const std::vector<std::string> kCats = {
        "drums", "bass", "lead", "chord", "misc"
    };
    return kCats;
}

bool MidiLoopManager::isValidCategory(const std::string& cat) {
    const auto& cs = categories();
    return std::find(cs.begin(), cs.end(), cat) != cs.end();
}

fs::path MidiLoopManager::midiLoopsRootDir() {
    fs::path root;
#ifdef _WIN32
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        root = fs::path(appData) / "YAWN" / "midi_loops";
    } else {
        root = fs::path(".") / "midi_loops";
    }
#else
    const char* home = std::getenv("HOME");
    root = fs::path(home ? home : "/tmp") / ".yawn" / "midi_loops";
#endif
    std::error_code ec;
    fs::create_directories(root, ec);
    return root;
}

std::string MidiLoopManager::sanitizeName(const std::string& name) {
    // Strip characters that are illegal in Windows filenames; collapse
    // whitespace at the edges. Same lightweight rule PresetManager uses
    // — accept reasonable Unicode, drop the obviously-broken stuff.
    static const std::string illegal = "<>:\"/\\|?*";
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (illegal.find(c) != std::string::npos) continue;
        if (static_cast<unsigned char>(c) < 0x20) continue;   // control chars
        out.push_back(c);
    }
    auto trim = [](std::string& s) {
        while (!s.empty() && (s.back()  == ' ' || s.back()  == '.')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && s[i] == ' ') ++i;
        if (i > 0) s.erase(0, i);
    };
    trim(out);
    if (out.empty()) out = "untitled";
    return out;
}

fs::path MidiLoopManager::saveMidiLoop(const std::string& name,
                                       const std::string& category,
                                       const midi::MidiClip& clip,
                                       double tempoBPM,
                                       int    timeSigNum,
                                       int    timeSigDen) {
    const std::string cat = isValidCategory(category) ? category : "misc";
    fs::path dir = midiLoopsRootDir() / cat;
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string safe = sanitizeName(name);
    fs::path file = dir / (safe + ".mid");

    // Don't silently overwrite — if it already exists, dedupe with a
    // numeric suffix ("foo.mid" → "foo (2).mid", etc.).
    int n = 2;
    while (fs::exists(file, ec)) {
        file = dir / (safe + " (" + std::to_string(n++) + ").mid");
        if (n > 9999) break;
    }

    if (!util::saveMidiFile(file, clip, tempoBPM, timeSigNum, timeSigDen))
        return {};
    return file;
}

const char* MidiLoopManager::autoCategory(const midi::MidiClip& clip) {
    const int n = clip.noteCount();
    if (n == 0) return "misc";

    int drumNotes = 0;
    int low = 127, high = 0;
    for (int i = 0; i < n; ++i) {
        const auto& note = clip.note(i);
        if (note.channel == 9) ++drumNotes;            // GM drum channel
        if (note.pitch < low)  low  = note.pitch;
        if (note.pitch > high) high = note.pitch;
    }
    // GM drum convention: any clip with a majority of notes on channel
    // 9 is treated as drums regardless of pitch range.
    if (drumNotes * 2 >= n) return "drums";

    const double len  = std::max(0.25, clip.lengthBeats());
    const double rate = static_cast<double>(n) / len;   // notes per beat

    if (high <= 55) return "bass";        // ceiling at G3 → low / sub register
    if (rate >= 3.0) return "chord";      // dense polyphony → chord stab/progression
    if (rate <= 2.0 && (high - low) <= 24) return "lead";   // mono-ish melodic line
    return "misc";
}

} // namespace yawn
