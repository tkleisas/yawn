#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

#include "app/Project.h"
#include "audio/AudioEngine.h"
#include "util/ProjectSerializer.h"
#include "util/Factory.h"

using namespace yawn;
using json = nlohmann::json;
namespace fs = std::filesystem;

// Helpers — each test gets its own temp dir to avoid CTest parallel races
static fs::path testProjectDir(const std::string& suffix = "default") {
    auto p = fs::temp_directory_path() / ("yawn_test_" + suffix + ".yawn");
    return p;
}

static void cleanupTestProject(const std::string& suffix = "default") {
    auto p = testProjectDir(suffix);
    if (fs::exists(p)) fs::remove_all(p);
}

// ---------------------------------------------------------------------------
// Factory tests
// ---------------------------------------------------------------------------

TEST(Factory, CreateAllInstruments) {
    EXPECT_NE(createInstrument("subsynth"), nullptr);
    EXPECT_NE(createInstrument("fmsynth"), nullptr);
    EXPECT_NE(createInstrument("sampler"), nullptr);
    EXPECT_NE(createInstrument("drumrack"), nullptr);
    EXPECT_NE(createInstrument("drumslop"), nullptr);
    EXPECT_NE(createInstrument("instrack"), nullptr);
    EXPECT_EQ(createInstrument("nonexistent"), nullptr);
}

TEST(Factory, CreateAllAudioEffects) {
    EXPECT_NE(createAudioEffect("reverb"), nullptr);
    EXPECT_NE(createAudioEffect("delay"), nullptr);
    EXPECT_NE(createAudioEffect("eq"), nullptr);
    EXPECT_NE(createAudioEffect("compressor"), nullptr);
    EXPECT_NE(createAudioEffect("filter"), nullptr);
    EXPECT_NE(createAudioEffect("chorus"), nullptr);
    EXPECT_NE(createAudioEffect("distortion"), nullptr);
    EXPECT_NE(createAudioEffect("oscilloscope"), nullptr);
    EXPECT_NE(createAudioEffect("spectrum"), nullptr);
    EXPECT_EQ(createAudioEffect("nonexistent"), nullptr);
}

TEST(Factory, CreateAllMidiEffects) {
    EXPECT_NE(createMidiEffect("arp"), nullptr);
    EXPECT_NE(createMidiEffect("chord"), nullptr);
    EXPECT_NE(createMidiEffect("scale"), nullptr);
    EXPECT_NE(createMidiEffect("notelength"), nullptr);
    EXPECT_NE(createMidiEffect("velocity"), nullptr);
    EXPECT_NE(createMidiEffect("random"), nullptr);
    EXPECT_NE(createMidiEffect("pitch"), nullptr);
    EXPECT_EQ(createMidiEffect("nonexistent"), nullptr);
}

// ---------------------------------------------------------------------------
// Generic parameter serialization
// ---------------------------------------------------------------------------

TEST(ParamSerializer, RoundTripInstrument) {
    auto synth = createInstrument("subsynth");
    synth->init(44100.0, 256);

    // Set non-default values
    for (int i = 0; i < synth->parameterCount(); ++i) {
        float mid = (synth->parameterInfo(i).minValue + synth->parameterInfo(i).maxValue) * 0.5f;
        synth->setParameter(i, mid);
    }

    json params = serializeParams(*synth);

    // Create fresh instance and restore
    auto synth2 = createInstrument("subsynth");
    synth2->init(44100.0, 256);
    deserializeParams(*synth2, params);

    for (int i = 0; i < synth->parameterCount(); ++i) {
        EXPECT_NEAR(synth->getParameter(i), synth2->getParameter(i), 0.001f)
            << "Parameter mismatch at index " << i;
    }
}

TEST(ParamSerializer, RoundTripAudioEffect) {
    auto fx = createAudioEffect("delay");
    fx->init(44100.0, 256);

    for (int i = 0; i < fx->parameterCount(); ++i) {
        float mid = (fx->parameterInfo(i).minValue + fx->parameterInfo(i).maxValue) * 0.5f;
        fx->setParameter(i, mid);
    }
    fx->setBypassed(true);
    fx->setMix(0.6f);

    json params = serializeParams(*fx);

    auto fx2 = createAudioEffect("delay");
    fx2->init(44100.0, 256);
    deserializeParams(*fx2, params);

    for (int i = 0; i < fx->parameterCount(); ++i) {
        EXPECT_NEAR(fx->getParameter(i), fx2->getParameter(i), 0.001f);
    }
}

