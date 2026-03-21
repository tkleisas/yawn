#pragma once

#include "core/Constants.h"
#include "audio/Clip.h"
#include "midi/MidiClip.h"
#include <vector>
#include <memory>
#include <string>

namespace yawn {

struct Track {
    enum class Type : uint8_t { Audio, Midi };

    std::string name;
    Type type = Type::Audio;
    int colorIndex = 0;
    float volume = 1.0f;
    bool muted = false;
    bool soloed = false;
    int midiInputPort = -1;   // -1 = all ports
    int midiInputChannel = -1; // -1 = all channels
    bool armed = false;        // For MIDI recording
};

struct Scene {
    std::string name;
};

// A clip slot can hold either an audio clip or a MIDI clip (or be empty)
struct ClipSlot {
    enum class Type { Empty, Audio, Midi };

    Type type() const {
        if (audioClip) return Type::Audio;
        if (midiClip) return Type::Midi;
        return Type::Empty;
    }

    bool empty() const { return !audioClip && !midiClip; }

    void clear() { audioClip.reset(); midiClip.reset(); }

    std::unique_ptr<audio::Clip> audioClip;
    std::unique_ptr<midi::MidiClip> midiClip;
};

// Project model: holds tracks, scenes, and the 2D clip grid.
// Owned by the UI thread. Clip pointers are passed to the audio engine.
class Project {
public:
    Project() = default;

    void init(int numTracks = kDefaultNumTracks, int numScenes = kDefaultNumScenes) {
        m_tracks.resize(numTracks);
        m_scenes.resize(numScenes);
        m_clipSlots.resize(numTracks);

        for (int t = 0; t < numTracks; ++t) {
            m_tracks[t].name = "Track " + std::to_string(t + 1);
            m_tracks[t].colorIndex = t;
            m_clipSlots[t].resize(numScenes);
        }
        for (int s = 0; s < numScenes; ++s) {
            m_scenes[s].name = std::to_string(s + 1);
        }
    }

    int numTracks() const { return static_cast<int>(m_tracks.size()); }
    int numScenes() const { return static_cast<int>(m_scenes.size()); }

    Track& track(int index) { return m_tracks[index]; }
    const Track& track(int index) const { return m_tracks[index]; }
    Scene& scene(int index) { return m_scenes[index]; }
    const Scene& scene(int index) const { return m_scenes[index]; }

    // Access the full clip slot (audio + midi)
    ClipSlot* getSlot(int trackIndex, int sceneIndex) {
        if (trackIndex < 0 || trackIndex >= numTracks()) return nullptr;
        if (sceneIndex < 0 || sceneIndex >= numScenes()) return nullptr;
        return &m_clipSlots[trackIndex][sceneIndex];
    }
    const ClipSlot* getSlot(int trackIndex, int sceneIndex) const {
        if (trackIndex < 0 || trackIndex >= numTracks()) return nullptr;
        if (sceneIndex < 0 || sceneIndex >= numScenes()) return nullptr;
        return &m_clipSlots[trackIndex][sceneIndex];
    }

    // Convenience: get audio clip (returns nullptr if slot is empty or MIDI)
    audio::Clip* getClip(int trackIndex, int sceneIndex) {
        auto* slot = getSlot(trackIndex, sceneIndex);
        return slot ? slot->audioClip.get() : nullptr;
    }
    const audio::Clip* getClip(int trackIndex, int sceneIndex) const {
        auto* slot = getSlot(trackIndex, sceneIndex);
        return slot ? slot->audioClip.get() : nullptr;
    }

    // Convenience: get MIDI clip
    midi::MidiClip* getMidiClip(int trackIndex, int sceneIndex) {
        auto* slot = getSlot(trackIndex, sceneIndex);
        return slot ? slot->midiClip.get() : nullptr;
    }
    const midi::MidiClip* getMidiClip(int trackIndex, int sceneIndex) const {
        auto* slot = getSlot(trackIndex, sceneIndex);
        return slot ? slot->midiClip.get() : nullptr;
    }

    // Set audio clip
    audio::Clip* setClip(int trackIndex, int sceneIndex, std::unique_ptr<audio::Clip> clip) {
        auto* slot = getSlot(trackIndex, sceneIndex);
        if (!slot) return nullptr;
        slot->midiClip.reset(); // Clear any existing MIDI clip
        slot->audioClip = std::move(clip);
        return slot->audioClip.get();
    }

    // Set MIDI clip
    midi::MidiClip* setMidiClip(int trackIndex, int sceneIndex, std::unique_ptr<midi::MidiClip> clip) {
        auto* slot = getSlot(trackIndex, sceneIndex);
        if (!slot) return nullptr;
        slot->audioClip.reset(); // Clear any existing audio clip
        slot->midiClip = std::move(clip);
        return slot->midiClip.get();
    }

    void addTrack() {
        Track t;
        t.name = "Track " + std::to_string(m_tracks.size() + 1);
        t.colorIndex = static_cast<int>(m_tracks.size());
        m_tracks.push_back(t);

        m_clipSlots.emplace_back();
        m_clipSlots.back().resize(numScenes());
    }

    void addTrack(const std::string& name, Track::Type type) {
        Track t;
        t.name = name;
        t.type = type;
        t.colorIndex = static_cast<int>(m_tracks.size());
        m_tracks.push_back(t);

        m_clipSlots.emplace_back();
        m_clipSlots.back().resize(numScenes());
    }

    void addScene() {
        Scene s;
        s.name = std::to_string(m_scenes.size() + 1);
        m_scenes.push_back(s);

        for (auto& trackSlots : m_clipSlots) {
            trackSlots.emplace_back();
        }
    }

private:
    std::vector<Track> m_tracks;
    std::vector<Scene> m_scenes;
    // m_clipSlots[trackIndex][sceneIndex]
    std::vector<std::vector<ClipSlot>> m_clipSlots;
};

} // namespace yawn
