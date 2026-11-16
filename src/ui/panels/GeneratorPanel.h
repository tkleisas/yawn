#pragma once

// GeneratorPanel — the generative-song view (ContentGrid top-left
// slot). Style blend knobs, musical parameters, and a lane → track
// mapping populated by the last generation. App owns the engine and
// performs the actual commit (session scenes + clips).

#include "generate/GeneratorTypes.h"
#include "ui/framework/v2/FlexBox.h"
#include "ui/framework/v2/Knob.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/StepSelector.h"
#include "ui/framework/v2/NumberInput.h"
#include "ui/framework/v2/Toggle.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/Label.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

class GeneratorPanel : public FlexBox {
public:
    using GenerateCallback =
        std::function<void(const generate::GeneratorParams&)>;
    using CommitCallback =
        std::function<void(const std::vector<std::string>& roles,
                           const std::vector<int>& tracks)>;
    using CloseCallback = std::function<void()>;
    using ParamMenuCallback =
        std::function<void(int slot, float screenX, float screenY)>;
    using LoadPresetCallback = std::function<void(const std::string& name)>;
    using SavePresetCallback = std::function<void()>;

    GeneratorPanel();

    void setOnGenerate(GenerateCallback cb) { m_onGenerate = std::move(cb); }
    void setOnCommit(CommitCallback cb)     { m_onCommit   = std::move(cb); }
    void setOnClose(CloseCallback cb)       { m_onClose    = std::move(cb); }
    void setOnParamMenu(ParamMenuCallback cb) { m_onParamMenu = std::move(cb); }
    void setOnLoadPreset(LoadPresetCallback cb) { m_onLoadPreset = std::move(cb); }
    void setOnSavePreset(SavePresetCallback cb) { m_onSavePreset = std::move(cb); }

    // Populated by App on open / after generation.
    void setStyleIds(const std::vector<std::string>& ids);
    void setScaleIds(const std::vector<std::string>& ids);
    void setTrackNames(const std::vector<std::string>& names);
    void setGeneratedRoles(const std::vector<std::string>& roles);
    void setPresetNames(const std::vector<std::string>& names);
    void setParams(const generate::GeneratorParams& params);
    void clearGenerated();
    void setStatus(const std::string& text, bool error = false);

    // MIDI-Learn / remote-control updates from App.
    void setStyleWeight(int index, float value);
    void setDensity(float value);
    void setNoteLength(float value);
    void setSwing(float value);
    void setTempo(float value);
    void setAutoMix(bool on);
    int  styleCount() const { return static_cast<int>(m_styleKnobs.size()); }
    std::string styleId(int index) const;

    bool hasGenerated() const { return !m_roles.empty(); }
    const std::vector<std::string>& generatedRoles() const { return m_roles; }
    std::vector<int> chosenTracks() const;
    generate::GeneratorParams buildParams() const;

protected:
    Size onMeasure(Constraints c, UIContext& ctx) override;
    void onLayout(Rect bounds, UIContext& ctx) override;

private:
    void rebuildStyles();
    void rebuildLanes();
    void onGenerateClicked();
    void onCommitClicked();

    std::vector<std::string> m_styleIds;
    std::vector<std::unique_ptr<FwKnob>> m_styleKnobs;

    std::vector<std::string> m_scaleIds;
    std::vector<std::string> m_trackNames;
    std::vector<std::string> m_roles;

    FwDropDown     m_scale;
    FwDropDown     m_root;
    FwDropDown     m_movement;
    FwStepSelector m_blend;
    FwNumberInput  m_seed;
    FwNumberInput  m_tempo;
    FwNumberInput  m_density;
    FwNumberInput  m_noteLength;
    FwNumberInput  m_swing;
    FwToggle       m_autoMix;
    FwButton       m_generate;
    FwButton       m_commit;
    FwButton       m_reroll;
    FwButton       m_savePreset;
    FwButton       m_close;
    FwDropDown     m_preset;

    std::unique_ptr<Label> m_title;
    std::unique_ptr<Label> m_hint;
    std::unique_ptr<Label> m_styleHeader;
    std::unique_ptr<Label> m_paramsHeader;
    std::unique_ptr<Label> m_laneHeader;
    std::unique_ptr<Label> m_status;

    std::vector<std::unique_ptr<Label>>      m_paramLabels;
    std::vector<Widget*>                     m_paramControls;
    std::vector<float>                       m_paramWidths;

    std::vector<std::unique_ptr<Label>>      m_laneLabels;
    std::vector<std::unique_ptr<FwDropDown>> m_laneDrops;

    GenerateCallback   m_onGenerate;
    CommitCallback     m_onCommit;
    CloseCallback      m_onClose;
    ParamMenuCallback  m_onParamMenu;
    LoadPresetCallback m_onLoadPreset;
    SavePresetCallback m_onSavePreset;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
