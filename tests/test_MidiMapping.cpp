#include <gtest/gtest.h>
#include "midi/MidiMapping.h"
#include "automation/AutomationTypes.h"
#include <nlohmann/json.hpp>

using namespace yawn;
using namespace yawn::midi;
using namespace yawn::automation;

// ========================= MidiMapping struct =========================

TEST(MidiMapping, CcToParamFullRange) {
    MidiMapping m;
    m.paramMin = 0.0f;
    m.paramMax = 1.0f;
    EXPECT_FLOAT_EQ(m.ccToParam(0), 0.0f);
    EXPECT_FLOAT_EQ(m.ccToParam(127), 1.0f);
    EXPECT_NEAR(m.ccToParam(64), 0.5039f, 0.01f);
}

TEST(MidiMapping, CcToParamCustomRange) {
    MidiMapping m;
    m.paramMin = 20.0f;
    m.paramMax = 300.0f;
    EXPECT_FLOAT_EQ(m.ccToParam(0), 20.0f);
    EXPECT_FLOAT_EQ(m.ccToParam(127), 300.0f);
    float mid = m.ccToParam(64);
    EXPECT_GT(mid, 150.0f);
    EXPECT_LT(mid, 170.0f);
}

TEST(MidiMapping, CcToParamInvertedRange) {
    MidiMapping m;
    m.paramMin = 1.0f;
    m.paramMax = 0.0f;
    EXPECT_FLOAT_EQ(m.ccToParam(0), 1.0f);
    EXPECT_FLOAT_EQ(m.ccToParam(127), 0.0f);
}

TEST(MidiMapping, MatchesCCAnyChannel) {
    MidiMapping m;
    m.midiChannel = -1;
    m.ccNumber = 7;
    m.enabled = true;
    EXPECT_TRUE(m.matchesCC(0, 7));
    EXPECT_TRUE(m.matchesCC(15, 7));
    EXPECT_FALSE(m.matchesCC(0, 8));
}

TEST(MidiMapping, MatchesCCSpecificChannel) {
    MidiMapping m;
    m.midiChannel = 3;
    m.ccNumber = 10;
    m.enabled = true;
    EXPECT_TRUE(m.matchesCC(3, 10));
    EXPECT_FALSE(m.matchesCC(0, 10));
    EXPECT_FALSE(m.matchesCC(3, 11));
}

TEST(MidiMapping, MatchesCCDisabled) {
    MidiMapping m;
    m.midiChannel = -1;
    m.ccNumber = 7;
    m.enabled = false;
    EXPECT_FALSE(m.matchesCC(0, 7));
}

// ========================= MidiLearnManager =========================

TEST(MidiLearnManager, InitialState) {
    MidiLearnManager mgr;
    EXPECT_FALSE(mgr.isLearning());
    EXPECT_TRUE(mgr.empty());
    EXPECT_EQ(mgr.size(), 0u);
}

TEST(MidiLearnManager, StartAndCancelLearn) {
    MidiLearnManager mgr;
    auto target = AutomationTarget::mixer(0, MixerParam::Volume);
    mgr.startLearn(target, 0.0f, 1.0f);
    EXPECT_TRUE(mgr.isLearning());
    EXPECT_EQ(mgr.learnTarget(), target);

    mgr.cancelLearn();
    EXPECT_FALSE(mgr.isLearning());
    EXPECT_TRUE(mgr.empty());
}

TEST(MidiLearnManager, LearnCCCreatesMapping) {
    MidiLearnManager mgr;
    auto target = AutomationTarget::mixer(0, MixerParam::Volume);
    mgr.startLearn(target, 0.0f, 2.0f);

    bool consumed = mgr.handleLearnCC(5, 74);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(mgr.isLearning());
    EXPECT_EQ(mgr.size(), 1u);

    auto* m = mgr.findByTarget(target);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->midiChannel, 5);
    EXPECT_EQ(m->ccNumber, 74);
    EXPECT_FLOAT_EQ(m->paramMin, 0.0f);
    EXPECT_FLOAT_EQ(m->paramMax, 2.0f);
    EXPECT_TRUE(m->enabled);
}

