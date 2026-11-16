#pragma once

// SongCommitter — turns a validated GeneratedSong into session scenes
// and MIDI clips in the project. UI-free and engine-free so the commit
// path is testable headlessly. Roles (drums/bass/chords/lead/…) are
// mapped to destination tracks by the caller; sections become appended
// scenes with follow actions chaining them.

#include "generate/GeneratorTypes.h"

#include <string>
#include <vector>

namespace yawn {
class Project;

namespace generate {

struct CommitResult {
    int firstScene = -1;
    int sceneCount = 0;
    std::vector<std::string> roles;
};

// All role names used anywhere in the song, first-appearance order.
std::vector<std::string> collectRoles(const GeneratedSong& song);

// Validates the mapping, then appends one scene per section (named
// "NN Section") and writes each lane's clip to its mapped track.
// Follow actions chain section N to N+1 and stop after the last one.
// On failure nothing is appended.
bool commitGeneratedSong(Project& project, const GeneratedSong& song,
                         const std::vector<int>& roleTracks,
                         CommitResult* result, std::string* error);

// Undo helper: removes the last `count` scenes while preserving the
// names of the scenes that remain (Project::deleteScene renumbers).
int removeLastScenesPreservingNames(Project& project, int count);

} // namespace generate
} // namespace yawn
