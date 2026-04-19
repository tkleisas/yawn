#pragma once

#include "core/Constants.h"
#include "app/ArrangementClip.h"
#include "audio/Clip.h"
#include "audio/ClipEngine.h"
#include "audio/FollowAction.h"
#include "midi/MidiClip.h"
#include "visual/VisualClip.h"
#include "automation/AutomationLane.h"
#include <vector>
#include <memory>
#include <string>
#include <algorithm>

namespace yawn {

struct Track {
    enum class Type : uint8_t { Audio, Midi, Visual };
    enum class MonitorMode : uint8_t { Auto, In, Off };
    // Blend mode used when compositing this visual track over lower ones.
    // Ignored for non-Visual tracks.
    enum class VisualBlendMode : uint8_t { Normal, Add, Multiply, Screen };

    std::string name;
    Type type = Type::Audio;
    int colorIndex = 0;
    float volume = 1.0f;
    bool muted = false;
    bool soloed = false;
    int midiInputPort = -1;   // -1 = all ports
    int midiInputChannel = -1; // -1 = all channels
    int midiOutputPort = -1;   // -1 = none
    int midiOutputChannel = -1; // -1 = all channels
    int audioInputCh = 1;     // 1=In1, 2=In2, 3=In1+2, 4=In3, 5=In3+4, etc. (0=none)
    bool mono = false;
    int sidechainSource = -1;  // -1=none, track index for sidechain input to instrument
    int resampleSource = -1;   // -1=none, track index for resampling audio input
    bool armed = false;
    MonitorMode monitorMode = MonitorMode::Auto;
    VisualBlendMode visualBlendMode = VisualBlendMode::Normal;
    audio::QuantizeMode recordQuantize = audio::QuantizeMode::NextBar;
    int recordLengthBars = 0;  // 0 = unlimited (free recording)

    // Default clip: last-launched scene index per track (-1 = none)
    // When transport plays, clips at defaultScene are re-launched.
    int defaultScene = -1;

    // Per-track automation lanes (arrangement-level, absolute beat times)
    std::vector<automation::AutomationLane> automationLanes;
    automation::AutoMode autoMode = automation::AutoMode::Off;

    // Arrangement clips (sorted by startBeat)
    std::vector<ArrangementClip> arrangementClips;
    bool arrangementActive = false;  // true = arrangement mode, false = session mode

    // Get arrangement clip at a given beat position
    const ArrangementClip* arrangementClipAt(double beat) const {
        for (auto& c : arrangementClips)
            if (beat >= c.startBeat && beat < c.endBeat()) return &c;
        return nullptr;
    }

    void sortArrangementClips() {
        std::sort(arrangementClips.begin(), arrangementClips.end(),
                  [](const ArrangementClip& a, const ArrangementClip& b) {
                      return a.startBeat < b.startBeat;
                  });
    }
};

struct Scene {
    std::string name;
};

// A clip slot can hold either an audio clip or a MIDI clip (or be empty)

struct ClipSlot {
    enum class Type { Empty, Audio, Midi, Visual };

    Type type() const {
        if (audioClip)  return Type::Audio;
        if (midiClip)   return Type::Midi;
        if (visualClip) return Type::Visual;
        return Type::Empty;
    }

    bool empty() const { return !audioClip && !midiClip && !visualClip; }

    void clear() {
        audioClip.reset();
        midiClip.reset();
        visualClip.reset();
        clipAutomation.clear();
    }

    std::unique_ptr<audio::Clip> audioClip;
    std::unique_ptr<midi::MidiClip> midiClip;
    std::unique_ptr<visual::VisualClip> visualClip;
    audio::QuantizeMode launchQuantize = audio::QuantizeMode::NextBar;

    // Follow action — triggers after clip plays for N bars
    FollowAction followAction;