TEST(MidiLearnManager, HandleLearnCCWhenNotLearning) {
    MidiLearnManager mgr;
    EXPECT_FALSE(mgr.handleLearnCC(0, 7));
    EXPECT_TRUE(mgr.empty());
}

TEST(MidiLearnManager, LearnReplacesExistingMapping) {
    MidiLearnManager mgr;
    auto target = AutomationTarget::mixer(0, MixerParam::Pan);

    // Learn CC 10
    mgr.startLearn(target, -1.0f, 1.0f);
    mgr.handleLearnCC(0, 10);

    // Learn CC 20 for same target — replaces previous
    mgr.startLearn(target, -1.0f, 1.0f);
    mgr.handleLearnCC(0, 20);

    EXPECT_EQ(mgr.size(), 1u);
    auto* m = mgr.findByTarget(target);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->ccNumber, 20);
}

TEST(MidiLearnManager, FindByCC) {
    MidiLearnManager mgr;

    MidiMapping m1;
    m1.midiChannel = -1;
    m1.ccNumber = 7;
    m1.target = AutomationTarget::mixer(0, MixerParam::Volume);
    mgr.addMapping(m1);

    MidiMapping m2;
    m2.midiChannel = -1;
    m2.ccNumber = 7;
    m2.target = AutomationTarget::mixer(1, MixerParam::Volume);
    mgr.addMapping(m2);

    MidiMapping m3;
    m3.midiChannel = -1;
    m3.ccNumber = 10;
    m3.target = AutomationTarget::mixer(0, MixerParam::Pan);
    mgr.addMapping(m3);

    auto results = mgr.findByCC(0, 7);
    EXPECT_EQ(results.size(), 2u);

    auto results2 = mgr.findByCC(0, 10);
    EXPECT_EQ(results2.size(), 1u);

    auto results3 = mgr.findByCC(0, 99);
    EXPECT_EQ(results3.size(), 0u);
}

TEST(MidiLearnManager, RemoveByTarget) {
    MidiLearnManager mgr;

    MidiMapping m1;
    m1.ccNumber = 7;
    m1.target = AutomationTarget::mixer(0, MixerParam::Volume);
    mgr.addMapping(m1);

    MidiMapping m2;
    m2.ccNumber = 10;
    m2.target = AutomationTarget::mixer(0, MixerParam::Pan);
    mgr.addMapping(m2);

    EXPECT_EQ(mgr.size(), 2u);

    mgr.removeByTarget(AutomationTarget::mixer(0, MixerParam::Volume));
    EXPECT_EQ(mgr.size(), 1u);
    EXPECT_EQ(mgr.findByTarget(AutomationTarget::mixer(0, MixerParam::Volume)), nullptr);
    EXPECT_NE(mgr.findByTarget(AutomationTarget::mixer(0, MixerParam::Pan)), nullptr);
}

TEST(MidiLearnManager, ClearAll) {
    MidiLearnManager mgr;
    mgr.startLearn(AutomationTarget::mixer(0, MixerParam::Volume));
    mgr.handleLearnCC(0, 7);
    mgr.startLearn(AutomationTarget::mixer(0, MixerParam::Pan));
    mgr.handleLearnCC(0, 10);

    EXPECT_EQ(mgr.size(), 2u);
    mgr.clearAll();
    EXPECT_EQ(mgr.size(), 0u);
    EXPECT_FALSE(mgr.isLearning());
}

// ========================= Serialization =========================

