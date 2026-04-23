#pragma once
// fw2::CustomDeviceBody + fw2::GroupedKnobBody — instrument-custom
// device body, migrated from v1 fw::CustomDeviceBody /
// fw::GroupedKnobBody (InstrumentDisplayWidget.h).
//
// GroupedKnobBody composes (optional) left-side display widget + N
// knob-grid sections. The knobs are native fw2::FwKnob; section layout
// is computed in onLayout. The display slot still points at v1
// `fw::Widget*` (the instrument-specific *DisplayPanel classes live in
// the still-v1 InstrumentDisplayWidget.h) — GroupedKnobBody owns it
// and drives its lifecycle by caching a v1 `fw::UIContext*` that the
// hosting panel sets before each lifecycle call. When the display
// panels migrate to fw2, drop the cache and the toFw1* helpers.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/V1EventBridge.h"
#include "ui/framework/Widget.h"          // v1 Widget
#include "ui/framework/UIContext.h"       // v1 UIContext
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

// ═══════════════════════════════════════════════════════════════════
// CustomDeviceBody — abstract base for custom instrument body layouts.
// Replaces the flat knob grid in fw2::DeviceWidget when set.
// ═══════════════════════════════════════════════════════════════════

class CustomDeviceBody : public Widget {
public:
    virtual ~CustomDeviceBody() = default;

    virtual void updateParamValue(int index, float value) = 0;
    virtual float preferredBodyWidth() const = 0;
    virtual void setOnParamChange(std::function<void(int, float)> cb) = 0;
    virtual void setOnParamTouch(std::function<void(int, float, bool)> cb) { (void)cb; }

    // Text-edit support for knob double-click numeric entry.
    virtual bool hasEditingKnob() const { return false; }
    virtual bool forwardKeyDown(int /*key*/) { return false; }
    virtual bool forwardTextInput(const char* /*text*/) { return false; }
    virtual void cancelEditingKnobs() {}
};

