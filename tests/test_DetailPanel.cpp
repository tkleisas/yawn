#include <gtest/gtest.h>
#include "ui/DetailPanel.h"
#include "effects/EffectChain.h"
#include "midi/MidiEffectChain.h"

using namespace yawn::ui;
using namespace yawn::instruments;

// Minimal test instrument for DetailPanel tests
class TestInstrument : public Instrument {
public:
    void init(double sr, int bs) override { m_sampleRate = sr; m_maxBlockSize = bs; }
    void reset() override {}
    void process(float*, int, int, const yawn::midi::MidiBuffer&) override {}

    const char* name() const override { return "TestSynth"; }
    const char* id()   const override { return "test.synth"; }

    int parameterCount() const override { return 3; }

    const InstrumentParameterInfo& parameterInfo(int index) const override {
        static const InstrumentParameterInfo params[] = {
            {"Volume",    0.0f, 1.0f, 0.7f, "dB",  false},
            {"Cutoff",  20.0f, 20000.0f, 1000.0f, "Hz", false},
            {"Bypass",   0.0f, 1.0f, 0.0f, "",    true},
        };
        return params[index];
    }

    float getParameter(int index) const override { return m_params[index]; }
    void setParameter(int index, float value) override { m_params[index] = value; }

private:
    float m_params[3] = {0.7f, 1000.0f, 0.0f};
};

TEST(DetailPanel, InitiallyClosed) {
    DetailPanel panel;
    EXPECT_FALSE(panel.isOpen());
    EXPECT_FLOAT_EQ(panel.height(), DetailPanel::kCollapsedHeight);
}

TEST(DetailPanel, SetDeviceChainPopulates) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);
    EXPECT_TRUE(panel.isOpen());
    EXPECT_FLOAT_EQ(panel.height(), DetailPanel::kPanelHeight);
}

TEST(DetailPanel, TogglePanel) {
    DetailPanel panel;
    panel.setOpen(true);
    EXPECT_TRUE(panel.isOpen());
    panel.toggle();
    EXPECT_FALSE(panel.isOpen());
    panel.toggle();
    EXPECT_TRUE(panel.isOpen());
}

TEST(DetailPanel, HeaderClickToggles) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    // Click on header (within kHeaderHeight = 28)
    bool consumed = panel.handleClick(100, 10, 0, 0, 800);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(panel.isOpen()); // toggled closed
}

TEST(DetailPanel, KnobDragChangesParam) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    // Knob grid in device panel body:
    // bodyY = kHeaderHeight(28), device header = kDeviceHeaderH(24)
    // knob area starts at dx+8, bodyY + kDeviceHeaderH + 4 = 56
    // First knob center: x=8+24=32, y=56+24=80
    float knobCX = 8 + DetailPanel::kKnobSize * 0.5f;
    float knobCY = DetailPanel::kHeaderHeight + DetailPanel::kDeviceHeaderH + 4
                   + DetailPanel::kKnobSize * 0.5f;

    panel.handleClick(knobCX, knobCY, 0, 0, 800);
    EXPECT_TRUE(panel.isDragging());

    float startVal = inst.getParameter(0);
    panel.handleDrag(knobCX, knobCY - 30);
    float newVal = inst.getParameter(0);
    EXPECT_GT(newVal, startVal);

    panel.handleRelease();
    EXPECT_FALSE(panel.isDragging());
}

TEST(DetailPanel, BooleanParamToggles) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    // 3 params, kMaxKnobRows=2 → cols=ceil(3/2)=2
    // Param 2: row=2/2=1, col=2%2=0
    // cellH = kKnobSize(48) + 22 = 70
    // ky = 56 + 1*70 = 126
    EXPECT_NEAR(inst.getParameter(2), 0.0f, 0.01f);

    float knobX = 8 + DetailPanel::kKnobSize * 0.5f;
    float cellH = DetailPanel::kKnobSize + 22.0f;
    float knobY = DetailPanel::kHeaderHeight + DetailPanel::kDeviceHeaderH + 4
                  + cellH + DetailPanel::kKnobSize * 0.5f;

    panel.handleClick(knobX, knobY, 0, 0, 800);
    EXPECT_FALSE(panel.isDragging());
    EXPECT_NEAR(inst.getParameter(2), 1.0f, 0.01f);
}

TEST(DetailPanel, RightClickResetsDefault) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    inst.setParameter(0, 0.2f);
    EXPECT_NEAR(inst.getParameter(0), 0.2f, 0.01f);

    float knobCX = 8 + DetailPanel::kKnobSize * 0.5f;
    float knobCY = DetailPanel::kHeaderHeight + DetailPanel::kDeviceHeaderH + 4
                   + DetailPanel::kKnobSize * 0.5f;

    panel.handleRightClick(knobCX, knobCY, 0, 0, 800);
    EXPECT_NEAR(inst.getParameter(0), 0.7f, 0.01f);
}

TEST(DetailPanel, ClearResetsState) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    panel.clear();
    // After clear, body click should not hit any device knobs
    float knobCX = 8 + DetailPanel::kKnobSize * 0.5f;
    float knobCY = DetailPanel::kHeaderHeight + DetailPanel::kDeviceHeaderH + 4
                   + DetailPanel::kKnobSize * 0.5f;
    // handleClick returns true (consumed) but no drag started since no devices
    panel.handleClick(knobCX, knobCY, 0, 0, 800);
    EXPECT_FALSE(panel.isDragging());
}

TEST(DetailPanel, FocusManagement) {
    DetailPanel panel;
    EXPECT_FALSE(panel.isFocused());

    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    // Clicking on panel sets focus
    panel.handleClick(100, 40, 0, 0, 800);
    EXPECT_TRUE(panel.isFocused());

    // Can be cleared externally
    panel.setFocused(false);
    EXPECT_FALSE(panel.isFocused());
}

TEST(DetailPanel, BypassToggle) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.setOpen(true);
    panel.setDeviceChain(nullptr, &inst, nullptr);

    EXPECT_FALSE(inst.bypassed());

    // Bypass button is at dx+18, hy = bodyY+5 = 33
    // bodyY = kHeaderHeight = 28, so hy = 33
    float bpX = 18 + DetailPanel::kBtnSize * 0.5f;
    float bpY = DetailPanel::kHeaderHeight + 5 + DetailPanel::kBtnSize * 0.5f;

    panel.handleClick(bpX, bpY, 0, 0, 800);
    EXPECT_TRUE(inst.bypassed());
}