TEST(MidiLearnManager, JsonRoundTrip) {
    MidiLearnManager original;

    MidiMapping m1;
    m1.midiChannel = 3;
    m1.ccNumber = 7;
    m1.target = AutomationTarget::mixer(0, MixerParam::Volume);
    m1.paramMin = 0.0f;
    m1.paramMax = 2.0f;
    original.addMapping(m1);

    MidiMapping m2;
    m2.midiChannel = -1;
    m2.ccNumber = 10;
    m2.target = AutomationTarget::audioEffect(1, 2, 3);
    m2.paramMin = -1.0f;
    m2.paramMax = 1.0f;
    m2.enabled = false;
    original.addMapping(m2);

    MidiMapping m3;
    m3.midiChannel = 0;
    m3.ccNumber = 1;
    m3.target = AutomationTarget::transport(TransportParam::BPM);
    m3.paramMin = 0.0f;
    m3.paramMax = 1.0f;
    original.addMapping(m3);

    // Serialize
    auto j = original.toJson();
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 3u);

    // Deserialize
    MidiLearnManager loaded;
    loaded.fromJson(j);
    EXPECT_EQ(loaded.size(), 3u);

    // Check mapping 1
    auto* r1 = loaded.findByTarget(AutomationTarget::mixer(0, MixerParam::Volume));
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->midiChannel, 3);
    EXPECT_EQ(r1->ccNumber, 7);
    EXPECT_FLOAT_EQ(r1->paramMin, 0.0f);
    EXPECT_FLOAT_EQ(r1->paramMax, 2.0f);
    EXPECT_TRUE(r1->enabled);

    // Check mapping 2
    auto* r2 = loaded.findByTarget(AutomationTarget::audioEffect(1, 2, 3));
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->midiChannel, -1);
    EXPECT_EQ(r2->ccNumber, 10);
    EXPECT_FLOAT_EQ(r2->paramMin, -1.0f);
    EXPECT_FLOAT_EQ(r2->paramMax, 1.0f);
    EXPECT_FALSE(r2->enabled);

    // Check transport mapping
    auto* r3 = loaded.findByTarget(AutomationTarget::transport(TransportParam::BPM));
    ASSERT_NE(r3, nullptr);
    EXPECT_EQ(r3->ccNumber, 1);
    EXPECT_EQ(r3->target.type, TargetType::Transport);
}

TEST(MidiLearnManager, FromJsonInvalid) {
    MidiLearnManager mgr;
    mgr.fromJson(nlohmann::json("not an array"));
    EXPECT_TRUE(mgr.empty());
}

TEST(MidiLearnManager, FromJsonEmpty) {
    MidiLearnManager mgr;
    mgr.fromJson(nlohmann::json::array());
    EXPECT_TRUE(mgr.empty());
}

// ========================= AutomationTarget Transport =========================

TEST(AutomationTarget, TransportFactory) {
    auto bpm = AutomationTarget::transport(TransportParam::BPM);
    EXPECT_EQ(bpm.type, TargetType::Transport);
    EXPECT_EQ(bpm.paramIndex, static_cast<int>(TransportParam::BPM));

    auto play = AutomationTarget::transport(TransportParam::Play);
    EXPECT_EQ(play.type, TargetType::Transport);
    EXPECT_EQ(play.paramIndex, static_cast<int>(TransportParam::Play));

    auto stop = AutomationTarget::transport(TransportParam::Stop);
    EXPECT_NE(play, stop);
}

// ========================= Note-based mapping =========================

TEST(MidiMapping, NoteToParam) {
    MidiMapping m;
    m.source = MappingSource::Note;
    m.paramMin = 0.0f;
    m.paramMax = 1.0f;
    EXPECT_FLOAT_EQ(m.noteToParam(true), 1.0f);
    EXPECT_FLOAT_EQ(m.noteToParam(false), 0.0f);
}

TEST(MidiMapping, MatchesNoteAnyChannel) {
    MidiMapping m;
    m.source = MappingSource::Note;
    m.midiChannel = -1;
    m.noteNumber = 60;
    m.enabled = true;
    EXPECT_TRUE(m.matchesNote(0, 60));
    EXPECT_TRUE(m.matchesNote(15, 60));
    EXPECT_FALSE(m.matchesNote(0, 61));
}

TEST(MidiMapping, MatchesNoteSpecificChannel) {
    MidiMapping m;
    m.source = MappingSource::Note;
    m.midiChannel = 9;
    m.noteNumber = 36;
    m.enabled = true;
    EXPECT_TRUE(m.matchesNote(9, 36));
    EXPECT_FALSE(m.matchesNote(0, 36));
}