    // Per-clip automation lanes (times relative to clip start, loop with clip)
    std::vector<automation::AutomationLane> clipAutomation;
};

// Which top-level view is displayed (UI only; per-track mode is Track::arrangementActive)
enum class ViewMode : uint8_t { Session, Arrangement };

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
        slot->midiClip.reset();   // Clear any existing MIDI clip
        slot->visualClip.reset(); // Clear any existing visual clip
        slot->audioClip = std::move(clip);
        return slot->audioClip.get();
    }

    // Set MIDI clip
    midi::MidiClip* setMidiClip(int trackIndex, int sceneIndex, std::unique_ptr<midi::MidiClip> clip) {
        auto* slot = getSlot(trackIndex, sceneIndex);
        if (!slot) return nullptr;
        slot->audioClip.reset();   // Clear any existing audio clip
        slot->visualClip.reset();  // Clear any existing visual clip
        slot->midiClip = std::move(clip);
        return slot->midiClip.get();
    }

    // Set visual clip
    visual::VisualClip* setVisualClip(int trackIndex, int sceneIndex,
                                       std::unique_ptr<visual::VisualClip> clip) {
        auto* slot = getSlot(trackIndex, sceneIndex);
        if (!slot) return nullptr;
        slot->audioClip.reset();
        slot->midiClip.reset();
        slot->visualClip = std::move(clip);
        return slot->visualClip.get();
    }

    // Move a clip slot's contents (audio/MIDI/visual + follow action + automation)
    void moveSlot(int srcTrack, int srcScene, int dstTrack, int dstScene) {
        auto* src = getSlot(srcTrack, srcScene);
        auto* dst = getSlot(dstTrack, dstScene);
        if (!src || !dst || src == dst) return;
        dst->audioClip      = std::move(src->audioClip);
        dst->midiClip       = std::move(src->midiClip);
        dst->visualClip     = std::move(src->visualClip);
        dst->followAction   = src->followAction;
        dst->clipAutomation = std::move(src->clipAutomation);
        dst->launchQuantize = src->launchQuantize;
        src->followAction   = FollowAction{};
        src->clipAutomation.clear();
        src->launchQuantize = audio::QuantizeMode::NextBar;
    }

    // Copy a clip slot's contents (clones the clip)
    void copySlot(int srcTrack, int srcScene, int dstTrack, int dstScene) {
        auto* src = getSlot(srcTrack, srcScene);
        auto* dst = getSlot(dstTrack, dstScene);
        if (!src || !dst || src == dst) return;
        if (src->audioClip)   dst->audioClip  = src->audioClip->clone();  else dst->audioClip.reset();
        if (src->midiClip)    dst->midiClip   = src->midiClip->clone();   else dst->midiClip.reset();
        if (src->visualClip)  dst->visualClip = src->visualClip->clone(); else dst->visualClip.reset();
        dst->followAction   = src->followAction;
        dst->clipAutomation = src->clipAutomation;
        dst->launchQuantize = src->launchQuantize;
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

    // Remove the last track (used by undo of Add Track)
    void removeLastTrack() {
        if (m_tracks.empty()) return;
        m_tracks.pop_back();
        m_clipSlots.pop_back();
    }

    void addScene() {
        Scene s;
        s.name = std::to_string(m_scenes.size() + 1);
        m_scenes.push_back(s);

        for (auto& trackSlots : m_clipSlots) {
            trackSlots.emplace_back();
        }
    }

    void insertScene(int index) {
        if (index < 0 || index > static_cast<int>(m_scenes.size())) return;
        Scene s;
        s.name = std::to_string(index + 1);
        m_scenes.insert(m_scenes.begin() + index, s);
        for (auto& trackSlots : m_clipSlots) {
            trackSlots.insert(trackSlots.begin() + index, ClipSlot{});
        }
        for (auto& t : m_tracks) {
            if (t.defaultScene >= index) t.defaultScene++;
        }
        renumberScenes();
    }

    void deleteScene(int index) {
        if (index < 0 || index >= static_cast<int>(m_scenes.size())) return;
        if (m_scenes.size() <= 1) return; // keep at least 1 scene
        m_scenes.erase(m_scenes.begin() + index);
        for (auto& trackSlots : m_clipSlots) {
            trackSlots.erase(trackSlots.begin() + index);
        }
        for (auto& t : m_tracks) {
            if (t.defaultScene == index) t.defaultScene = -1;
            else if (t.defaultScene > index) t.defaultScene--;
        }
        renumberScenes();
    }

    void duplicateScene(int index) {
        if (index < 0 || index >= static_cast<int>(m_scenes.size())) return;
        int dst = index + 1;
        Scene s;
        s.name = m_scenes[index].name + " copy";
        m_scenes.insert(m_scenes.begin() + dst, s);
        for (auto& trackSlots : m_clipSlots) {
            auto srcSlot = ClipSlot{};
            auto& orig = trackSlots[index];
            if (orig.audioClip)  srcSlot.audioClip  = orig.audioClip->clone();
            if (orig.midiClip)   srcSlot.midiClip   = orig.midiClip->clone();
            if (orig.visualClip) srcSlot.visualClip = orig.visualClip->clone();
            srcSlot.launchQuantize = orig.launchQuantize;
            srcSlot.followAction = orig.followAction;
            srcSlot.clipAutomation = orig.clipAutomation;
            trackSlots.insert(trackSlots.begin() + dst, std::move(srcSlot));
        }
        for (auto& t : m_tracks) {
            if (t.defaultScene >= dst) t.defaultScene++;
        }
    }

    void deleteTrack(int index) {
        if (index < 0 || index >= static_cast<int>(m_tracks.size())) return;
        if (m_tracks.size() <= 1) return; // keep at least 1 track
        m_tracks.erase(m_tracks.begin() + index);
        m_clipSlots.erase(m_clipSlots.begin() + index);
        // Fix sidechain/resample references
        for (auto& t : m_tracks) {
            if (t.sidechainSource == index) t.sidechainSource = -1;
            else if (t.sidechainSource > index) t.sidechainSource--;
            if (t.resampleSource == index) t.resampleSource = -1;
            else if (t.resampleSource > index) t.resampleSource--;
        }
    }

    void insertTrack(int index, const Track& track, std::vector<ClipSlot> slots) {
        if (index < 0 || index > static_cast<int>(m_tracks.size())) return;
        m_tracks.insert(m_tracks.begin() + index, track);
        m_clipSlots.insert(m_clipSlots.begin() + index, std::move(slots));
    }

    // ─── Arrangement view ──────────────────────────────────────────────
    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode m) { m_viewMode = m; }

    double arrangementLength() const { return m_arrangementLength; }
    void setArrangementLength(double beats) { m_arrangementLength = beats; }

    // Recompute arrangement length from all track clips
    void updateArrangementLength() {
        double maxEnd = 16.0; // minimum 4 bars
        for (auto& t : m_tracks)
            for (auto& c : t.arrangementClips)
                maxEnd = std::max(maxEnd, c.endBeat());
        // Add 16 beats (4 bars) of padding beyond last clip
        m_arrangementLength = maxEnd + 16.0;
    }

private:
    void renumberScenes() {
        for (size_t i = 0; i < m_scenes.size(); ++i)
            m_scenes[i].name = std::to_string(i + 1);
    }

    std::vector<Track> m_tracks;
    std::vector<Scene> m_scenes;
    // m_clipSlots[trackIndex][sceneIndex]
    std::vector<std::vector<ClipSlot>> m_clipSlots;
    ViewMode m_viewMode = ViewMode::Session;
    double m_arrangementLength = 64.0; // default 16 bars
};

} // namespace yawn
