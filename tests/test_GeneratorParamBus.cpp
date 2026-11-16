#include <gtest/gtest.h>

#include "generate/GeneratorParamBus.h"
#include "midi/MidiMapping.h"

using namespace yawn;

TEST(GeneratorParamBus, WriteReadAndVersion) {
    auto& bus = generate::GeneratorParamBus::instance();
    const uint32_t before = bus.version();
    bus.write(generate::kGenDensity, 0.42f);
    bus.write(generate::kGenStyleBase + 3, 0.75f);
    EXPECT_FLOAT_EQ(bus.read(generate::kGenDensity), 0.42f);
    EXPECT_FLOAT_EQ(bus.read(generate::kGenStyleBase + 3), 0.75f);
    EXPECT_GT(bus.version(), before);

    // Out-of-range slots are ignored.
    bus.write(-1, 1.0f);
    bus.write(generate::kGenSlotCount, 1.0f);
    EXPECT_FLOAT_EQ(bus.read(-1), 0.0f);
}

TEST(GeneratorParamBus, MidiMappingRoundTripsGeneratorTarget) {
    midi::MidiLearnManager mgr;
    midi::MidiMapping m;
    m.source = midi::MappingSource::CC;
    m.ccNumber = 74;
    m.target.type = automation::TargetType::GeneratorParam;
    m.target.paramIndex = generate::kGenStyleBase + 2;
    m.paramMin = 0.0f;
    m.paramMax = 1.0f;
    mgr.addMapping(m);

    const auto json = mgr.toJson();
    midi::MidiLearnManager back;
    back.fromJson(json);
    ASSERT_EQ(back.size(), 1u);
    const auto& t = back.mappings().front().target;
    EXPECT_EQ(t.type, automation::TargetType::GeneratorParam);
    EXPECT_EQ(t.paramIndex, generate::kGenStyleBase + 2);
    EXPECT_EQ(back.mappings().front().ccNumber, 74);
}
