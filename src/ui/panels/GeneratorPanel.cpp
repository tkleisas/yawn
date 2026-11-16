#include "ui/panels/GeneratorPanel.h"

#include "ui/framework/v2/Theme.h"
#include "generate/GeneratorParamBus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {

constexpr float kPad        = 16.0f;
constexpr float kGap        = 10.0f;
constexpr float kKnobW      = 76.0f;
constexpr float kKnobH      = 74.0f;
constexpr float kGroupH     = 40.0f;
constexpr float kLabelH     = 13.0f;
constexpr float kControlH   = 24.0f;
constexpr float kRowGap     = 8.0f;

const std::vector<std::string> kRootNames = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

std::string styleDisplayName(const std::string& id) {
    if (id == "ethnic_maqam") return "maqam";
    if (id == "classical")    return "classic";
    return id;
}

std::unique_ptr<Label> makeLabel(const std::string& text,
                                 TextColorToken token,
                                 float scale = 1.0f) {
    auto l = std::make_unique<Label>(text);
    l->setTextColorToken(token);
    l->setFontScale(scale);
    l->setTruncation(Truncation::Ellipsis);
    l->setAlign(TextAlign::Start, VerticalAlign::Middle);
    return l;
}

} // namespace

GeneratorPanel::GeneratorPanel() {
    setDirection(Direction::Column);
    setAlign(Align::Stretch);
    setAutoCaptureOnUnhandledPress(false);

    m_title = makeLabel("Generative Song", TextColorToken::Primary, 1.25f);
    m_hint = makeLabel(
        "Blend styles, generate, then assign each lane to a track. "
        "Sections become scenes with follow actions.",
        TextColorToken::Secondary);
    m_styleHeader = makeLabel("Style blend weights", TextColorToken::Primary);
    m_paramsHeader = makeLabel("Song parameters", TextColorToken::Primary);
    m_laneHeader = makeLabel("Lanes → destination tracks",
                             TextColorToken::Primary);
    m_status = makeLabel("", TextColorToken::Secondary);
    m_status->setTruncation(Truncation::Ellipsis);

    addChild(m_title.get());
    addChild(m_hint.get());
    addChild(m_styleHeader.get());
    addChild(m_paramsHeader.get());
    addChild(m_laneHeader.get());
    addChild(m_status.get());

    addChild(&m_scale);
    addChild(&m_root);
    addChild(&m_movement);
    addChild(&m_blend);
    addChild(&m_seed);
    addChild(&m_tempo);
    addChild(&m_density);
    addChild(&m_noteLength);
    addChild(&m_swing);
    addChild(&m_autoMix);
    addChild(&m_generate);
    addChild(&m_commit);
    addChild(&m_reroll);
    addChild(&m_close);
    addChild(&m_preset);
    addChild(&m_savePreset);

    std::vector<std::string> roots{ "Auto" };
    roots.insert(roots.end(), kRootNames.begin(), kRootNames.end());
    m_root.setItems(roots);
    m_root.setSelectedIndex(0);
    m_movement.setItems({ "Auto", "Ascending", "Descending", "Arch",
                          "Valley", "Random" });
    m_movement.setSelectedIndex(0);
    m_blend.setRange(0, 1);
    m_blend.setValue(0);
    m_blend.setFormatter([](int v) {
        return std::string(v == 0 ? "Fuse" : "Rotate");
    });

    m_scale.setItems({ "Auto" });
    m_scale.setSelectedIndex(0);

    m_seed.setRange(1.0f, 9999999.0f);
    m_seed.setFormat("%.0f");
    m_seed.setStep(1.0f);
    m_seed.setValue(1.0f);

    m_tempo.setRange(0.0f, 300.0f);
    m_tempo.setFormat("%.0f");
    m_tempo.setStep(1.0f);
    m_tempo.setSuffix(" BPM");
    m_tempo.setValue(0.0f);

    m_density.setRange(0.0f, 1.0f);
    m_noteLength.setRange(0.1f, 2.0f);
    m_swing.setRange(0.0f, 0.75f);
    for (FwNumberInput* n : { &m_density, &m_noteLength, &m_swing }) {
        n->setFormat("%.2f");
        n->setStep(0.01f);
    }
    m_density.setValue(0.55f);
    m_noteLength.setValue(0.5f);
    m_swing.setValue(0.0f);

    m_autoMix.setLabel("Auto mix");
    m_autoMix.setState(true);

    auto wireSlot = [this](FwNumberInput& n, int slot) {
        n.setOnRightClick([this, slot](Point p) {
            if (m_onParamMenu) m_onParamMenu(slot, p.x, p.y);
        });
    };
    wireSlot(m_density, generate::kGenDensity);
    wireSlot(m_noteLength, generate::kGenNoteLength);
    wireSlot(m_swing, generate::kGenSwing);
    wireSlot(m_tempo, generate::kGenTempo);

    m_generate.setLabel("Generate");
    m_generate.setHighlighted(true);
    m_generate.setOnClick([this]() { onGenerateClicked(); });
    m_commit.setLabel("Commit");
    m_commit.setOnClick([this]() { onCommitClicked(); });
    m_commit.setEnabled(false);
    m_reroll.setLabel("Reroll Seed");
    m_reroll.setOnClick([this]() {
        std::random_device rd;
        const int v = 1 + static_cast<int>(rd() % 9999999u);
        m_seed.setValue(static_cast<float>(v));
    });
    m_close.setLabel("Close");
    m_close.setOnClick([this]() { if (m_onClose) m_onClose(); });

    m_preset.setPlaceholder("Presets");
    m_preset.setMaxVisibleItems(12);
    m_preset.setOnChange([this](int idx, const std::string& label) {
        if (idx >= 0 && !label.empty() && m_onLoadPreset)
            m_onLoadPreset(label);
    });
    m_savePreset.setLabel("Save Preset");
    m_savePreset.setOnClick([this]() {
        if (m_onSavePreset) m_onSavePreset();
    });

    const char* paramNames[] = {
        "Seed", "Scale", "Root", "Movement", "Blend",
        "Tempo (0=auto)", "", "Density", "Note length", "Swing",
    };
    for (const char* name : paramNames)
        m_paramLabels.push_back(makeLabel(name, TextColorToken::Secondary));
    m_paramControls = {
        &m_seed, &m_scale, &m_root, &m_movement, &m_blend,
        &m_tempo, &m_autoMix, &m_density, &m_noteLength, &m_swing,
    };
    m_paramWidths = {
        90.0f, 160.0f, 90.0f, 130.0f, 110.0f,
        130.0f, 110.0f, 80.0f, 100.0f, 80.0f,
    };
    for (auto& l : m_paramLabels) addChild(l.get());

    m_laneHeader->setVisible(false);
    m_status->setText(
        "Choose styles, press Generate, then map lanes and Commit.");
}