TEST(ParamSerializer, RoundTripMidiEffect) {
    auto fx = createMidiEffect("arp");
    fx->init(44100.0);

    for (int i = 0; i < fx->parameterCount(); ++i) {
        float mid = (fx->parameterInfo(i).minValue + fx->parameterInfo(i).maxValue) * 0.5f;
        fx->setParameter(i, mid);
    }

    json params = serializeParams(*fx);

    auto fx2 = createMidiEffect("arp");
    fx2->init(44100.0);
    deserializeParams(*fx2, params);

    for (int i = 0; i < fx->parameterCount(); ++i) {
        EXPECT_NEAR(fx->getParameter(i), fx2->getParameter(i), 0.001f);
    }
}

// ---------------------------------------------------------------------------
// Effect chain serialization
// ---------------------------------------------------------------------------

TEST(EffectChainSerializer, RoundTrip) {
    effects::EffectChain chain;
    chain.init(44100.0, 256);

    auto rev = createAudioEffect("reverb");
    rev->init(44100.0, 256);
    rev->setParameter(0, 0.8f);  // RoomSize
    rev->setBypassed(true);
    rev->setMix(0.5f);
    chain.append(std::move(rev));

    auto del = createAudioEffect("delay");
    del->init(44100.0, 256);
    del->setParameter(0, 300.0f);  // TimeMs
    chain.append(std::move(del));

    json j = serializeEffectChain(chain);
    EXPECT_EQ(j.size(), 2u);
    EXPECT_EQ(j[0]["id"], "reverb");
    EXPECT_EQ(j[1]["id"], "delay");

    effects::EffectChain chain2;
    chain2.init(44100.0, 256);
    deserializeEffectChain(chain2, j, 44100.0, 256);

    EXPECT_EQ(chain2.count(), 2);
    EXPECT_TRUE(chain2.effectAt(0)->bypassed());
    EXPECT_NEAR(chain2.effectAt(0)->mix(), 0.5f, 0.001f);
    EXPECT_NEAR(chain2.effectAt(0)->getParameter(0), 0.8f, 0.001f);
    EXPECT_NEAR(chain2.effectAt(1)->getParameter(0), 300.0f, 0.5f);
}

// ---------------------------------------------------------------------------
// MIDI clip serialization
// ---------------------------------------------------------------------------

TEST(MidiClipSerializer, RoundTrip) {
    midi::MidiClip clip(8.0);
    clip.setName("Test MIDI");
    clip.setLoop(false);

    midi::MidiNote n;
    n.startBeat = 1.0;
    n.duration = 0.5;
    n.pitch = 64;
    n.velocity = 16000;
    n.channel = 2;
    n.pressure = 0.7f;
    n.slide = 0.3f;
    n.pitchBendOffset = -0.5f;
    clip.addNote(n);

    midi::MidiCCEvent cc;
    cc.beat = 2.0;
    cc.ccNumber = 74;
    cc.value = 65536;
    cc.channel = 2;
    clip.addCC(cc);

    json j = serializeMidiClip(clip);
    auto clip2 = deserializeMidiClip(j);

    EXPECT_EQ(clip2->name(), "Test MIDI");
    EXPECT_DOUBLE_EQ(clip2->lengthBeats(), 8.0);
    EXPECT_FALSE(clip2->loop());
    EXPECT_EQ(clip2->noteCount(), 1);
    EXPECT_EQ(clip2->note(0).pitch, 64);
    EXPECT_EQ(clip2->note(0).velocity, 16000);
    EXPECT_NEAR(clip2->note(0).pressure, 0.7f, 0.001f);
    EXPECT_NEAR(clip2->note(0).slide, 0.3f, 0.001f);
    EXPECT_NEAR(clip2->note(0).pitchBendOffset, -0.5f, 0.001f);
    EXPECT_EQ(clip2->ccCount(), 1);
    EXPECT_EQ(clip2->ccEvent(0).ccNumber, 74);
    EXPECT_EQ(clip2->ccEvent(0).value, 65536u);
}

