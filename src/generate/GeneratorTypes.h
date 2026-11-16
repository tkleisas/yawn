#pragma once

// GeneratorTypes — plain data structs shared by the Lua generator host,
// the app commit layer, and (later) the CLAP plugin shell. No UI/audio
// dependencies; see docs/generative-song-design.md.

#include <cstdint>
#include <string>
#include <vector>

namespace yawn {
namespace generate {

struct StyleMix {
    std::string id;
    float weight = 1.0f;
};

// Parameters handed to the Lua generator. Sentinel values:
//   scale/root empty      -> style decides
//   movement "auto"       -> style decides
//   density/noteLength/swing < 0 -> style decides
//   tempo 0               -> style decides
//   sections empty        -> style template
struct GeneratorParams {
    uint64_t seed = 1;
    std::vector<StyleMix> styles{{"pop", 1.0f}};
    std::string blend = "fuse";      // "fuse" | "rotate"
    std::string scale;
    std::string root;
    std::string movement = "auto";
    bool generateVisuals = false;
    std::string visualTheme = "auto"; // auto | sacred | cosmic | ink | abstract
    float visualReactivity = 1.0f;    // 0 = static .. 2 = exaggerated
    float density = -1.0f;
    float noteLength = -1.0f;
    float swing = -1.0f;
    int tempo = 0;
    int beatsPerBar = 4;
    std::vector<std::string> sections;
};

struct VisualShaderParamInfo {
    std::string name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
};

// One shader the visual generator may choose. The host builds this
// catalog from bundled + project shader folders; Lua selects an id and
// sets its @range params.
struct VisualShaderInfo {
    std::string id;      // file stem, e.g. "32_mandala_seed"
    std::string path;    // resolvable path handed to VisualClip
    std::vector<VisualShaderParamInfo> params;
};

struct GeneratedNote {
    double startBeat = 0.0;
    double duration = 0.25;
    int pitch = 60;
    float cents = 0.0f;
    int velocity = 100;
    int channel = 0;
};

struct GeneratedClip {
    std::string role;
    double lengthBeats = 4.0;
    std::vector<GeneratedNote> notes;
};

struct GeneratedVisual {
    std::string shaderId;
    std::string shaderPath;
    std::string name;
    std::string role;       // drums / bass / chords / lead / visual
    std::string audioRole;  // which lane's audio drives this visual
    std::string image;      // bundled seed image filename, optional
    float reactivity = 1.0f;
    std::vector<std::pair<std::string, float>> params;
};

struct GeneratedSection {
    std::string id;
    std::string name;
    int bars = 4;
    std::vector<GeneratedClip> clips;
    std::vector<GeneratedVisual> visuals;
};

struct GeneratedSong {
    std::string title = "Untitled";
    int tempo = 120;
    int beatsPerBar = 4;
    std::vector<GeneratedSection> sections;

    double totalBeats() const {
        double total = 0.0;
        for (const auto& s : sections)
            total += static_cast<double>(s.bars) * beatsPerBar;
        return total;
    }

    int totalVisuals() const {
        int n = 0;
        for (const auto& s : sections)
            n += static_cast<int>(s.visuals.size());
        return n;
    }

    int totalNotes() const {
        int n = 0;
        for (const auto& s : sections)
            for (const auto& c : s.clips)
                n += static_cast<int>(c.notes.size());
        return n;
    }
};

} // namespace generate
} // namespace yawn