void GeneratorPanel::setStyleIds(const std::vector<std::string>& ids) {
    m_styleIds = ids;
    rebuildStyles();
}

void GeneratorPanel::rebuildStyles() {
    for (auto& k : m_styleKnobs) removeChild(k.get());
    m_styleKnobs.clear();

    for (const auto& id : m_styleIds) {
        auto k = std::make_unique<FwKnob>();
        k->setLabel(styleDisplayName(id));
        k->setRange(0.0f, 1.0f);
        k->setValue(id == "pop" ? 1.0f : 0.0f);
        k->setDefaultValue(id == "pop" ? 1.0f : 0.0f);
        k->setValueFormatter([](float v) {
            char b[16];
            std::snprintf(b, sizeof(b), "%.2f", v);
            return std::string(b);
        });
        const int slot = generate::kGenStyleBase + static_cast<int>(m_styleKnobs.size());
        k->setOnRightClick([this, slot](Point p) {
            if (m_onParamMenu) m_onParamMenu(slot, p.x, p.y);
        });
        addChild(k.get());
        m_styleKnobs.push_back(std::move(k));
    }
    invalidate();
}

void GeneratorPanel::setScaleIds(const std::vector<std::string>& ids) {
    m_scaleIds = ids;
    std::vector<std::string> items{ "Auto" };
    items.insert(items.end(), ids.begin(), ids.end());
    const int keep = m_scale.selectedIndex();
    m_scale.setItems(items);
    m_scale.setSelectedIndex(
        keep >= 0 && keep < m_scale.itemCount() ? keep : 0);
}