// ---------------------------------------------------------------------------
// Full project round-trip
// ---------------------------------------------------------------------------

class ProjectSerializerTest : public ::testing::Test {
protected:
    fs::path projDir() const { return testProjectDir(testSuffix()); }

    void SetUp() override {
        cleanupTestProject(testSuffix());
    }
    void TearDown() override {
        cleanupTestProject(testSuffix());
    }
private:
    std::string testSuffix() const {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }
};

TEST_F(ProjectSerializerTest, SaveAndLoadEmptyProject) {
    Project project;
    project.init(4, 4);
    audio::AudioEngine engine;

    ASSERT_TRUE(ProjectSerializer::saveToFolder(projDir(), project, engine));
    EXPECT_TRUE(fs::exists(projDir() / "project.json"));
    EXPECT_TRUE(fs::is_directory(projDir() / "samples"));

    // Verify JSON is valid
    std::ifstream in(projDir() / "project.json");
    json root = json::parse(in);
    EXPECT_EQ(root["formatVersion"], 1);
    EXPECT_EQ(root["tracks"].size(), 4u);
    EXPECT_EQ(root["scenes"].size(), 4u);
}

TEST_F(ProjectSerializerTest, RoundTripTrackMetadata) {
    Project project;
    project.init(2, 2);
    project.track(0).name = "Bass";
    project.track(0).type = Track::Type::Midi;
    project.track(0).colorIndex = 5;
    project.track(0).volume = 0.75f;
    project.track(0).muted = true;
    project.track(0).midiInputPort = 2;
    project.track(0).midiInputChannel = 3;
    project.track(1).name = "Drums";
    project.track(1).armed = true;
    project.scene(0).name = "Intro";
    project.scene(1).name = "Verse";

    audio::AudioEngine engine;

    ASSERT_TRUE(ProjectSerializer::saveToFolder(projDir(), project, engine));

    Project project2;
    audio::AudioEngine engine2;
    ASSERT_TRUE(ProjectSerializer::loadFromFolder(projDir(), project2, engine2));

    EXPECT_EQ(project2.numTracks(), 2);
    EXPECT_EQ(project2.numScenes(), 2);
    EXPECT_EQ(project2.track(0).name, "Bass");
    EXPECT_EQ(project2.track(0).type, Track::Type::Midi);
    EXPECT_EQ(project2.track(0).colorIndex, 5);
    EXPECT_NEAR(project2.track(0).volume, 0.75f, 0.001f);
    EXPECT_TRUE(project2.track(0).muted);
    EXPECT_EQ(project2.track(0).midiInputPort, 2);
    EXPECT_EQ(project2.track(0).midiInputChannel, 3);
    EXPECT_EQ(project2.track(1).name, "Drums");
    EXPECT_TRUE(project2.track(1).armed);
    EXPECT_EQ(project2.scene(0).name, "Intro");
    EXPECT_EQ(project2.scene(1).name, "Verse");
}

