#include <gtest/gtest.h>

#include "generate/SongCommitter.h"
#include "app/Project.h"
#include "midi/MidiTypes.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace yawn;
using namespace yawn::generate;

namespace {

GeneratedClip clipFor(const std::string& role, double lengthBeats) {
    GeneratedClip c;
    c.role = role;
    c.lengthBeats = lengthBeats;
    GeneratedNote n;
    n.startBeat = 0.0;
    n.duration = 0.5;
    n.pitch = 60;
    n.cents = 25.0f;
    n.velocity = 100;
    n.channel = 0;
    c.notes.push_back(n);
    return c;
}

GeneratedSong twoSectionSong() {
    GeneratedSong song;
    song.title = "Test";
    song.tempo = 120;
    song.beatsPerBar = 4;

    GeneratedSection intro;
    intro.id = "intro";
    intro.name = "Intro";
    intro.bars = 1;
    intro.clips.push_back(clipFor("drums", 4.0));
    intro.clips.push_back(clipFor("bass", 4.0));

    GeneratedSection chorus;
    chorus.id = "chorus";
    chorus.name = "Chorus";
    chorus.bars = 2;
    chorus.clips.push_back(clipFor("drums", 8.0));
    chorus.clips.push_back(clipFor("bass", 8.0));

    song.sections.push_back(intro);
    song.sections.push_back(chorus);
    return song;
}

} // namespace

TEST(SongCommitter, CollectRolesInFirstAppearanceOrder) {
    const auto roles = collectRoles(twoSectionSong());
    ASSERT_EQ(roles.size(), 2u);
    EXPECT_EQ(roles[0], "drums");
    EXPECT_EQ(roles[1], "bass");
}

TEST(SongCommitter, AppendsScenesClipsAndFollowActions) {
    Project project;
    project.init(6, 2);
    project.scene(0).name = "Alpha";
    project.scene(1).name = "Beta";

    CommitResult result;
    std::string error;
    ASSERT_TRUE(commitGeneratedSong(project, twoSectionSong(), { 0, 1 },
                                    &result, &error)) << error;
    EXPECT_EQ(project.numScenes(), 4);
    EXPECT_EQ(result.firstScene, 2);
    EXPECT_EQ(result.sceneCount, 2);
    EXPECT_EQ(project.scene(2).name, "03 Intro");
    EXPECT_EQ(project.scene(3).name, "04 Chorus");

    auto* drums = project.getMidiClip(0, 2);
    auto* bass = project.getMidiClip(1, 2);
    ASSERT_NE(drums, nullptr);
    ASSERT_NE(bass, nullptr);
    EXPECT_EQ(drums->noteCount(), 1);
    EXPECT_FLOAT_EQ(drums->note(0).cents, 25.0f);
    EXPECT_EQ(drums->note(0).velocity, midi::Convert::vel7to16(100));

    auto* introSlot = project.getSlot(0, 2);
    ASSERT_NE(introSlot, nullptr);
    EXPECT_TRUE(introSlot->followAction.enabled);
    EXPECT_EQ(introSlot->followAction.barCount, 1);
    EXPECT_EQ(introSlot->followAction.actionA, FollowActionType::Next);

    auto* chorusSlot = project.getSlot(0, 3);
    ASSERT_NE(chorusSlot, nullptr);
    EXPECT_EQ(chorusSlot->followAction.barCount, 2);
    EXPECT_EQ(chorusSlot->followAction.actionA, FollowActionType::Stop);

    // No clip on an unmapped track.
    EXPECT_EQ(project.getMidiClip(2, 2), nullptr);
}

TEST(SongCommitter, RejectsBadMappingsWithoutAppending) {
    Project project;
    project.init(4, 2);

    CommitResult result;
    std::string error;
    EXPECT_FALSE(commitGeneratedSong(project, twoSectionSong(), { 0 },
                                     &result, &error));
    EXPECT_EQ(project.numScenes(), 2);

    EXPECT_FALSE(commitGeneratedSong(project, twoSectionSong(), { 0, 0 },
                                     &result, &error));
    EXPECT_EQ(project.numScenes(), 2);

    EXPECT_FALSE(commitGeneratedSong(project, twoSectionSong(), { 0, 9 },
                                     &result, &error));
    EXPECT_EQ(project.numScenes(), 2);
}

TEST(SongCommitter, UndoHelperPreservesRemainingSceneNames) {
    Project project;
    project.init(4, 3);
    project.scene(0).name = "Alpha";
    project.scene(1).name = "Beta";
    project.scene(2).name = "Gamma";

    CommitResult result;
    std::string error;
    ASSERT_TRUE(commitGeneratedSong(project, twoSectionSong(), { 0, 1 },
                                    &result, &error)) << error;
    ASSERT_EQ(project.numScenes(), 5);

    EXPECT_EQ(removeLastScenesPreservingNames(project, 2), 2);
    ASSERT_EQ(project.numScenes(), 3);
    EXPECT_EQ(project.scene(0).name, "Alpha");
    EXPECT_EQ(project.scene(1).name, "Beta");
    EXPECT_EQ(project.scene(2).name, "Gamma");
}
