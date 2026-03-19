#include <gtest/gtest.h>
#include "ui/DetailPanel.h"

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

TEST(DetailPanel, ShowInstrumentOpens) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);

    panel.showInstrument(&inst);
    EXPECT_TRUE(panel.isOpen());
    EXPECT_STREQ(panel.title(), "TestSynth");
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
    panel.showInstrument(&inst);

    // Click on header (y = panelY + 10, which is within kHeaderHeight = 24)
    bool consumed = panel.handleClick(100, 10, 0, 0, 800);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(panel.isOpen()); // toggled closed
}

TEST(DetailPanel, KnobDragChangesParam) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.showInstrument(&inst);

    // Click on first knob (at approximately x=12, y=kHeaderHeight+8 = 32)
    float panelY = 0;
    float knobCenterX = 12 + DetailPanel::kKnobSize * 0.5f;
    float knobCenterY = panelY + DetailPanel::kHeaderHeight + 8 + DetailPanel::kKnobSize * 0.5f;

    panel.handleClick(knobCenterX, knobCenterY, 0, panelY, 800);
    EXPECT_TRUE(panel.isDragging());

    // Drag up (decrease my) should increase value
    float startVal = inst.getParameter(0);
    panel.handleDrag(knobCenterX, knobCenterY - 30);
    float newVal = inst.getParameter(0);
    EXPECT_GT(newVal, startVal);

    panel.handleRelease();
    EXPECT_FALSE(panel.isDragging());
}

TEST(DetailPanel, BooleanParamToggles) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.showInstrument(&inst);

    // Param 2 is boolean ("Bypass"), initially 0.0
    EXPECT_NEAR(inst.getParameter(2), 0.0f, 0.01f);

    // Click on third knob
    float panelY = 0;
    float knobX = 12 + 2 * (DetailPanel::kKnobSize + DetailPanel::kKnobSpacing)
                  + DetailPanel::kKnobSize * 0.5f;
    float knobY = panelY + DetailPanel::kHeaderHeight + 8 + DetailPanel::kKnobSize * 0.5f;

    panel.handleClick(knobX, knobY, 0, panelY, 800);
    // Boolean params toggle, don't drag
    EXPECT_FALSE(panel.isDragging());
    EXPECT_NEAR(inst.getParameter(2), 1.0f, 0.01f);
}

TEST(DetailPanel, RightClickResetsDefault) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.showInstrument(&inst);

    // Change param 0 away from default
    inst.setParameter(0, 0.2f);
    EXPECT_NEAR(inst.getParameter(0), 0.2f, 0.01f);

    // Right-click on first knob
    float panelY = 0;
    float knobCenterX = 12 + DetailPanel::kKnobSize * 0.5f;
    float knobCenterY = panelY + DetailPanel::kHeaderHeight + 8 + DetailPanel::kKnobSize * 0.5f;

    panel.handleRightClick(knobCenterX, knobCenterY, 0, panelY, 800);
    EXPECT_NEAR(inst.getParameter(0), 0.7f, 0.01f); // default is 0.7
}

TEST(DetailPanel, ClearRemovesTarget) {
    DetailPanel panel;
    TestInstrument inst;
    inst.init(44100, 256);
    panel.showInstrument(&inst);
    EXPECT_STREQ(panel.title(), "TestSynth");

    panel.clear();
    EXPECT_STREQ(panel.title(), "No Selection");
}
