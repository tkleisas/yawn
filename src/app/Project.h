#pragma once

#include "audio/Clip.h"
#include <vector>
#include <memory>
#include <string>

namespace yawn {

struct Track {
    std::string name;
    int colorIndex = 0;
    float volume = 1.0f;
    bool muted = false;
    bool soloed = false;
};

struct Scene {
    std::string name;
};

// Project model: holds tracks, scenes, and the 2D clip grid.
// Owned by the UI thread. Clip pointers are passed to the audio engine.
class Project {
public:
    static constexpr int kDefaultNumTracks = 8;
    static constexpr int kDefaultNumScenes = 8;

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

    audio::Clip* getClip(int trackIndex, int sceneIndex) {
        if (trackIndex < 0 || trackIndex >= numTracks()) return nullptr;
        if (sceneIndex < 0 || sceneIndex >= numScenes()) return nullptr;
        return m_clipSlots[trackIndex][sceneIndex].get();
    }

    const audio::Clip* getClip(int trackIndex, int sceneIndex) const {
        if (trackIndex < 0 || trackIndex >= numTracks()) return nullptr;
        if (sceneIndex < 0 || sceneIndex >= numScenes()) return nullptr;
        return m_clipSlots[trackIndex][sceneIndex].get();
    }

    // Set a clip. Returns the raw pointer (for passing to audio engine).
    audio::Clip* setClip(int trackIndex, int sceneIndex, std::unique_ptr<audio::Clip> clip) {
        if (trackIndex < 0 || trackIndex >= numTracks()) return nullptr;
        if (sceneIndex < 0 || sceneIndex >= numScenes()) return nullptr;
        m_clipSlots[trackIndex][sceneIndex] = std::move(clip);
        return m_clipSlots[trackIndex][sceneIndex].get();
    }

    void addTrack() {
        Track t;
        t.name = "Track " + std::to_string(m_tracks.size() + 1);
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
    std::vector<std::vector<std::unique_ptr<audio::Clip>>> m_clipSlots;
};

} // namespace yawn