TEST(MidiMapping, MatchesNoteCCSourceDoesNotMatchNote) {
    MidiMapping m;
    m.source = MappingSource::CC;
    m.midiChannel = -1;
    m.ccNumber = 60;
    m.enabled = true;
    EXPECT_FALSE(m.matchesNote(0, 60));
}

TEST(MidiMapping, MatchesCCNoteSourceDoesNotMatchCC) {
    MidiMapping m;
    m.source = MappingSource::Note;
    m.midiChannel = -1;
    m.noteNumber = 7;
    m.enabled = true;
    EXPECT_FALSE(m.matchesCC(0, 7));
}

TEST(MidiMapping, LabelCC) {
    MidiMapping m;
    m.source = MappingSource::CC;
    m.ccNumber = 74;
    EXPECT_EQ(m.label(), "CC74");
}

TEST(MidiMapping, LabelNote) {
    MidiMapping m;
    m.source = MappingSource::Note;
    m.noteNumber = 60;
    EXPECT_EQ(m.label(), "N60");
}

TEST(MidiLearnManager, LearnNoteCreatesMapping) {
    MidiLearnManager mgr;
    auto target = AutomationTarget::transport(TransportParam::Play);
    mgr.startLearn(target, 0.0f, 1.0f);

    bool consumed = mgr.handleLearnNote(0, 60);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(mgr.isLearning());
    EXPECT_EQ(mgr.size(), 1u);

    auto* m = mgr.findByTarget(target);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->source, MappingSource::Note);
    EXPECT_EQ(m->midiChannel, 0);
    EXPECT_EQ(m->noteNumber, 60);
}

TEST(MidiLearnManager, FindByNote) {
    MidiLearnManager mgr;

    MidiMapping m1;
    m1.source = MappingSource::Note;
    m1.midiChannel = -1;
    m1.noteNumber = 60;
    m1.target = AutomationTarget::transport(TransportParam::Play);
    mgr.addMapping(m1);

    MidiMapping m2;
    m2.source = MappingSource::CC;
    m2.midiChannel = -1;
    m2.ccNumber = 60;
    m2.target = AutomationTarget::mixer(0, MixerParam::Volume);
    mgr.addMapping(m2);

    auto noteHits = mgr.findByNote(0, 60);
    EXPECT_EQ(noteHits.size(), 1u);
    EXPECT_EQ(noteHits[0]->target.type, TargetType::Transport);

    auto ccHits = mgr.findByCC(0, 60);
    EXPECT_EQ(ccHits.size(), 1u);
    EXPECT_EQ(ccHits[0]->target.type, TargetType::Mixer);
}

TEST(MidiLearnManager, JsonRoundTripWithNotes) {
    MidiLearnManager original;

    MidiMapping m1;
    m1.source = MappingSource::Note;
    m1.midiChannel = 9;
    m1.noteNumber = 36;
    m1.target = AutomationTarget::transport(TransportParam::Play);
    m1.paramMin = 0.0f;
    m1.paramMax = 1.0f;
    original.addMapping(m1);

    MidiMapping m2;
    m2.source = MappingSource::CC;
    m2.midiChannel = 0;
    m2.ccNumber = 7;
    m2.target = AutomationTarget::mixer(0, MixerParam::Volume);
    original.addMapping(m2);

    auto j = original.toJson();
    MidiLearnManager loaded;
    loaded.fromJson(j);
    EXPECT_EQ(loaded.size(), 2u);

    auto* r1 = loaded.findByTarget(AutomationTarget::transport(TransportParam::Play));
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->source, MappingSource::Note);
    EXPECT_EQ(r1->midiChannel, 9);
    EXPECT_EQ(r1->noteNumber, 36);

    auto* r2 = loaded.findByTarget(AutomationTarget::mixer(0, MixerParam::Volume));
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->source, MappingSource::CC);
    EXPECT_EQ(r2->ccNumber, 7);
}
