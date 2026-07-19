// DetailDisplayRegistry.cpp — per-device display builders, extracted
// from DetailPanelWidget.h's setupInstrumentDisplay /
// setupAudioEffectDisplay / setupMidiEffectDisplay if-chains.
// One function + one table row per device. See the header for the
// extension contract.

#include "ui/panels/DetailDisplayRegistry.h"

#include "ui/panels/DetailPanelWidget.h"

// Concrete devices
#include "instruments/SubtractiveSynth.h"
#include "instruments/FMSynth.h"
#include "instruments/Sampler.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSlop.h"
#include "instruments/KarplusStrong.h"
#include "instruments/WavetableSynth.h"
#include "instruments/GranularSynth.h"
#include "instruments/Vocoder.h"
#include "instruments/Multisampler.h"
#include "instruments/InstrumentRack.h"
#include "instruments/DrumSynth.h"
#include "instruments/StringMachine.h"
#include "instruments/DrawbarOrgan.h"
#include "instruments/ElectricPiano.h"
#include "effects/SplineEQ.h"
#include "effects/ConvolutionReverb.h"
#include "effects/NeuralAmp.h"
#include "effects/Filter.h"
#include "midi/LFO.h"

// Display panels
#include "ui/framework/v2/FMAlgorithmWidget.h"
#include "ui/framework/v2/SubSynthDisplayPanel.h"
#include "ui/framework/v2/SamplerDisplayPanel.h"
#include "ui/framework/v2/DrumSlopDisplayPanel.h"
#include "ui/framework/v2/WavetableDisplayPanel.h"
#include "ui/framework/v2/GranularDisplayPanel.h"
#include "ui/framework/v2/VocoderDisplayPanel.h"
#include "ui/framework/v2/DrumRackDisplayPanel.h"
#include "ui/framework/v2/InstrumentRackDisplayPanel.h"
#include "ui/framework/v2/MultisamplerDisplayPanel.h"
#include "ui/framework/v2/SplineEQDisplayPanel.h"
#include "ui/framework/v2/ConvReverbDisplayPanel.h"
#include "ui/framework/v2/NeuralAmpDisplayPanel.h"
#include "ui/framework/v2/FilterDisplayWidget.h"
#include "ui/framework/v2/LFODisplayWidget.h"

#include <cmath>