void GeneratorPanel::setTrackNames(const std::vector<std::string>& names) {
    m_trackNames = names;
    for (auto& d : m_laneDrops) {
        if (!d) continue;
        const int keep = d->selectedIndex();
        d->setItems(m_trackNames);
        d->setSelectedIndex(
            keep >= 0 && keep < d->itemCount() ? keep : -1);
    }
    invalidate();
}

void GeneratorPanel::setGeneratedRoles(const std::vector<std::string>& roles) {
    m_roles = roles;
    rebuildLanes();
    m_commit.setEnabled(!m_roles.empty());
    m_laneHeader->setVisible(!m_roles.empty());
    invalidate();
}

void GeneratorPanel::clearGenerated() {
    m_roles.clear();
    rebuildLanes();
    m_commit.setEnabled(false);
    m_laneHeader->setVisible(false);
    invalidate();
}

void GeneratorPanel::rebuildLanes() {
    for (auto& l : m_laneLabels) removeChild(l.get());
    for (auto& d : m_laneDrops) removeChild(d.get());
    m_laneLabels.clear();
    m_laneDrops.clear();

    for (size_t i = 0; i < m_roles.size(); ++i) {
        auto l = makeLabel(m_roles[i], TextColorToken::Secondary);
        addChild(l.get());
        m_laneLabels.push_back(std::move(l));

        auto d = std::make_unique<FwDropDown>();
        d->setItems(m_trackNames);
        if (!m_trackNames.empty()) {
            const int idx = std::min(static_cast<int>(i),
                                     static_cast<int>(m_trackNames.size()) - 1);
            d->setSelectedIndex(idx);
        } else {
            d->setPlaceholder("No tracks");
        }
        addChild(d.get());
        m_laneDrops.push_back(std::move(d));
    }
}

void GeneratorPanel::setStatus(const std::string& text, bool error) {
    m_status->setText(text);
    if (error) {
        m_status->setColor(theme().palette.error);
    } else {
        m_status->clearColor();
        m_status->setTextColorToken(TextColorToken::Secondary);
    }
}

void GeneratorPanel::setPresetNames(const std::vector<std::string>& names) {
    if (names.empty()) {
        m_preset.setPlaceholder("No presets");
        m_preset.clearItems();
    } else {
        const int keep = m_preset.selectedIndex();
        const std::string keepLabel = keep >= 0 ? m_preset.selectedLabel()
                                                : std::string{};
        m_preset.setPlaceholder("Presets");
        m_preset.setItems(names);
        m_preset.setSelectedIndex(-1);
        if (!keepLabel.empty()) {
            for (int i = 0; i < m_preset.itemCount(); ++i) {
                if (m_preset.itemLabel(i) == keepLabel) {
                    m_preset.setSelectedIndex(i);
                    break;
                }
            }
        }
    }
    invalidate();
}

void GeneratorPanel::setParams(const generate::GeneratorParams& p) {
    for (size_t i = 0; i < m_styleIds.size(); ++i) {
        float w = 0.0f;
        for (const auto& mix : p.styles)
            if (mix.id == m_styleIds[i]) { w = mix.weight; break; }
        m_styleKnobs[i]->setValue(w);
    }
    auto selectByLabel = [](FwDropDown& d, const std::string& label,
                            int fallback) {
        if (label.empty()) { d.setSelectedIndex(fallback); return; }
        for (int i = 0; i < d.itemCount(); ++i) {
            if (d.itemLabel(i) == label) { d.setSelectedIndex(i); return; }
        }
        d.setSelectedIndex(fallback);
    };
    selectByLabel(m_scale, p.scale, 0);
    selectByLabel(m_root, p.root, 0);
    static const char* kMoves[] = {
        "auto", "ascending", "descending", "arch", "valley", "random",
    };
    int moveIdx = 0;
    for (int i = 0; i < 6; ++i)
        if (p.movement == kMoves[i]) { moveIdx = i; break; }
    m_movement.setSelectedIndex(moveIdx);
    m_blend.setValue(p.blend == "rotate" ? 1 : 0);
    m_seed.setValue(static_cast<float>(p.seed ? p.seed : 1));
    m_tempo.setValue(static_cast<float>(p.tempo));
    const bool autoMix = p.density < 0.0f || p.noteLength < 0.0f ||
                         p.swing < 0.0f;
    if (p.density >= 0.0f) m_density.setValue(std::clamp(p.density, 0.0f, 1.0f));
    if (p.noteLength >= 0.0f)
        m_noteLength.setValue(std::clamp(p.noteLength, 0.1f, 2.0f));
    if (p.swing >= 0.0f) m_swing.setValue(std::clamp(p.swing, 0.0f, 0.75f));
    m_autoMix.setState(autoMix);
    invalidate();
}