// Utility — format a parameter value for display (matches v1).
inline std::string groupedFormatParamValue(float value,
                                            const std::string& unit,
                                            bool isBoolean) {
    if (isBoolean) return value > 0.5f ? "On" : "Off";
    char buf[32];
    if (unit == "Hz") {
        if (value >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.1fk", value / 1000.0f);
        else                   std::snprintf(buf, sizeof(buf), "%.0f",  value);
    } else if (unit == "dB") std::snprintf(buf, sizeof(buf), "%.1f", value);
    else if (unit == "ms")   std::snprintf(buf, sizeof(buf), "%.0f", value);
    else if (unit == "%")    std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
    else if (unit == "s") {
        if (value < 0.1f) std::snprintf(buf, sizeof(buf), "%.0fms", value * 1000.0f);
        else              std::snprintf(buf, sizeof(buf), "%.2fs", value);
    } else if (unit == "x") std::snprintf(buf, sizeof(buf), "%.1fx", value);
    else                     std::snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

// Shorten parameter labels for compact knob display (matches v1).
inline std::string groupedShortenLabel(const std::string& name) {
    std::string s = name;
    if (s.size() > 4 && s[0] == 'O' && s[1] == 'p' && s[2] >= '1' && s[2] <= '9' && s[3] == ' ')
        s = s.substr(4);

    if (s == "Algorithm")   return "Algo";
    if (s == "Feedback")    return "FB";
    if (s == "Volume")      return "Vol";
    if (s == "Level")       return "Lvl";
    if (s == "Attack")      return "Atk";
    if (s == "Release")     return "Rel";
    if (s == "Cutoff")      return "Cut";
    if (s == "Resonance")   return "Res";
    if (s == "Depth")       return "Dpt";
    if (s == "Amount")      return "Amt";
    if (s == "Sustain")     return "Sus";
    if (s == "Decay")       return "Dcy";
    if (s == "Noise Level") return "Noise";
    if (s == "Root Note")   return "Root";
    if (s == "Filter Type") return "Type";
    if (s == "Filter Cutoff") return "Cut";
    if (s == "Filter Cut")    return "Cut";
    if (s == "Filter Res")    return "Res";
    if (s == "Filter Reso")   return "Reso";
    if (s == "Filter Env")    return "FiltEnv";
    if (s == "Osc1 Wave")     return "Wave";
    if (s == "Osc1 Level")    return "Level";
    if (s == "Osc2 Wave")     return "Wave";
    if (s == "Osc2 Level")    return "Level";
    if (s == "Osc2 Detune")   return "Detune";
    if (s == "Osc2 Octave")   return "Octave";
    if (s == "Sub Level")     return "Sub";
    if (s == "LFO Rate")      return "Rate";
    if (s == "LFO Depth")     return "Depth";
    if (s == "Amp Attack")    return "Atk";
    if (s == "Amp Decay")     return "Dcy";
    if (s == "Amp Sustain")   return "Sus";
    if (s == "Amp Release")   return "Rel";
    if (s == "Filt Attack")   return "Atk";
    if (s == "Filt Decay")    return "Dcy";
    if (s == "Filt Sustain")  return "Sus";
    if (s == "Filt Release")  return "Rel";
    if (s == "Loop Start")    return "Start";
    if (s == "Loop End")      return "End";
    if (s == "Sample Gain")   return "Gain";
    if (s == "Slice Count")   return "Slices";
    if (s == "Slice Mode")    return "Mode";
    if (s == "Orig BPM")      return "BPM";
    if (s == "Base Note")     return "Base";
    if (s == "Swing")         return "Swng";
    if (s == "Start Offset")  return "Offs";
    // DrumSlop per-pad
    if (s == "Pad Vol")       return "Vol";
    if (s == "Pad Pan")       return "Pan";
    if (s == "Pad Pitch")     return "Pitch";
    if (s == "Pad Rev")       return "Rev";
    if (s == "Pad Cutoff")    return "Cut";
    if (s == "Pad Reso")      return "Reso";
    if (s == "Pad Atk")       return "Atk";
    if (s == "Pad Dec")       return "Dcy";
    if (s == "Pad Sus")       return "Sus";
    if (s == "Pad Rel")       return "Rel";
    return s;
}

// ═══════════════════════════════════════════════════════════════════
// GroupedKnobBody — left-side display + N sections × knob-grid.
// Layout:  [Display?]  |  [Section 1]  |  [Section 2]  |  ...
// Each section: label row on top, then 2-row knob grid.
// ═══════════════════════════════════════════════════════════════════

class GroupedKnobBody : public CustomDeviceBody {
public:
    struct ParamDesc {
        int index;
        std::string name, unit;
        float minVal, maxVal, defaultVal;
        bool isBoolean;
    };
    struct SectionDef {
        std::string label;
        std::vector<int> paramIndices;
    };
    struct Config {
        // Optional left-side display widget — v1 fw::Widget for now.
        // GroupedKnobBody takes ownership on configure().
        ::yawn::ui::fw::Widget* display = nullptr;
        float displayWidth = 0;
        std::vector<SectionDef> sections;
    };

    GroupedKnobBody() { setName("GroupedKnobBody"); }

    ~GroupedKnobBody() override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                delete ke.knob;
        delete m_display;
    }

    void configure(const Config& config, const std::vector<ParamDesc>& allParams) {
        m_display      = config.display;
        m_displayWidth = config.displayWidth;

        for (auto& sd : config.sections) {
            InternalSection sec;
            sec.label = sd.label;
            for (int idx : sd.paramIndices) {
                if (idx < 0 || idx >= static_cast<int>(allParams.size())) continue;
                const auto& pd = allParams[idx];
                auto* k = new FwKnob();
                k->setRange(pd.minVal, pd.maxVal);
                k->setDefaultValue(pd.defaultVal);
                k->setValue(pd.defaultVal);
                k->setLabel(groupedShortenLabel(pd.name));

                const float range = pd.maxVal - pd.minVal;
                const bool isInteger = (range >= 2.0f && range <= 32.0f &&
                                         pd.minVal == std::floor(pd.minVal) &&
                                         pd.maxVal == std::floor(pd.maxVal) &&
                                         pd.unit.empty() && !pd.isBoolean);
                if (isInteger) {
                    k->setStep(1.0f);
                    k->setPixelsPerFullRange(48.0f);
                    k->setValueFormatter([](float v) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "%d",
                                       static_cast<int>(std::round(v)));
                        return std::string(buf);
                    });
                } else {
                    k->setValueFormatter([unit = pd.unit, isBool = pd.isBoolean](float v) {
                        return groupedFormatParamValue(v, unit, isBool);
                    });
                }

                k->setOnChange([this, pidx = pd.index](float v) {
                    if (m_onParamChange) m_onParamChange(pidx, v);
                });
                k->setOnDragEnd([this, pidx = pd.index](float startV, float endV) {
                    if (!m_onParamTouch) return;
                    m_onParamTouch(pidx, startV, /*touching*/true);
                    m_onParamTouch(pidx, endV,   /*touching*/false);
                });
                sec.knobs.push_back({pd.index, k});
            }
            m_sections.push_back(std::move(sec));
        }
    }

    void updateParamValue(int index, float value) override {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.paramIndex == index) {
                    if (!ke.knob->isDragging() && !ke.knob->isEditing())
                        ke.knob->setValue(value);
                    return;
                }
    }

    float preferredBodyWidth() const override {
        float w = 0;
        if (m_display) w += m_displayWidth + kSectionGap;
        for (auto& sec : m_sections)
            w += sectionWidth(sec) + kSectionGap;
        return w;
    }

    void setOnParamChange(std::function<void(int, float)> cb) override {
        m_onParamChange = std::move(cb);
    }

    void setOnParamTouch(std::function<void(int, float, bool)> cb) override {
        m_onParamTouch = std::move(cb);
    }

    // ─── v1-display cache ───────────────────────────────────────────
    // The hosting panel stashes its v1 UIContext here before calling
    // render()/onLayout() so we can drive the v1 display widget's
    // lifecycle. Non-owning; valid only for the duration of the call.
    void setV1Ctx(const ::yawn::ui::fw::UIContext* ctx) { m_v1Ctx = ctx; }

    ::yawn::ui::fw::Widget* display() const { return m_display; }

    // ─── fw2 Widget overrides ───────────────────────────────────────

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({preferredBodyWidth(), c.maxH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        Widget::onLayout(bounds, ctx);
        float x = bounds.x;
        const float y = bounds.y;
        const float h = bounds.h;

        if (m_display && m_v1Ctx) {
            // Ask the display what height it prefers at the slot width;
            // cap to our body height so it never overflows. Keeps sub-
            // displays (OSC / Filter / AMP / FILT envelopes) at their
            // intended ~4:3 aspect instead of stretching vertically.
            auto ps = m_display->measure(
                ::yawn::ui::fw::Constraints::loose(m_displayWidth, h),
                *m_v1Ctx);
            const float displayH = std::min(h, std::max(48.0f, ps.h));
            m_display->layout(
                ::yawn::ui::fw::Rect{x, y, m_displayWidth, displayH},
                *m_v1Ctx);
            x += m_displayWidth + kSectionGap;
        } else if (m_display) {
            x += m_displayWidth + kSectionGap;
        }

        for (auto& sec : m_sections) {
            const int   cols = sectionCols(sec);
            const float secW = sectionWidth(sec);
            sec.x = x;
            sec.w = secW;

            const float knobsX = x;
            const float knobY  = y + kLabelRowH;
            for (size_t i = 0; i < sec.knobs.size(); ++i) {
                const int col = static_cast<int>(i) % cols;
                const int row = static_cast<int>(i) / cols;
                const float kx = knobsX + col * kCellW;
                const float ky = knobY + row * kRowH;
                sec.knobs[i].knob->measure(
                    Constraints::tight(kKnobW, kKnobH), ctx);
                sec.knobs[i].knob->layout({kx, ky, kKnobW, kKnobH}, ctx);
            }

            x += secW + kSectionGap;
        }
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        // v1 display paints through v1 UIContext (Font/Renderer path).
        if (m_display && m_v1Ctx)
            m_display->paint(*const_cast<::yawn::ui::fw::UIContext*>(m_v1Ctx));

        // 8/26 ≈ 14.8 px actual (matches v1 look, fits within kLabelRowH).
        const float lblFs = 8.0f * (48.0f / 26.0f);
        bool first = true;
        for (auto& sec : m_sections) {
            if (!sec.label.empty() && tm)
                tm->drawText(r, sec.label,
                              sec.x + 2.0f, m_bounds.y + 2.0f,
                              lblFs, Color{120, 160, 220, 220});

            if (!first)
                r.drawRect(sec.x - kSectionGap / 2, m_bounds.y + 2,
                           1, m_bounds.h - 4, Color{55, 55, 65, 150});
            first = false;

            for (auto& ke : sec.knobs)
                ke.knob->render(ctx);
        }
    }
#endif

    // ─── Events ─────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e) override {
        // Forward to v1 display first (e.g. DrumSlop pad grid).
        if (m_display) {
            const auto& db = m_display->bounds();
            if (e.x >= db.x && e.x < db.x + db.w &&
                e.y >= db.y && e.y < db.y + db.h) {
                auto v1ev = ::yawn::ui::fw2::toFw1Mouse(e);
                if (m_display->onMouseDown(v1ev)) {
                    m_draggingDisplay = true;
                    captureMouse();
                    return true;
                }
            }
        }
        // Knob hit-test — each knob handles its own drag via gesture SM.
        for (auto& sec : m_sections) {
            for (auto& ke : sec.knobs) {
                const auto& kb = ke.knob->bounds();
                if (e.x < kb.x || e.x >= kb.x + kb.w) continue;
                if (e.y < kb.y || e.y >= kb.y + kb.h) continue;
                MouseEvent ev = e;
                ev.lx = e.x - kb.x;
                ev.ly = e.y - kb.y;
                ke.knob->dispatchMouseDown(ev);
                m_draggingKnob = ke.knob;
                captureMouse();
                return true;
            }
        }
        return false;
    }

    bool onMouseMove(MouseMoveEvent& e) override {
        if (m_draggingDisplay && m_display) {
            auto v1ev = ::yawn::ui::fw2::toFw1MouseMove(e);
            m_display->onMouseMove(v1ev);
            return true;
        }
        if (m_draggingKnob) {
            MouseMoveEvent ev = e;
            ev.lx = e.x - m_draggingKnob->bounds().x;
            ev.ly = e.y - m_draggingKnob->bounds().y;
            m_draggingKnob->dispatchMouseMove(ev);
            return true;
        }
        return false;
    }

    bool onMouseUp(MouseEvent& e) override {
        if (m_draggingDisplay && m_display) {
            auto v1ev = ::yawn::ui::fw2::toFw1Mouse(e);
            m_display->onMouseUp(v1ev);
            m_draggingDisplay = false;
            releaseMouse();
            return true;
        }
        if (m_draggingKnob) {
            MouseEvent ev = e;
            ev.lx = e.x - m_draggingKnob->bounds().x;
            ev.ly = e.y - m_draggingKnob->bounds().y;
            m_draggingKnob->dispatchMouseUp(ev);
            m_draggingKnob = nullptr;
            releaseMouse();
            return true;
        }
        return false;
    }

    // ─── Knob text-edit (forwarded from hosting panel) ──────────────

    FwKnob* editingKnob() const {
        for (auto& sec : m_sections)
            for (auto& ke : sec.knobs)
                if (ke.knob->isEditing()) return ke.knob;
        return nullptr;
    }

    bool hasEditingKnob() const override { return editingKnob() != nullptr; }

    bool forwardKeyDown(int key) override {
        auto* k = editingKnob();
        if (!k) return false;
        KeyEvent ke;
        switch (key) {
            case 27 /*SDLK_ESCAPE*/:    ke.key = Key::Escape;    break;
            case 13 /*SDLK_RETURN*/:    ke.key = Key::Enter;     break;
            case 8  /*SDLK_BACKSPACE*/: ke.key = Key::Backspace; break;
            default:                    return false;
        }
        return k->dispatchKeyDown(ke);
    }

    bool forwardTextInput(const char* text) override {
        auto* k = editingKnob();
        if (!k) return false;
        k->takeTextInput(text ? text : "");
        return true;
    }

    void cancelEditingKnobs() override {
        if (auto* k = editingKnob()) k->endEdit(/*commit*/false);
    }