namespace yawn {
namespace ui {
namespace fw2 {

// ── Panel-internals accessor ──────────────────────────────────────
// The builders legitimately need the panel's callback members (menu
// hooks, sidechain providers, cross-panel stashes) and its updater
// list. DetailPanelWidget befriends this one struct rather than
// growing a dozen public getters.
namespace detail {

struct DisplayRegistryAccess {
    static std::vector<std::function<void()>>& updaters(DetailPanelWidget& p) {
        return p.m_displayUpdaters;
    }
    static auto& onDrumPadFxMenu(DetailPanelWidget& p)        { return p.m_onDrumPadFxMenu; }
    static auto& onInstrackChainFxMenu(DetailPanelWidget& p)  { return p.m_onInstrackChainFxMenu; }
    static auto& onAutoSampleRequested(DetailPanelWidget& p)  { return p.m_onAutoSampleRequested; }
    static auto& onLoadConvIR(DetailPanelWidget& p)           { return p.m_onLoadConvIR; }
    static auto& onLoadNamModel(DetailPanelWidget& p)         { return p.m_onLoadNamModel; }
    static auto& onLfoTargetMenu(DetailPanelWidget& p)        { return p.m_onLfoTargetMenu; }
    static auto& setSidechainSourceCb(DetailPanelWidget& p)   { return p.m_setSidechainSource; }
    static auto& trackNamesProvider(DetailPanelWidget& p)     { return p.m_trackNamesProvider; }
    static auto& sidechainSourceProvider(DetailPanelWidget& p){ return p.m_sidechainSourceProvider; }
    static auto& lfoTargetNameResolver(DetailPanelWidget& p)  { return p.m_lfoTargetNameResolver; }
    static int   autoTrackIndex(const DetailPanelWidget& p)   { return p.m_autoTrackIndex; }
    static int   clipSampleRate(const DetailPanelWidget& p)   { return p.m_clipSampleRate; }
    static auto& drumRackDisplay(DetailPanelWidget& p)        { return p.m_drumRackDisplay; }
    static auto& drumRackInst(DetailPanelWidget& p)           { return p.m_drumRackInst; }
    static auto& instrackInst(DetailPanelWidget& p)           { return p.m_instrackInst; }
    static auto& msDisplay(DetailPanelWidget& p)              { return p.m_msDisplay; }
    static auto& msInst(DetailPanelWidget& p)                 { return p.m_msInst; }
    static auto& pendingMsPanelWidthHook(DetailPanelWidget& p){ return p.m_pendingMsPanelWidthHook; }
};

} // namespace detail
using Access = detail::DisplayRegistryAccess;

namespace {

// ── Instrument builders ───────────────────────────────────────────

DeviceDisplaySetup buildFMSynth(const DisplayBuildArgs&, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* algoW = new FMAlgorithmWidget();
    out.config.display = algoW;
    out.config.displayWidth = 130;
    out.config.sections = {
        {"",     {0, 1, 18}},         // Algorithm, Feedback, Volume
        {"Op 1", {2, 3, 4, 5}},       // Level, Ratio, Attack, Release
        {"Op 2", {6, 7, 8, 9}},
        {"Op 3", {10, 11, 12, 13}},
        {"Op 4", {14, 15, 16, 17}},
    };
    out.updater = [algoW, inst]() {
        algoW->setAlgorithm(static_cast<int>(inst->getParameter(0)));
        algoW->setFeedback(inst->getParameter(1));
        algoW->setOpLevels(inst->getParameter(2), inst->getParameter(6),
                           inst->getParameter(10), inst->getParameter(14));
    };
    return out;
}

DeviceDisplaySetup buildSubSynth(const DisplayBuildArgs&, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* panel = new SubSynthDisplayPanel();
    out.config.display = panel;
    out.config.displayWidth = 150;
    out.config.sections = {
        {"Osc",      {0, 1, 2, 3, 4, 5, 6, 7}},
        {"Filter",   {8, 9, 10, 11}},
        {"Amp",      {12, 13, 14, 15}},
        {"Filt Env", {16, 17, 18, 19}},
        {"LFO",      {20, 21, 22}},
    };
    out.updater = [panel, inst]() {
        // Param 8 (Filter Cutoff) is 0..1 normalized → convert
        // to Hz for the display's filter-curve widget.
        const float cutoffHz =
            instruments::SubtractiveSynth::cutoffNormToHz(
                inst->getParameter(8));
        panel->updateFromParams(
            inst->getParameter(0),  inst->getParameter(1),
            inst->getParameter(2),  inst->getParameter(3),
            cutoffHz,               inst->getParameter(9),
            inst->getParameter(10),
            inst->getParameter(12), inst->getParameter(13),
            inst->getParameter(14), inst->getParameter(15),
            inst->getParameter(16), inst->getParameter(17),
            inst->getParameter(18), inst->getParameter(19));
    };
    return out;
}

DeviceDisplaySetup buildSampler(const DisplayBuildArgs&, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* samplerPanel = new SamplerDisplayPanel();
    out.config.display = samplerPanel;
    out.config.displayWidth = 130;
    out.config.sections = {
        {"Sample", {0, 10, 11}},
        {"Loop",   {8, 9}},
        {"Amp",    {1, 2, 3, 4}},
        {"Filter", {5, 6}},
        {"",       {7}},
    };
    out.updater = [samplerPanel, inst]() {
        samplerPanel->setADSR(inst->getParameter(1), inst->getParameter(2),
                              inst->getParameter(3), inst->getParameter(4));
        samplerPanel->setLoopPoints(inst->getParameter(8), inst->getParameter(9));
        samplerPanel->setReverse(inst->getParameter(10) > 0.5f);
        auto* sampler = dynamic_cast<instruments::Sampler*>(inst);
        if (sampler) {
            if (sampler->hasSample())
                samplerPanel->setSampleData(sampler->sampleData(),
                                            sampler->sampleFrames(),
                                            sampler->sampleChannels());
            samplerPanel->setPlayhead(sampler->playheadPosition(),
                                      sampler->isPlaying());
        }
    };
    return out;
}

DeviceDisplaySetup buildDrumSlop(const DisplayBuildArgs&, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* dsPanel = new DrumSlopDisplayPanel();
    out.config.display = dsPanel;
    out.config.displayWidth = 160;
    out.config.sections = {
        {"Global", {0, 1, 2, 3, 4, 5}},
        {"Pad", {6, 7, 8, 9, 10, 11}},
        {"Pad Env", {12, 13, 14, 15}},
    };

    // Pad click callback to select pad
    dsPanel->setOnPadClick([inst](int padIdx) {
        auto* ds = dynamic_cast<instruments::DrumSlop*>(inst);
        if (ds) ds->setSelectedPad(padIdx);
    });

    out.updater = [dsPanel, inst]() {
        auto* ds = dynamic_cast<instruments::DrumSlop*>(inst);
        if (!ds) return;

        dsPanel->setSliceCount(ds->sliceCount());
        dsPanel->setBaseNote(static_cast<int>(ds->getParameter(
            instruments::DrumSlop::kBaseNote)));
        dsPanel->setSelectedPad(ds->selectedPad());

        if (ds->hasLoop()) {
            dsPanel->setLoopData(ds->loopData(), ds->loopFrames(),
                                 ds->loopChannels());
            // Build slice boundaries for display
            std::vector<int64_t> bounds;
            int sc = ds->sliceCount();
            for (int i = 0; i <= sc; ++i)
                bounds.push_back(ds->sliceBoundary(i));
            dsPanel->setSliceBoundaries(bounds);
        }

        for (int i = 0; i < instruments::DrumSlop::kNumPads; ++i)
            dsPanel->setPadPlaying(i, ds->isPadPlaying(i));
    };
    return out;
}

DeviceDisplaySetup buildWavetable(const DisplayBuildArgs&, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* wtPanel = new WavetableDisplayPanel();
    out.config.display = wtPanel;
    out.config.displayWidth = 130;
    out.config.sections = {
        {"Osc",      {0, 1, 14, 15, 16}},
        {"Amp",      {5, 6, 7, 8}},
        {"Filter",   {2, 3, 4}},
        {"Filt Env", {9, 10, 11, 12}},
        {"",         {13, 17}},
    };
    static const char* tableNames[] = {
        "Basic", "PWM", "Formant", "Harmonic", "Digital"
    };
    out.updater = [wtPanel, inst]() {
        auto* wt = dynamic_cast<instruments::WavetableSynth*>(inst);
        if (!wt) return;
        int table = wt->currentTable();
        float pos = wt->getParameter(instruments::WavetableSynth::kPosition);
        int numFrames = wt->frameCount(table);
        if (numFrames > 0) {
            // Compute morphed waveform at current position
            static float morphBuf[instruments::WavetableSynth::kFrameSize];
            float framePos = pos * (numFrames - 1);
            int f0 = static_cast<int>(framePos);
            int f1 = std::min(f0 + 1, numFrames - 1);
            float frac = framePos - f0;
            const float* d0 = wt->frameData(table, f0);
            const float* d1 = wt->frameData(table, f1);
            if (d0 && d1) {
                for (int s = 0; s < instruments::WavetableSynth::kFrameSize; ++s)
                    morphBuf[s] = d0[s] * (1.0f - frac) + d1[s] * frac;
                wtPanel->setWaveformData(morphBuf,
                                         instruments::WavetableSynth::kFrameSize);
            }
        }
        wtPanel->setPosition(pos);
        if (table >= 0 && table < 5)
            wtPanel->setTableName(tableNames[table]);
    };
    return out;
}

DeviceDisplaySetup buildGranular(const DisplayBuildArgs&, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* grPanel = new GranularDisplayPanel();
    out.config.display = grPanel;
    out.config.displayWidth = 130;
    out.config.sections = {
        {"Grain",    {0, 1, 2, 3}},
        {"Pitch",    {4, 5, 14}},
        {"Shape",    {6, 7, 12, 13}},
        {"Filter",   {8, 9}},
        {"Env",      {10, 11}},
        {"",         {15, 16}},
    };
    out.updater = [grPanel, inst]() {
        auto* gr = dynamic_cast<instruments::GranularSynth*>(inst);
        if (!gr) return;
        if (gr->hasSample())
            grPanel->setSampleData(gr->sampleData(), gr->sampleFrames(),
                                   gr->sampleChannels());
        grPanel->setPosition(gr->currentPosition());
        grPanel->setScanPosition(gr->scanPosition());
        grPanel->setPlaying(gr->isPlaying());
        // Normalize grain size for display (100ms / total length)
        float grainMs = gr->getParameter(instruments::GranularSynth::kGrainSize);
        float totalMs = gr->sampleFrames() > 0
            ? 1000.0f * gr->sampleFrames() / 44100.0f : 1.0f;
        grPanel->setGrainSize(grainMs / totalMs);
    };
    return out;
}

DeviceDisplaySetup buildVocoder(const DisplayBuildArgs& a, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* vocPanel = new VocoderDisplayPanel();
    out.config.display = vocPanel;
    // Slightly wider than the original 130 px so the sidechain
    // source dropdown has room when it appears, but not so wide
    // that the section knobs to the right get squeezed and
    // start their labels overlapping. 180 px fits "(none)" /
    // "Track 1" / "Audio 1" comfortably — long names get
    // ellipsised by the dropdown painter.
    out.config.displayWidth = 180;
    out.config.sections = {
        {"Source",   {1, 15, 2, 16}},   // Carrier Type, Detune, Mod Source, Carrier=SC
        {"Bands",    {0, 3, 6}},
        {"Envelope", {4, 5}},
        {"Mix",      {7, 8, 9}},
        {"Amp",      {10, 11}},
        {"Output",   {12, 13, 14}},
    };

    // Wire the device-side sidechain picker → App callback.
    // App-side handler updates project.track[t].sidechainSource
    // and sends SetSidechainSourceMsg to the audio engine.
    auto& panel = a.panel;
    vocPanel->setOnSidechainSourceChanged(
        [&panel](int sourceIdx) {
            const int track = Access::autoTrackIndex(panel);
            if (track < 0) return;
            if (Access::setSidechainSourceCb(panel))
                Access::setSidechainSourceCb(panel)(track, sourceIdx);
        });

    out.updater = [&panel, vocPanel, inst]() {
        auto* voc = dynamic_cast<instruments::Vocoder*>(inst);
        if (!voc) return;
        if (voc->hasModulatorSample())
            vocPanel->setModulatorData(voc->modulatorData(),
                                       voc->modulatorFrames());
        vocPanel->setPlayhead(voc->modulatorPlayhead());
        vocPanel->setPlaying(voc->isPlaying());
        vocPanel->setBandCount(
            static_cast<int>(voc->getParameter(instruments::Vocoder::kBands)));
        vocPanel->setModSource(
            static_cast<int>(voc->getParameter(instruments::Vocoder::kModSource)));
        vocPanel->setModLevel(voc->consumeModulatorLevel());
        // Drain the per-band envelope peaks into the display
        // panel so the spectrum strip animates with the
        // modulator's spectral content. Sized to kMaxBands
        // (32); trailing entries are simply ignored at the
        // current band count.
        float bandLevels[instruments::Vocoder::kMaxBands] = {};
        voc->consumeBandLevels(bandLevels,
                               instruments::Vocoder::kMaxBands);
        vocPanel->setBandLevels(bandLevels,
                                instruments::Vocoder::kMaxBands);

        // Pull the latest project track list + current sidechain
        // source so the dropdown stays in sync with whatever
        // the Mixer I/O strip might have changed.
        const int track = Access::autoTrackIndex(panel);
        if (Access::trackNamesProvider(panel)) {
            vocPanel->setAvailableSources(
                Access::trackNamesProvider(panel)(), track);
        }
        if (Access::sidechainSourceProvider(panel) && track >= 0) {
            vocPanel->setSidechainSource(
                Access::sidechainSourceProvider(panel)(track));
        }
    };
    return out;
}

DeviceDisplaySetup buildDrumSynth(const DisplayBuildArgs&, instruments::Instrument*) {
    DeviceDisplaySetup out;
    // 8 sections, one per drum, in MIDI-ascending order (matches
    // the DrumRoll's row order — bottom = Kick (note 36), top
    // = Tambourine (note 54)). Kick has 7 params (the only drum
    // with sine/white/pink mix knobs); the others have the
    // standard 4 (tune, attack, decay, drive).
    //
    // GroupedKnobBody packs 7 knobs into a 4×2 grid (cols=
    // ceil(7/2)=4) and 4 knobs into a 2×2 grid (cols=ceil(4/2)
    // =2), so the kick strip is roughly twice as wide as each
    // other-drum strip. Total body width ≈ 1230 px which fits
    // comfortably on a 1280-px screen with horizontal scroll
    // for narrower displays.
    out.config.sections = {
        {"Kick",  { 0,  1,  2,  3,  4,  5,  6}},
        {"Snare", { 7,  8,  9, 10}},
        {"Clap",  {11, 12, 13, 14}},
        {"Tom1",  {15, 16, 17, 18}},
        {"CHH",   {19, 20, 21, 22}},
        {"OHH",   {23, 24, 25, 26}},
        {"Tom2",  {27, 28, 29, 30}},
        {"Tamb",  {31, 32, 33, 34}},
        {"Global",{35}},   // OS 2x oversampling toggle
    };
    return out;
}

DeviceDisplaySetup buildDrumRack(const DisplayBuildArgs& a, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* drPanel = new DrumRackDisplayPanel();
    out.config.display = drPanel;
    out.config.displayWidth = 160;
    // 0 = Volume (Global). 1..8 = per-selected-pad:
    // Pad Volume / Pan / Pitch / Choke / Attack / Decay
    // / Start / End. Grouped into Pad / AR Env / Region
    // so each row has breathing space.
    out.config.sections = {
        {"Global", {0}},
        {"Pad",    {1, 2, 3, 4}},
        {"AR Env", {5, 6}},
        {"Region", {7, 8}},
    };

    auto& panel = a.panel;
    // Stash for the cross-panel drag-drop hook so audio
    // clips dropped on a pad can be routed there.
    Access::drumRackDisplay(panel) = drPanel;
    Access::drumRackInst(panel)    = dynamic_cast<instruments::DrumRack*>(inst);

    drPanel->setOnPadClick([inst](int note) {
        auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
        if (dr) dr->setSelectedPad(note);
    });

    // Right-click on a pad → forward up to App so it can
    // open the per-pad fx context menu (Add / Remove
    // effects). DetailPanel doesn't know about the audio-
    // effect factory; App owns that knowledge.
    drPanel->setOnPadRightClick([&panel, inst](int note,
                                               float sx, float sy) {
        auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
        if (dr && Access::onDrumPadFxMenu(panel))
            Access::onDrumPadFxMenu(panel)(dr, note, sx, sy);
    });

    out.updater = [drPanel, inst, lastSel = -1]() mutable {
        auto* dr = dynamic_cast<instruments::DrumRack*>(inst);
        if (!dr) return;

        int sel = dr->selectedPad();
        drPanel->setSelectedPad(sel);

        // Only auto-jump the page when the selection ACTUALLY
        // changed (e.g. external setSelectedPad from drop or
        // MIDI click). Without this guard, the updater fired
        // every frame would force the page to track sel/16
        // and any user-driven page navigation (arrow buttons
        // / page labels / scroll wheel) would be reverted on
        // the next render tick.
        if (sel != lastSel) {
            drPanel->setPage(sel / 16);
            lastSel = sel;
        }

        // Update sample/playing state for all 128 pads
        for (int n = 0; n < instruments::DrumRack::kNumPads; ++n) {
            drPanel->setPadHasSample(n, dr->hasSample(n));
            drPanel->setPadPlaying(n, dr->isPadPlaying(n));
        }

        // Selected pad waveform
        const float* data = dr->padSampleData(sel);
        if (data)
            drPanel->setSelectedPadWaveform(data, dr->padSampleFrames(sel),
                                            dr->padSampleChannels(sel));
        else
            drPanel->setSelectedPadWaveform(nullptr, 0, 1);

        // Region trim markers — pushed every frame so the
        // markers track the Region knobs live as the user
        // drags them, and so a kit-preset load also updates
        // the display without an extra rebuild.
        drPanel->setSelectedPadRegion(dr->padStart(sel),
                                      dr->padEnd(sel));
    };
    return out;
}

DeviceDisplaySetup buildInstrumentRack(const DisplayBuildArgs& a, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* irPanel = new InstrumentRackDisplayPanel();
    out.config.display = irPanel;
    // Bumped 160 → 240 so the chain rows fit "1: Subtractive
    // Synth" without truncation at the user's theme font
    // scale, and so the key/vel range bars have enough width
    // to read at a glance.
    out.config.displayWidth = 240;
    out.config.sections = {
        {"Rack",  {0}},
        {"Chain", {1, 2, 3, 4, 5, 6}},
    };

    auto& panel = a.panel;
    // Stash the rack pointer so tick() can detect a chain
    // selection or per-chain fx count change and trigger a
    // rebuild — same pattern as m_drumRackInst.
    Access::instrackInst(panel) = dynamic_cast<instruments::InstrumentRack*>(inst);

    irPanel->setOnChainClick([inst](int idx) {
        auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
        if (ir) ir->setSelectedChain(idx);
    });

    // Right-click on a chain row → forward up to App so it
    // can open the per-chain fx context menu (Add / Remove
    // effects). DetailPanel doesn't know about the audio-
    // effect factory; App owns that knowledge.
    irPanel->setOnChainRightClick([&panel, inst](int chainIdx,
                                                  float sx, float sy) {
        auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
        if (ir && Access::onInstrackChainFxMenu(panel))
            Access::onInstrackChainFxMenu(panel)(ir, chainIdx, sx, sy);
    });

    irPanel->setOnAddChain([inst]() {
        auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
        if (ir && ir->chainCount() < instruments::InstrumentRack::kMaxChains)
            ir->addChain(std::make_unique<instruments::SubtractiveSynth>());
    });

    irPanel->setOnRemoveChain([inst](int idx) {
        auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
        if (ir && idx < ir->chainCount())
            ir->removeChain(idx);
    });

    irPanel->setOnToggleChain([inst](int idx) {
        auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
        if (ir && idx < ir->chainCount())
            ir->chain(idx).enabled = !ir->chain(idx).enabled;
    });

    out.updater = [irPanel, inst]() {
        auto* ir = dynamic_cast<instruments::InstrumentRack*>(inst);
        if (!ir) return;

        irPanel->setChainCount(ir->chainCount());
        irPanel->setSelectedChain(ir->selectedChain());

        for (int i = 0; i < ir->chainCount(); ++i) {
            const auto& ch = ir->chain(i);
            InstrumentRackDisplayPanel::ChainInfo ci;
            ci.name    = ch.instrument ? ch.instrument->name() : "Empty";
            ci.keyLow  = ch.keyLow;
            ci.keyHigh = ch.keyHigh;
            ci.velLow  = ch.velLow;
            ci.velHigh = ch.velHigh;
            ci.volume  = ch.volume;
            ci.enabled = ch.enabled;
            irPanel->setChain(i, ci);
        }
    };
    return out;
}

DeviceDisplaySetup buildMultisampler(const DisplayBuildArgs& a, instruments::Instrument* inst) {
    DeviceDisplaySetup out;
    auto* msPanel = new MultisamplerDisplayPanel();
    out.config.display = msPanel;
    auto& panel = a.panel;
    // Stash for the cross-panel drag-drop hook so audio
    // clips dropped on the panel can be added as zones.
    Access::msDisplay(panel) = msPanel;
    Access::msInst(panel)    = dynamic_cast<instruments::Multisampler*>(inst);
    // Wider than other display panels because it hosts both
    // a zone list and a per-zone editor side-by-side.
    out.config.displayWidth = 360.0f;
    // Multisampler param indices (Multisampler::Param):
    //   0..3  Amp ADSR
    //   4..6  Filter (Cutoff, Reso, Env amount)
    //   7..10 Filter ADSR
    //   11    Glide
    //   12    VelXfade
    //   13    Volume
    out.config.sections = {
        {"Amp",      {0, 1, 2, 3}},
        {"Filter",   {4, 5, 6}},
        {"Filt Env", {7, 8, 9, 10}},
        {"Misc",     {11, 12, 13}},
    };

    // Edit a single zone's metadata in place. The audio thread
    // reads zone fields without a lock, same caveat as
    // addZone / clearZones — UI-thread mutation is racy in
    // theory but harmless in practice for one-int writes.
    msPanel->setOnZoneFieldChange([inst](int zoneIdx,
            const MultisamplerDisplayPanel::ZoneRow& src) {
        auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
        if (!ms) return;
        auto* z = ms->zone(zoneIdx);
        if (!z) return;
        z->rootNote = src.rootNote;
        z->lowKey   = src.lowKey;
        z->highKey  = src.highKey;
        z->lowVel   = src.lowVel;
        z->highVel  = src.highVel;
        z->tune     = src.tune;
        z->volume   = src.volume;
        z->pan      = src.pan;
        z->loop     = src.loop;
    });

    msPanel->setOnRemoveZone([inst](int zoneIdx) {
        auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
        if (!ms) return;
        ms->removeZone(zoneIdx);
    });

    msPanel->setOnAutoSampleClicked([&panel, inst]() {
        // Forward the click up to App, which owns the
        // FwAutoSampleDialog and the engine/midi context the
        // dialog needs to open with.
        auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
        if (ms && Access::onAutoSampleRequested(panel)) {
            Access::onAutoSampleRequested(panel)(ms);
        }
    });

    // Push fresh zone rows from the instrument each frame.
    // setZones short-circuits when nothing changed, so the
    // user's mid-edit knob values aren't stomped.
    out.updater = [msPanel, inst]() {
        auto* ms = dynamic_cast<instruments::Multisampler*>(inst);
        if (!ms) return;
        std::vector<MultisamplerDisplayPanel::ZoneRow> rows;
        rows.reserve(ms->zoneCount());
        for (int i = 0; i < ms->zoneCount(); ++i) {
            const auto* z = ms->zone(i);
            if (!z) continue;
            MultisamplerDisplayPanel::ZoneRow r;
            r.rootNote     = z->rootNote;
            r.lowKey       = z->lowKey;
            r.highKey      = z->highKey;
            r.lowVel       = z->lowVel;
            r.highVel      = z->highVel;
            r.tune         = z->tune;
            r.volume       = z->volume;
            r.pan          = z->pan;
            r.loop         = z->loop;
            r.sampleFrames = z->sampleFrames;
            // filename: derived in the auto-sampler / drop
            // path (commit 3) — empty for now.
            rows.push_back(r);
        }
        msPanel->setZones(std::move(rows));
    };
    // Width-change wiring (deferred to AFTER the GroupedKnobBody
    // is constructed by the dispatcher; the body* only exists
    // then, so the actual setOnPreferredWidthChanged() call
    // lives there — we just stash the panel pointer).
    Access::pendingMsPanelWidthHook(panel) = msPanel;
    return out;
}

} // namespace
namespace {

// ── Audio-effect builders ─────────────────────────────────────────

DeviceDisplaySetup buildSplineEQ(const DisplayBuildArgs& a, effects::AudioEffect* fx) {
    DeviceDisplaySetup out;
    // Full-body replacement: the panel IS the EQ editor (the 40
    // params remain settable via automation / preset / MIDI Learn —
    // just not shown as strip knobs). As a CustomDeviceBody the
    // panel controls its own preferredBodyWidth(), so the strip
    // sizes sanely instead of ballooning past 1200 px on knob count.
    auto* disp = new SplineEQDisplayPanel();
    disp->setEQ(static_cast<effects::SplineEQ*>(fx));
    disp->setSampleRate(static_cast<double>(Access::clipSampleRate(a.panel)));
    const auto setParam = a.setParam;
    disp->setOnParamChange([setParam](int idx, float v) { setParam(idx, v); });
    out.customBody = disp;
    return out;
}

DeviceDisplaySetup buildConvReverb(const DisplayBuildArgs& a, effects::AudioEffect* fx) {
    DeviceDisplaySetup out;
    // Same CustomDeviceBody pattern as the Spline EQ — the
    // panel replaces the per-param knob list. Buttons drive
    // an App-side IR loader (file dialog + libsndfile decode
    // + push to effect via loadIRMono). Per-frame updater
    // keeps the panel's filename / waveform thumbnail in
    // sync with the underlying effect.
    auto* disp = new ConvReverbDisplayPanel();
    auto* cr   = static_cast<effects::ConvolutionReverb*>(fx);
    auto& panel = a.panel;
    const auto setParam = a.setParam;
    disp->setOnParamChange([setParam](int idx, float v) { setParam(idx, v); });
    disp->setOnLoadRequest([&panel, cr]() {
        if (Access::onLoadConvIR(panel)) Access::onLoadConvIR(panel)(cr);
    });
    disp->setOnClearRequest([cr]() { cr->clearIR(); });
    out.customBody = disp;
    out.updater = [disp, cr]() {
        // Pull filename from the effect's stored path
        // (just the basename, not the full path — fits the
        // panel better).
        std::string p = cr->irPath();
        std::string base = p;
        const auto slash = p.find_last_of("/\\");
        if (slash != std::string::npos) base = p.substr(slash + 1);
        disp->setIRName(base);
        disp->setIRWaveform(cr->irData(), cr->irDataLength());
    };
    return out;
}

DeviceDisplaySetup buildNeuralAmp(const DisplayBuildArgs& a, effects::AudioEffect* fx) {
    DeviceDisplaySetup out;
    // The model panel (filename / Load / Clear / Lite) lives on
    // the left as a GroupedKnobBody display widget; the built-in
    // amp-strip knobs (gate / drive / tone / output) group to its
    // right, so the device renders a full amp channel. The Load
    // button drives an App-side handler that opens an SDL .nam
    // dialog and calls effect->setModelPath (NAM load + Reset +
    // prewarm).
    auto* disp = new NeuralAmpDisplayPanel();
    auto* na   = static_cast<effects::NeuralAmp*>(fx);
    auto& panel = a.panel;
    disp->setOnLoadRequest([&panel, na]() {
        if (Access::onLoadNamModel(panel)) Access::onLoadNamModel(panel)(na);
    });
    disp->setOnClearRequest([na]() { na->setModelPath(""); });
    disp->setOnToggleLite([na]() { na->setLite(!na->lite()); });

    out.config.display = disp;
    out.config.displayWidth = 190.0f;
    out.config.sections = {
        {"Gate",   {effects::NeuralAmp::kGate}},
        {"Drive",  {effects::NeuralAmp::kInputGain}},
        {"Tone",   {effects::NeuralAmp::kBass,
                    effects::NeuralAmp::kMid,
                    effects::NeuralAmp::kTreble}},
        {"Output", {effects::NeuralAmp::kOutputGain,
                    effects::NeuralAmp::kMix,
                    effects::NeuralAmp::kNormalize}},
    };
    out.updater = [disp, na]() {
        std::string p = na->modelPath();
        std::string base = p;
        const auto slash = p.find_last_of("/\\");
        if (slash != std::string::npos) base = p.substr(slash + 1);
        disp->setModelName(base);
        disp->setLoadedFlag(na->hasModel());
        disp->setErrorText(na->lastLoadError());
        disp->setLiteState(na->lite(), na->isSlimmable());
        disp->setWarningText(
            na->sampleRateMismatch()
                ? ("trained @ " +
                   std::to_string(static_cast<int>(
                       na->expectedSampleRate())) + " Hz")
                : std::string());
    };
    return out;
}

DeviceDisplaySetup buildFilter(const DisplayBuildArgs&, effects::AudioEffect* fx) {
    DeviceDisplaySetup out;
    auto* disp = new FilterDisplayWidget();
    out.customPanel = disp;
    out.customPanelHeight = 52.0f;
    out.customPanelMinW   = 200.0f;
    out.updater = [disp, fx]() {
        auto* flt = static_cast<effects::Filter*>(fx);
        // Cutoff is stored 0..1 → convert to Hz for the display.
        disp->setCutoff(effects::Filter::cutoffNormToHz(
            flt->getParameter(effects::Filter::kCutoff)));
        // Resonance stored 0.1..20 biquad Q → rough 0..1 normalize
        // so the curve bump scales sensibly.
        const float q = flt->getParameter(effects::Filter::kResonance);
        disp->setResonance(std::clamp((q - 0.1f) / 5.0f, 0.0f, 1.0f));
        disp->setFilterType(
            static_cast<int>(flt->getParameter(effects::Filter::kType)));
    };
    return out;
}

// ── MIDI-effect builders ──────────────────────────────────────────

DeviceDisplaySetup buildLFO(const DisplayBuildArgs& a, midi::MidiEffect* fx) {
    DeviceDisplaySetup out;
    auto* lfoDisp = new LFODisplayWidget();
    out.customPanel = lfoDisp;
    out.customPanelHeight = 52.0f;
    out.customPanelMinW   = 200.0f;

    auto& panel = a.panel;
    // Clicking the display opens the named target picker (App
    // builds + shows it — it owns the engine/visual enumeration).
    const int track = Access::autoTrackIndex(panel);
    const int chain = a.chainIndex;
    lfoDisp->setOnClick([&panel, track, chain](float sx, float sy) {
        if (Access::onLfoTargetMenu(panel))
            Access::onLfoTargetMenu(panel)(track, chain, sx, sy);
    });

    out.updater = [&panel, lfoDisp, fx, track, chain]() {
        auto* lfo = static_cast<midi::LFO*>(fx);
        lfoDisp->setShape(static_cast<int>(lfo->getParameter(midi::LFO::kShape)));
        lfoDisp->setDepth(lfo->getParameter(midi::LFO::kDepth));
        lfoDisp->setBias(lfo->getParameter(midi::LFO::kBias));
        lfoDisp->setPhaseOffset(lfo->getParameter(midi::LFO::kPhase));
        lfoDisp->setCurrentValue(lfo->currentValue());
        lfoDisp->setCurrentPhase(lfo->currentPhase());
        lfoDisp->setLinked(lfo->linkTargetId() != 0);
        if (Access::lfoTargetNameResolver(panel))
            lfoDisp->setTargetLabel(Access::lfoTargetNameResolver(panel)(track, chain));
    };
    return out;
}

// ── Registry tables ───────────────────────────────────────────────

struct InstrumentRow { const char* name; InstrumentDisplayBuilder build; };
constexpr InstrumentRow kInstrumentRows[] = {
    {"FM Synth",          buildFMSynth},
    {"Subtractive Synth", buildSubSynth},
    {"Sampler",           buildSampler},
    {"DrumSlop",          buildDrumSlop},
    {"Wavetable Synth",   buildWavetable},
    {"Granular Synth",    buildGranular},
    {"Vocoder",           buildVocoder},
    {"Drum Synth",        buildDrumSynth},
    {"Drum Rack",         buildDrumRack},
    {"Instrument Rack",   buildInstrumentRack},
    {"Multisampler",      buildMultisampler},
};

struct AudioFxRow { const char* id; AudioFxDisplayBuilder build; };
constexpr AudioFxRow kAudioFxRows[] = {
    {"splineeq",   buildSplineEQ},
    {"convreverb", buildConvReverb},
    {"neuralamp",  buildNeuralAmp},
    {"filter",     buildFilter},
};

struct MidiFxRow { const char* id; MidiFxDisplayBuilder build; };
constexpr MidiFxRow kMidiFxRows[] = {
    {"lfo", buildLFO},
};

} // namespace

InstrumentDisplayBuilder findInstrumentDisplayBuilder(const std::string& name) {
    for (const auto& row : kInstrumentRows)
        if (name == row.name) return row.build;
    return nullptr;
}

AudioFxDisplayBuilder findAudioFxDisplayBuilder(const std::string& id) {
    for (const auto& row : kAudioFxRows)
        if (id == row.id) return row.build;
    return nullptr;
}

MidiFxDisplayBuilder findMidiFxDisplayBuilder(const std::string& id) {
    for (const auto& row : kMidiFxRows)
        if (id == row.id) return row.build;
    return nullptr;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