TEST_F(ProjectSerializerTest, RoundTripInstrumentAndEffects) {
    Project project;
    project.init(1, 1);
    audio::AudioEngine engine;

    // Set instrument with custom params
    auto synth = createInstrument("subsynth");
    synth->init(44100.0, 256);
    engine.setInstrument(0, std::move(synth));
    // Set params AFTER setInstrument (which calls init/applyDefaults)
    engine.instrument(0)->setParameter(0, 2.0f);  // Osc1Wave

    // Add MIDI effect
    auto arp = createMidiEffect("arp");
    arp->init(44100.0);
    arp->setParameter(0, 2.0f);  // Direction
    arp->setBypassed(true);
    engine.midiEffectChain(0).addEffect(std::move(arp));

    // Add audio effect to track
    auto rev = createAudioEffect("reverb");
    rev->init(44100.0, 256);
    rev->setParameter(0, 0.9f);  // RoomSize
    rev->setMix(0.4f);
    engine.mixer().trackEffects(0).append(std::move(rev));

    ASSERT_TRUE(ProjectSerializer::saveToFolder(projDir(), project, engine));

    Project project2;
    audio::AudioEngine engine2;
    ASSERT_TRUE(ProjectSerializer::loadFromFolder(projDir(), project2, engine2));

    // Verify instrument
    auto* inst = engine2.instrument(0);
    ASSERT_NE(inst, nullptr);
    EXPECT_STREQ(inst->id(), "subsynth");
    EXPECT_NEAR(inst->getParameter(0), 2.0f, 0.001f);

    // Verify MIDI effect
    EXPECT_EQ(engine2.midiEffectChain(0).count(), 1);
    auto* mfx = engine2.midiEffectChain(0).effect(0);
    ASSERT_NE(mfx, nullptr);
    EXPECT_STREQ(mfx->id(), "arp");
    EXPECT_TRUE(mfx->bypassed());
    EXPECT_NEAR(mfx->getParameter(0), 2.0f, 0.001f);

    // Verify audio effect
    EXPECT_EQ(engine2.mixer().trackEffects(0).count(), 1);
    auto* fx = engine2.mixer().trackEffects(0).effectAt(0);
    ASSERT_NE(fx, nullptr);
    EXPECT_STREQ(fx->id(), "reverb");
    EXPECT_NEAR(fx->getParameter(0), 0.9f, 0.001f);
    EXPECT_NEAR(fx->mix(), 0.4f, 0.001f);
}

TEST_F(ProjectSerializerTest, RoundTripMixerState) {
    Project project;
    project.init(2, 1);
    audio::AudioEngine engine;

    engine.mixer().setTrackVolume(0, 0.6f);
    engine.mixer().setTrackPan(0, -0.3f);
    engine.mixer().setTrackMute(1, true);
    engine.mixer().setSendLevel(0, 0, 0.5f);
    engine.mixer().setSendEnabled(0, 0, true);
    engine.mixer().setSendMode(0, 0, audio::SendMode::PreFader);
    engine.mixer().setReturnVolume(0, 0.8f);
    engine.mixer().setMasterVolume(0.9f);

    ASSERT_TRUE(ProjectSerializer::saveToFolder(projDir(), project, engine));

    Project project2;
    audio::AudioEngine engine2;
    ASSERT_TRUE(ProjectSerializer::loadFromFolder(projDir(), project2, engine2));

    EXPECT_NEAR(engine2.mixer().trackChannel(0).volume, 0.6f, 0.001f);
    EXPECT_NEAR(engine2.mixer().trackChannel(0).pan, -0.3f, 0.001f);
    EXPECT_TRUE(engine2.mixer().trackChannel(1).muted);
    EXPECT_NEAR(engine2.mixer().trackChannel(0).sends[0].level, 0.5f, 0.001f);
    EXPECT_TRUE(engine2.mixer().trackChannel(0).sends[0].enabled);
    EXPECT_EQ(engine2.mixer().trackChannel(0).sends[0].mode, audio::SendMode::PreFader);
    EXPECT_NEAR(engine2.mixer().returnBus(0).volume, 0.8f, 0.001f);
    EXPECT_NEAR(engine2.mixer().master().volume, 0.9f, 0.001f);
}

TEST_F(ProjectSerializerTest, RoundTripBPM) {
    Project project;
    project.init(1, 1);
    audio::AudioEngine engine;
    engine.transport().setBPM(140.0);

    ASSERT_TRUE(ProjectSerializer::saveToFolder(projDir(), project, engine));

    Project project2;
    audio::AudioEngine engine2;
    ASSERT_TRUE(ProjectSerializer::loadFromFolder(projDir(), project2, engine2));

    EXPECT_NEAR(engine2.transport().bpm(), 140.0, 0.1);
}

TEST_F(ProjectSerializerTest, LoadNonexistentFails) {
    Project project;
    audio::AudioEngine engine;
    EXPECT_FALSE(ProjectSerializer::loadFromFolder("nonexistent_path.yawn", project, engine));
}

