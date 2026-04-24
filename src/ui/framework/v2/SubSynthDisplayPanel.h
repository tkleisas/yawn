#pragma once
// fw2::SubSynthDisplayPanel — composite visualization for the
// Subtractive Synth instrument. Migrated from v1
// fw::SubSynthDisplayPanel. 2-row layout:
//   Top:    OSC 1 | OSC 2 | Filter response
//   Bottom: Amp ADSR   | Filter ADSR

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/OscDisplayWidget.h"
#include "ui/framework/v2/FilterDisplayWidget.h"
#include "ui/framework/v2/ADSRDisplayWidget.h"

namespace yawn {
namespace ui {
namespace fw2 {

class SubSynthDisplayPanel : public Widget {
public:
    SubSynthDisplayPanel() {
        setName("SubSynthDisplay");
        m_osc1.setLabel("OSC 1");
        m_osc1.setColor(Color{0, 200, 255, 255});
        m_osc2.setLabel("OSC 2");
        m_osc2.setColor(Color{0, 180, 230, 255});
        m_filter.setFilterType(0);
        m_ampAdsr.setLabel("AMP");
        m_ampAdsr.setColor(Color{80, 220, 100, 255});
        m_filtAdsr.setLabel("FILT");
        m_filtAdsr.setColor(Color{220, 200, 60, 255});
    }

    void updateFromParams(
        float osc1Wave, float osc1Level,
        float osc2Wave, float osc2Level,
        float filterCutoff, float filterReso, float filterType,
        float ampA, float ampD, float ampS, float ampR,
        float filtA, float filtD, float filtS, float filtR)
    {
        m_osc1.setWaveform(static_cast<int>(osc1Wave));
        m_osc1.setLevel(osc1Level);
        m_osc2.setWaveform(static_cast<int>(osc2Wave));
        m_osc2.setLevel(osc2Level);
        m_filter.setCutoff(filterCutoff);
        m_filter.setResonance(filterReso);
        m_filter.setFilterType(static_cast<int>(filterType));
        m_ampAdsr.setADSR(ampA, ampD, ampS, ampR);
        m_filtAdsr.setADSR(filtA, filtD, filtS, filtR);
    }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 96.0f});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        const float gap = 3.0f;
        const float halfH = (bounds.h - gap) * 0.5f;

        // Top row: Osc1 | Osc2 | Filter (equal width)
        const float topUnit = (bounds.w - 2 * gap) / 3.0f;
        float x = bounds.x;
        layoutChild(m_osc1, {x, bounds.y, topUnit, halfH}, ctx);
        x += topUnit + gap;
        layoutChild(m_osc2, {x, bounds.y, topUnit, halfH}, ctx);
        x += topUnit + gap;
        layoutChild(m_filter, {x, bounds.y, topUnit, halfH}, ctx);

        // Bottom row: Amp ADSR | Filt ADSR (equal width)
        const float botUnit = (bounds.w - gap) * 0.5f;
        const float botY = bounds.y + halfH + gap;
        layoutChild(m_ampAdsr,  {bounds.x,               botY, botUnit, halfH}, ctx);
        layoutChild(m_filtAdsr, {bounds.x + botUnit + gap, botY, botUnit, halfH}, ctx);
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        m_osc1.render(ctx);
        m_osc2.render(ctx);
        m_filter.render(ctx);
        m_ampAdsr.render(ctx);
        m_filtAdsr.render(ctx);
    }
#endif

private:
    static void layoutChild(Widget& w, Rect r, UIContext& ctx) {
        w.measure(Constraints::tight(r.w, r.h), ctx);
        w.layout(r, ctx);
    }

    OscDisplayWidget    m_osc1, m_osc2;
    FilterDisplayWidget m_filter;
    ADSRDisplayWidget   m_ampAdsr, m_filtAdsr;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