std::string GeneratorPanel::styleId(int index) const {
    if (index < 0 || index >= static_cast<int>(m_styleIds.size()))
        return {};
    return m_styleIds[static_cast<size_t>(index)];
}

void GeneratorPanel::setStyleWeight(int index, float value) {
    if (index < 0 || index >= static_cast<int>(m_styleKnobs.size())) return;
    m_styleKnobs[static_cast<size_t>(index)]->setValue(value);
    invalidate();
}

void GeneratorPanel::setDensity(float value) {
    m_density.setValue(std::clamp(value, 0.0f, 1.0f));
    m_autoMix.setState(false);
    invalidate();
}

void GeneratorPanel::setNoteLength(float value) {
    m_noteLength.setValue(std::clamp(value, 0.1f, 2.0f));
    m_autoMix.setState(false);
    invalidate();
}

void GeneratorPanel::setSwing(float value) {
    m_swing.setValue(std::clamp(value, 0.0f, 0.75f));
    m_autoMix.setState(false);
    invalidate();
}

void GeneratorPanel::setTempo(float value) {
    m_tempo.setValue(std::clamp(value, 0.0f, 300.0f));
    invalidate();
}

void GeneratorPanel::setAutoMix(bool on) {
    m_autoMix.setState(on);
    invalidate();
}

std::vector<int> GeneratorPanel::chosenTracks() const {
    std::vector<int> out;
    out.reserve(m_laneDrops.size());
    for (const auto& d : m_laneDrops)
        out.push_back(d ? d->selectedIndex() : -1);
    return out;
}

generate::GeneratorParams GeneratorPanel::buildParams() const {
    generate::GeneratorParams p;
    p.styles.clear();
    for (size_t i = 0; i < m_styleKnobs.size(); ++i) {
        const float w = m_styleKnobs[i]->value();
        if (w > 0.001f)
            p.styles.push_back({ m_styleIds[i], w });
    }
    p.blend = (m_blend.value() == 0) ? "fuse" : "rotate";
    p.scale = m_scale.selectedIndex() > 0 ? m_scale.selectedLabel() : "";
    p.root = m_root.selectedIndex() > 0 ? m_root.selectedLabel() : "";
    const int mv = std::clamp(m_movement.selectedIndex(), 0, 5);
    static const char* kMoves[] = {
        "auto", "ascending", "descending", "arch", "valley", "random",
    };
    p.movement = kMoves[mv];
    p.seed = static_cast<uint64_t>(std::llround(m_seed.value()));
    if (p.seed == 0) p.seed = 1;
    p.tempo = static_cast<int>(std::lround(m_tempo.value()));
    p.beatsPerBar = 4;
    if (m_autoMix.state()) {
        p.density = -1.0f;
        p.noteLength = -1.0f;
        p.swing = -1.0f;
    } else {
        p.density = m_density.value();
        p.noteLength = m_noteLength.value();
        p.swing = m_swing.value();
    }
    return p;
}

void GeneratorPanel::onGenerateClicked() {
    const auto p = buildParams();
    if (p.styles.empty()) {
        setStatus("Enable at least one style weight (above 0).", true);
        return;
    }
    if (m_onGenerate) m_onGenerate(p);
}

void GeneratorPanel::onCommitClicked() {
    if (m_roles.empty()) {
        setStatus("Generate a song first.", true);
        return;
    }
    const auto tracks = chosenTracks();
    for (int t : tracks) {
        if (t < 0) {
            setStatus("Choose a destination track for every lane.", true);
            return;
        }
    }
    if (m_onCommit) m_onCommit(m_roles, tracks);
}