private:
    static constexpr float kKnobW      = 52.0f;
    static constexpr float kKnobH      = 64.0f;
    static constexpr float kCellW      = 62.0f;
    static constexpr float kRowH       = 68.0f;
    static constexpr float kLabelRowH  = 20.0f;
    static constexpr float kSectionGap = 14.0f;
    static constexpr float kPadX       = 6.0f;
    static constexpr int   kMaxRows    = 2;

    struct KnobEntry { int paramIndex; FwKnob* knob; };
    struct InternalSection {
        std::string label;
        std::vector<KnobEntry> knobs;
        float x = 0, w = 0;
    };

    ::yawn::ui::fw::Widget*          m_display      = nullptr;
    float                             m_displayWidth = 0;
    std::vector<InternalSection>      m_sections;

    const ::yawn::ui::fw::UIContext* m_v1Ctx = nullptr;

    FwKnob*                           m_draggingKnob    = nullptr;
    bool                              m_draggingDisplay = false;
    std::function<void(int, float)>              m_onParamChange;
    std::function<void(int, float, bool)>        m_onParamTouch;

    static int sectionCols(const InternalSection& s) {
        const int n = static_cast<int>(s.knobs.size());
        return n <= kMaxRows ? 1 : (n + kMaxRows - 1) / kMaxRows;
    }
    static float sectionWidth(const InternalSection& s) {
        const float knobsW = static_cast<float>(sectionCols(s)) * kCellW;
        const float labelW = static_cast<float>(s.label.size()) * 8.0f + 4.0f;
        return std::max(knobsW, labelW);
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