TEST_F(ProjectSerializerTest, SchemaVersioningIgnoresUnknownFields) {
    // Create a project.json with extra fields that shouldn't break loading
    auto dir = projDir();
    fs::create_directories(dir / "samples");

    json root;
    root["formatVersion"] = 1;
    root["futureField"] = "should be ignored";
    root["project"] = {{"bpm", 130.0}, {"unknownSetting", true}};
    root["tracks"] = json::array();
    root["tracks"].push_back({
        {"name", "Test"}, {"type", "Audio"}, {"colorIndex", 0},
        {"volume", 1.0f}, {"muted", false}, {"soloed", false},
        {"midiInputPort", -1}, {"midiInputChannel", -1}, {"armed", false},
        {"newTrackField", 42}
    });
    root["scenes"] = json::array();
    root["scenes"].push_back({{"name", "1"}});
    root["clips"] = json::object();
    root["mixer"] = {{"tracks", json::array()}, {"returns", json::array()},
                     {"master", {{"volume", 1.0f}}}};

    std::ofstream out(dir / "project.json");
    out << root.dump(2);
    out.close();

    Project project;
    audio::AudioEngine engine;
    ASSERT_TRUE(ProjectSerializer::loadFromFolder(dir, project, engine));
    EXPECT_EQ(project.track(0).name, "Test");
    EXPECT_NEAR(engine.transport().bpm(), 130.0, 0.1);
}

TEST_F(ProjectSerializerTest, RoundTripInstrumentRackChains) {
    Project project;
    project.init(1, 1);
    project.track(0).name = "RackTest";
    project.track(0).type = Track::Type::Midi;

    audio::AudioEngine engine;

    // Create an InstrumentRack with two chains: a SubSynth and a DrumRack
    auto rack = std::make_unique<instruments::InstrumentRack>();
    auto synth = createInstrument("subsynth");
    synth->init(44100.0, 256);
    rack->addChain(std::move(synth), 0, 59, 1, 127);
    rack->chain(0).volume = 0.7f;
    rack->chain(0).pan = -0.3f;

    auto dr = createInstrument("drumrack");
    dr->init(44100.0, 256);
    rack->addChain(std::move(dr), 60, 127, 1, 127);
    rack->chain(1).volume = 0.5f;
    rack->chain(1).enabled = false;

    rack->init(44100.0, 256);
    engine.setInstrument(0, std::move(rack));
    // Set a param after setInstrument to verify round-trip
    engine.instrument(0)->setParameter(0, 0.6f); // Rack Volume

    auto dir = projDir();
    ASSERT_TRUE(ProjectSerializer::saveToFolder(dir, project, engine));
    ASSERT_TRUE(fs::exists(dir / "project.json"));

    // Load into fresh state
    Project project2;
    audio::AudioEngine engine2;
    ASSERT_TRUE(ProjectSerializer::loadFromFolder(dir, project2, engine2));

    auto* loaded = engine2.instrument(0);
    ASSERT_NE(loaded, nullptr);
    EXPECT_STREQ(loaded->name(), "Instrument Rack");

    auto* loadedRack = dynamic_cast<instruments::InstrumentRack*>(loaded);
    ASSERT_NE(loadedRack, nullptr);
    EXPECT_EQ(loadedRack->chainCount(), 2);

    // Chain 0: SubSynth with key range 0-59
    EXPECT_STREQ(loadedRack->chain(0).instrument->name(), "Subtractive Synth");
    EXPECT_EQ(loadedRack->chain(0).keyLow, 0);
    EXPECT_EQ(loadedRack->chain(0).keyHigh, 59);
    EXPECT_NEAR(loadedRack->chain(0).volume, 0.7f, 0.01f);
    EXPECT_NEAR(loadedRack->chain(0).pan, -0.3f, 0.01f);

    // Chain 1: DrumRack with key range 60-127, disabled
    EXPECT_STREQ(loadedRack->chain(1).instrument->name(), "Drum Rack");
    EXPECT_EQ(loadedRack->chain(1).keyLow, 60);
    EXPECT_EQ(loadedRack->chain(1).keyHigh, 127);
    EXPECT_NEAR(loadedRack->chain(1).volume, 0.5f, 0.01f);
    EXPECT_FALSE(loadedRack->chain(1).enabled);

    // Rack volume persisted
    EXPECT_NEAR(loadedRack->getParameter(0), 0.6f, 0.01f);
}