Size GeneratorPanel::onMeasure(Constraints c, UIContext& ctx) {
    (void)ctx;
    return c.constrain({ c.maxW, c.maxH });
}

void GeneratorPanel::onLayout(Rect bounds, UIContext& ctx) {
    const float x0 = bounds.x + kPad;
    const float right = bounds.x + bounds.w - kPad;
    const float availW = std::max(200.0f, bounds.w - 2.0f * kPad);

    auto layoutChild = [&](Widget* w, float x, float y, float ww, float hh) {
        if (!w) return;
        w->measure(Constraints::tight(ww, hh), ctx);
        w->layout(Rect{ x, y, ww, hh }, ctx);
    };

    float y = bounds.y + kPad;

    const float closeW = 80.0f;
    const float saveW = 96.0f;
    const float presetW = 200.0f;
    layoutChild(&m_close, right - closeW, y, closeW, 24.0f);
    layoutChild(&m_savePreset, right - closeW - 8.0f - saveW, y, saveW, 24.0f);
    layoutChild(&m_preset, right - closeW - 8.0f - saveW - 8.0f - presetW,
                y, presetW, 24.0f);
    const float titleW = std::max(120.0f,
        right - closeW - saveW - presetW - 32.0f - x0);
    layoutChild(m_title.get(), x0, y, titleW, 26.0f);
    y += 28.0f;
    layoutChild(m_hint.get(), x0, y, availW, 18.0f);
    y += 24.0f;

    layoutChild(m_styleHeader.get(), x0, y, 220.0f, 16.0f);
    y += 20.0f;

    if (!m_styleKnobs.empty()) {
        const int cols = std::max(
            2, static_cast<int>((availW + kGap) / (kKnobW + kGap)));
        int col = 0;
        float x = x0;
        for (auto& k : m_styleKnobs) {
            if (col == cols) {
                col = 0;
                x = x0;
                y += kKnobH + kRowGap;
            }
            layoutChild(k.get(), x, y, kKnobW, kKnobH);
            x += kKnobW + kGap;
            ++col;
        }
        y += kKnobH + 14.0f;
    }

    layoutChild(m_paramsHeader.get(), x0, y, 220.0f, 16.0f);
    y += 20.0f;

    {
        float x = x0;
        for (size_t i = 0; i < m_paramControls.size(); ++i) {
            const float w = m_paramWidths[i];
            if (x > x0 && x + w > right) {
                x = x0;
                y += kGroupH + 4.0f;
            }
            layoutChild(m_paramLabels[i].get(), x, y, w, kLabelH);
            layoutChild(m_paramControls[i], x, y + kLabelH + 1.0f, w,
                        kControlH);
            x += w + 12.0f;
        }
        y += kGroupH + 10.0f;
    }

    {
        float x = x0;
        const float bw = 110.0f;
        for (FwButton* b : { &m_generate, &m_commit, &m_reroll }) {
            if (x > x0 && x + bw > right) {
                x = x0;
                y += 30.0f;
            }
            layoutChild(b, x, y, bw, 26.0f);
            x += bw + kGap;
        }
        y += 36.0f;
    }

    if (!m_roles.empty()) {
        layoutChild(m_laneHeader.get(), x0, y, 300.0f, 16.0f);
        y += 20.0f;
        float x = x0;
        const float lw = 160.0f;
        for (size_t i = 0; i < m_roles.size(); ++i) {
            if (x > x0 && x + lw > right) {
                x = x0;
                y += kGroupH + 4.0f;
            }
            layoutChild(m_laneLabels[i].get(), x, y, lw, kLabelH);
            layoutChild(m_laneDrops[i].get(), x, y + kLabelH + 1.0f, lw,
                        kControlH);
            x += lw + 12.0f;
        }
        y += kGroupH + 8.0f;
    }

    const float statusY = std::min(y, bounds.y + bounds.h - 30.0f);
    layoutChild(m_status.get(), x0, statusY, availW, 26.0f);
}

} // namespace fw2
} // namespace ui
} // namespace yawn
