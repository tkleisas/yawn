#include "generate/SongCommitter.h"

#include "app/Project.h"
#include "midi/MidiTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace yawn {
namespace generate {

std::vector<std::string> collectRoles(const GeneratedSong& song) {
    std::vector<std::string> roles;
    for (const auto& sec : song.sections) {
        for (const auto& clip : sec.clips) {
            if (std::find(roles.begin(), roles.end(), clip.role) == roles.end())
                roles.push_back(clip.role);
        }
    }
    return roles;
}

bool commitGeneratedSong(Project& project, const GeneratedSong& song,
                         const std::vector<int>& roleTracks,
                         CommitResult* result, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    if (song.sections.empty()) return fail("song has no sections");
    if (song.beatsPerBar < 1 || song.beatsPerBar > 16)
        return fail("invalid beatsPerBar");

    const std::vector<std::string> roles = collectRoles(song);
    if (roles.empty()) return fail("song has no lanes");
    if (roleTracks.size() != roles.size()) {
        return fail("lane mapping has " + std::to_string(roleTracks.size()) +
                    " entries, song has " + std::to_string(roles.size()) +
                    " lanes");
    }

    std::set<int> usedTracks;
    for (int t : roleTracks) {
        if (t < 0 || t >= project.numTracks())
            return fail("destination track out of range");
        if (!usedTracks.insert(t).second)
            return fail("two lanes target the same track");
    }

    // Validate before touching the project.
    for (const auto& sec : song.sections) {
        if (sec.bars < 1 || sec.bars > 256)
            return fail("section '" + sec.name + "' has invalid bars");
        if (sec.clips.empty())
            return fail("section '" + sec.name + "' has no lanes");
        const double secBeats =
            static_cast<double>(sec.bars) * song.beatsPerBar;
        for (const auto& clip : sec.clips) {
            if (clip.lengthBeats <= 0.0 || clip.lengthBeats > secBeats + 1e-6)
                return fail("clip '" + clip.role + "' has invalid length");
            for (const auto& n : clip.notes) {
                if (n.pitch < 0 || n.pitch > 127 || n.velocity < 1 ||
                    n.velocity > 127 || n.startBeat < 0.0 ||
                    n.duration <= 0.0 ||
                    n.startBeat + n.duration > clip.lengthBeats + 1e-6) {
                    return fail("clip '" + clip.role +
                                "' has an invalid note");
                }
                if (!std::isfinite(n.cents) || n.cents < -1200.0f ||
                    n.cents > 1200.0f)
                    return fail("clip '" + clip.role +
                                "' has an invalid cents offset");
            }
        }
    }

    const int firstScene = project.numScenes();
    for (size_t si = 0; si < song.sections.size(); ++si) {
        const auto& sec = song.sections[si];
        project.addScene();
        const int scene = project.numScenes() - 1;

        char sceneName[160];
        std::snprintf(sceneName, sizeof(sceneName), "%02d %s",
                      scene + 1,
                      sec.name.empty() ? sec.id.c_str() : sec.name.c_str());
        project.scene(scene).name = sceneName;

        for (const auto& clip : sec.clips) {
            auto it = std::find(roles.begin(), roles.end(), clip.role);
            if (it == roles.end()) continue;  // unreachable: collectRoles
            const int track = roleTracks[static_cast<size_t>(it - roles.begin())];

            auto mc = std::make_unique<midi::MidiClip>(clip.lengthBeats);
            mc->setName(sec.name + " " + clip.role);
            for (const auto& gn : clip.notes) {
                midi::MidiNote n;
                n.startBeat = gn.startBeat;
                n.duration = gn.duration;
                n.pitch = static_cast<uint8_t>(gn.pitch);
                n.channel = static_cast<uint8_t>(std::clamp(gn.channel, 0, 15));
                n.velocity = midi::Convert::vel7to16(
                    static_cast<uint8_t>(std::clamp(gn.velocity, 1, 127)));
                n.cents = static_cast<float>(
                    std::max(-1200.0, std::min(1200.0,
                        static_cast<double>(gn.cents))));
                mc->addNote(n);
            }
            project.setMidiClip(track, scene, std::move(mc));

            auto* slot = project.getSlot(track, scene);
            if (slot) {
                slot->launchQuantize = audio::QuantizeMode::NextBar;
                slot->followAction.enabled = true;
                slot->followAction.barCount = sec.bars;
                slot->followAction.actionA =
                    (si + 1 == song.sections.size())
                        ? FollowActionType::Stop
                        : FollowActionType::Next;
                slot->followAction.actionB = FollowActionType::None;
                slot->followAction.chanceA = 100;
            }
        }
    }

    if (result) {
        result->firstScene = firstScene;
        result->sceneCount = static_cast<int>(song.sections.size());
        result->roles = roles;
    }
    return true;
}

int removeLastScenesPreservingNames(Project& project, int count) {
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(project.numScenes()));
    for (int i = 0; i < project.numScenes(); ++i)
        names.push_back(project.scene(i).name);

    int removed = 0;
    while (removed < count && project.numScenes() > 1) {
        project.deleteScene(project.numScenes() - 1);
        ++removed;
    }
    // deleteScene renumbers all remaining scenes; restore the original
    // names for the surviving prefix so existing work keeps its labels.
    for (int i = 0; i < project.numScenes() &&
                    i < static_cast<int>(names.size()); ++i) {
        project.scene(i).name = names[static_cast<size_t>(i)];
    }
    return removed;
}

} // namespace generate
} // namespace yawn
