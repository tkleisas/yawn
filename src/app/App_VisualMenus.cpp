// App_VisualMenus.cpp — shader-library, macro-mapping, visual-knob-LFO,
// LFO-target, scene and arrangement-clip context menus.
// Split out of App.cpp.
#include "app/App.h"
#include "Version.h"
#include "visual/LiveInputEnum.h"
#include "visual/VisualModBus.h"
#include "transcribe/AudioToMidi.h"
#include "ui/framework/v2/Fw2Painters.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/Dialog.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Theme.h"
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
#include "effects/Reverb.h"
#include "effects/Delay.h"
#include "effects/EQ.h"
#include "effects/Compressor.h"
#include "effects/Limiter.h"
#include "effects/Filter.h"
#include "effects/Chorus.h"
#include "effects/Phaser.h"
#include "effects/Wah.h"
#include "effects/Distortion.h"
#include "effects/TapeEmulation.h"
#include "effects/AmpSimulator.h"
#include "effects/Oscilloscope.h"
#include "effects/SpectrumAnalyzer.h"
#include "effects/Tuner.h"
#include "effects/BeatRepeat.h"
#include "effects/BufferRepeat.h"
#include "effects/Resampler.h"
#include "effects/ClockDrift.h"
#include "effects/CDError.h"
#include "effects/AutoPanner.h"
#include "midi/Arpeggiator.h"
#include "midi/Chord.h"
#include "midi/Scale.h"
#include "midi/NoteLength.h"
#include "midi/VelocityEffect.h"
#include "midi/MidiRandom.h"
#include "midi/MidiPitch.h"
#include "midi/LFO.h"
#include "util/ProjectSerializer.h"
#include "util/Logger.h"
#include "util/FileIO.h"
#include "util/MidiFileIO.h"
#include "audio/OfflineRenderer.h"
#include "presets/PresetGenerator.h"
#include "presets/MidiLoopManager.h"
#include "presets/DrumPatterns.h"
#include "presets/MelodicPatterns.h"
#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <cinttypes>
#include <cstring>
#include <thread>

namespace yawn {

void App::showShaderLibraryMenu(float mx, float my) {
    // Refresh on every open so newly imported shaders appear without
    // restarting the app. Project root drives <project>/shaders/.
    if (!m_projectPath.empty())
        m_shaderLibrary.setProjectShadersDir(m_projectPath / "shaders");
    else
        m_shaderLibrary.setProjectShadersDir({});
    m_shaderLibrary.refresh();

    const auto& entries = m_shaderLibrary.entries();
    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    std::vector<MenuEntry> items;

    // The "+ Add Pass" menu only adds *effects* — passes that sample
    // iPrev and process the previous output. "Sources" (standalone
    // generators that ignore iPrev) would blank the chain if appended,
    // so we filter them out here. Project shaders are kept (the user
    // wrote them; they presumably know the convention they used).
    auto isEffectish = [](const std::string& cat) {
        return cat == "Effects" || cat == "Project";
    };
    bool anyEffect = false;
    for (const auto& e : entries) if (isEffectish(e.category)) { anyEffect = true; break; }

    if (entries.empty() || !anyEffect) {
        items.push_back(header("(no chainable effects found)"));
    } else {
        // Group by category — Project then Effects, in that order.
        // Insert separator + heading rows so the menu reads like a
        // categorised picker.
        std::string lastCat;
        for (const auto& e : entries) {
            if (!isEffectish(e.category)) continue;
            if (e.category != lastCat) {
                if (!lastCat.empty())
                    items.push_back(separator());
                items.push_back(header("── " + e.category + " ──"));
                lastCat = e.category;
            }
            std::string path = e.absolutePath;
            items.push_back(item(e.label, [this, path]() {
                if (m_selectedTrack < 0 ||
                    m_selectedTrack >= m_project.numTracks()) return;
                auto& trk = m_project.track(m_selectedTrack);
                visual::ShaderPass pass;
                // Store project-relative path when possible so projects
                // remain portable; fall back to absolute otherwise.
                pass.shaderPath = path;
                if (!m_projectPath.empty()) {
                    std::error_code ec;
                    auto rel = std::filesystem::relative(path, m_projectPath, ec);
                    if (!ec && !rel.empty() && rel.string().find("..") == std::string::npos)
                        pass.shaderPath = rel.generic_string();
                }
                trk.visualEffectChain.push_back(std::move(pass));
                // Re-launch the active clip so the engine picks up
                // the new chain entry. If no clip is launched on this
                // track yet, the chain just sits ready and applies
                // the next time one launches.
                int sc = (trk.defaultScene >= 0) ? trk.defaultScene
                                                   : m_selectedScene;
                auto* slot = m_project.getSlot(m_selectedTrack, sc);
                if (slot && slot->visualClip) {
                    launchVisualClipData(m_selectedTrack, *slot->visualClip,
                                          slot->visualClip->firstShaderPath());
                }
                markDirty();
                updateDetailForSelectedTrack();
            }));
        }
    }

    ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
}

void App::showMacroMappingMenu(const MacroTarget& target, float mx, float my) {
    if (m_selectedTrack < 0 ||
        m_selectedTrack >= m_project.numTracks()) return;
    auto& macros = m_project.track(m_selectedTrack).macros;

    // Find current mapping (if any) for this exact target. Targets
    // match on (kind, index, paramName); a parameter can only be
    // mapped to one macro at a time — picking a new macro replaces.
    auto matches = [&](const MacroMapping& m) {
        return m.target.kind      == target.kind  &&
               m.target.index     == target.index &&
               m.target.paramName == target.paramName;
    };
    int   currentMacro = -1;
    float currentMin   = 0.0f;
    float currentMax   = 1.0f;
    for (size_t i = 0; i < macros.mappings.size(); ++i) {
        if (matches(macros.mappings[i])) {
            currentMacro = macros.mappings[i].macroIdx;
            currentMin   = macros.mappings[i].rangeMin;
            currentMax   = macros.mappings[i].rangeMax;
            break;
        }
    }

    // Helper: locate the live mapping in the chain (or nullptr) so
    // the range-edit lambdas don't re-search every fire.
    auto findMapping = [this, target]() -> MacroMapping* {
        if (m_selectedTrack < 0 ||
            m_selectedTrack >= m_project.numTracks()) return nullptr;
        auto& chain = m_project.track(m_selectedTrack).macros.mappings;
        for (auto& m : chain) {
            if (m.target.kind      == target.kind  &&
                m.target.index     == target.index &&
                m.target.paramName == target.paramName) {
                return &m;
            }
        }
        return nullptr;
    };

    auto setMapping = [this, target, currentMin, currentMax](int macroIdx) {
        if (m_selectedTrack < 0 ||
            m_selectedTrack >= m_project.numTracks()) return;
        auto& chain = m_project.track(m_selectedTrack).macros.mappings;
        // Replace any existing mapping for this exact target before
        // adding the new one — one macro per parameter. We carry the
        // existing range across so re-routing to a different macro
        // doesn't silently widen the user's carefully-set sub-range.
        chain.erase(std::remove_if(chain.begin(), chain.end(),
            [&](const MacroMapping& m) {
                return m.target.kind      == target.kind  &&
                       m.target.index     == target.index &&
                       m.target.paramName == target.paramName;
            }), chain.end());
        MacroMapping nm;
        nm.macroIdx = macroIdx;
        nm.target   = target;
        nm.rangeMin = currentMin;
        nm.rangeMax = currentMax;
        chain.push_back(std::move(nm));
        markDirty();
    };

    auto unmap = [this, target]() {
        if (m_selectedTrack < 0 ||
            m_selectedTrack >= m_project.numTracks()) return;
        auto& chain = m_project.track(m_selectedTrack).macros.mappings;
        chain.erase(std::remove_if(chain.begin(), chain.end(),
            [&](const MacroMapping& m) {
                return m.target.kind      == target.kind  &&
                       m.target.index     == target.index &&
                       m.target.paramName == target.paramName;
            }), chain.end());
        markDirty();
    };

    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    auto itemEn = [](std::string label, std::function<void()> action, bool enabled) {
        MenuEntry e = item(std::move(label), std::move(action));
        e.enabled = enabled;
        return e;
    };
    std::vector<MenuEntry> items;
    // Header row — disabled, just shows what we're mapping.
    {
        std::string title = "Map '" + target.paramName + "' to:";
        items.push_back(header(title));
        items.push_back(separator());
    }

    // 8 macros — show "Macro N" + custom label if any. Currently
    // mapped macro carries a "✓" prefix (cheap visual hint pre-4.2
    // proper card UI).
    for (int i = 0; i < MacroDevice::kNumMacros; ++i) {
        std::string label = (i == currentMacro ? "✓ Macro " : "   Macro ")
                             + std::to_string(i + 1);
        if (!macros.labels[i].empty())
            label += ": " + macros.labels[i];
        items.push_back(item(label, [setMapping, i]() { setMapping(i); }));
    }

    if (currentMacro >= 0) {
        items.push_back(separator());

        // Sub-range section. Header shows the live range (lets the
        // user verify in one glance without typing). The five action
        // items below cover the common edits — typed bounds for
        // precision, presets for speed, invert as a one-click flip.
        {
            char rangeLabel[64];
            std::snprintf(rangeLabel, sizeof(rangeLabel),
                           "Range: %.2f .. %.2f", currentMin, currentMax);
            items.push_back(header(rangeLabel));
        }

        items.push_back(item("Set min…", [this, findMapping]() {
            auto* m = findMapping();
            if (!m) return;
            char def[16];
            std::snprintf(def, sizeof(def), "%.3f", m->rangeMin);
            m_textInputDialog.prompt("Range min", def,
                [this, findMapping](const std::string& text) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                    auto* mm = findMapping();
                    if (!mm) return;
                    try { mm->rangeMin = std::stof(text); }
                    catch (...) { return; }
                    markDirty();
                });
        }));

        items.push_back(item("Set max…", [this, findMapping]() {
            auto* m = findMapping();
            if (!m) return;
            char def[16];
            std::snprintf(def, sizeof(def), "%.3f", m->rangeMax);
            m_textInputDialog.prompt("Range max", def,
                [this, findMapping](const std::string& text) {
                    SDL_StopTextInput(m_mainWindow.getHandle());
                    auto* mm = findMapping();
                    if (!mm) return;
                    try { mm->rangeMax = std::stof(text); }
                    catch (...) { return; }
                    markDirty();
                });
        }));

        items.push_back(itemEn("Invert range",
            [this, findMapping]() {
                auto* m = findMapping();
                if (!m) return;
                std::swap(m->rangeMin, m->rangeMax);
                markDirty();
            },
            // Only meaningful when min != max — disable on a
            // pinched mapping so a stray click can't no-op visibly.
            std::abs(currentMax - currentMin) > 1e-6f));

        items.push_back(itemEn("Reset range (0..1)",
            [this, findMapping]() {
                auto* m = findMapping();
                if (!m) return;
                m->rangeMin = 0.0f;
                m->rangeMax = 1.0f;
                markDirty();
            },
            // Disabled when already at the default — same logic the
            // LFO menu uses for its preset checkmarks.
            std::abs(currentMin) > 1e-6f ||
            std::abs(currentMax - 1.0f) > 1e-6f));

        items.push_back(separator());
        items.push_back(item("Unmap", [unmap]() { unmap(); }));
    }

    ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
}

void App::showVisualKnobLFOMenu(int knobIdx, float mx, float my) {
    if (knobIdx < 0 || knobIdx >= 8) return;
    if (m_selectedTrack < 0 || m_selectedTrack >= m_project.numTracks()) return;
    if (m_project.track(m_selectedTrack).type != Track::Type::Visual) return;

    // Pull current LFO state — prefer the engine's live copy when a
    // layer exists (already kept in sync with the macro device on
    // every launch), otherwise fall back to the track's macros.lfos
    // directly. The fallback path is what makes the menu open even
    // when no clip has been launched on this track yet — useful for
    // configuring a tempo-synced LFO before pressing play.
    visual::VisualLFO snapshot;
    if (const visual::VisualLFO* live =
            m_visualEngine.getLayerKnobLFO(m_selectedTrack, knobIdx)) {
        snapshot = *live;
    } else {
        const auto& s =
            m_project.track(m_selectedTrack).macros.lfos[knobIdx];
        snapshot.enabled = s.enabled;
        snapshot.shape   = static_cast<visual::VisualLFO::Shape>(s.shape);
        snapshot.rate    = s.rate;
        snapshot.depth   = s.depth;
        snapshot.sync    = s.sync;
    }
    // `cur` is kept as a const ref to `snapshot` so the rest of this
    // function (which inspects `cur->...` to mark menu items checked)
    // stays unchanged whether we sourced from the engine or the
    // project model.
    const visual::VisualLFO* cur = &snapshot;

    auto apply = [this, knobIdx](visual::VisualLFO lfo) {
        m_visualEngine.setLayerKnobLFO(m_selectedTrack, knobIdx, lfo);
        // Mirror into the track-level macro device for persistence.
        // (Phase 4.1: LFO state moved off VisualClip — modulators
        // belong with the macro values they drive.)
        if (m_selectedTrack >= 0 &&
            m_selectedTrack < m_project.numTracks() &&
            knobIdx >= 0 && knobIdx < MacroDevice::kNumMacros) {
            auto& saved = m_project.track(m_selectedTrack).macros.lfos[knobIdx];
            saved.enabled = lfo.enabled;
            saved.shape   = static_cast<uint8_t>(lfo.shape);
            saved.rate    = lfo.rate;
            saved.depth   = lfo.depth;
            saved.sync    = lfo.sync;
            markDirty();
        }
    };

    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    auto itemEn = [](std::string label, std::function<void()> action, bool enabled) {
        MenuEntry e = item(std::move(label), std::move(action));
        e.enabled = enabled;
        return e;
    };
    std::vector<MenuEntry> items;
    char title[32];
    std::snprintf(title, sizeof(title), "Knob %c",
                   static_cast<char>('A' + knobIdx));
    items.push_back(header(title));
    items.push_back(separator());

    // MIDI Learn / Remove Mapping — routes a MIDI CC to this knob.
    {
        auto tgt = automation::AutomationTarget::visualKnob(m_selectedTrack, knobIdx);
        bool mapped = false;
        std::string mappedLabel;
        for (const auto& mm : m_midiLearnManager.mappings()) {
            if (mm.target == tgt) { mapped = true; mappedLabel = mm.label(); break; }
        }
        const bool learning = m_midiLearnManager.isLearning() &&
                              m_midiLearnManager.learnTarget() == tgt;
        if (learning) {
            items.push_back(item("Cancel MIDI Learn", [this]() {
                m_midiLearnManager.cancelLearn();
            }));
        } else if (mapped) {
            std::string label = "MIDI: " + mappedLabel + "  (click to re-learn)";
            items.push_back(item(label, [this, tgt]() {
                m_midiLearnManager.startLearn(tgt, 0.0f, 1.0f);
            }));
            items.push_back(item("Remove MIDI Mapping", [this, tgt]() {
                m_midiLearnManager.removeByTarget(tgt);
            }));
        } else {
            items.push_back(item("MIDI Learn…", [this, tgt]() {
                m_midiLearnManager.startLearn(tgt, 0.0f, 1.0f);
            }));
        }
        items.push_back(separator());
    }

    items.push_back(header("-- LFO --"));

    // On/off toggle — shown checked when disabled so the active state stays.
    items.push_back(item(cur->enabled ? "Disable LFO" : "Enable LFO",
        [apply, snapshot]() mutable {
            snapshot.enabled = !snapshot.enabled;
            apply(snapshot);
        }));

    items.push_back(separator());

    // Shape submenu.
    std::vector<MenuEntry> shapeItems;
    const char* shapeNames[] = {"Sine", "Triangle", "Saw", "Square", "Sample & Hold"};
    for (int s = 0; s < 5; ++s) {
        shapeItems.push_back(itemEn(shapeNames[s],
            [apply, snapshot, s]() mutable {
                snapshot.shape = static_cast<visual::VisualLFO::Shape>(s);
                snapshot.enabled = true;
                apply(snapshot);
            }, static_cast<int>(cur->shape) != s));
    }
    items.push_back(submenu("Shape", std::move(shapeItems)));

    // Rate submenu (beats per cycle).
    std::vector<MenuEntry> rateItems;
    struct RatePreset { const char* label; float beats; };
    static const RatePreset rates[] = {
        {"1/16",   0.25f}, {"1/8",   0.5f}, {"1/4",   1.0f}, {"1/2",   2.0f},
        {"1 bar",  4.0f},  {"2 bars",8.0f}, {"4 bars", 16.0f},
    };
    for (const auto& rp : rates) {
        rateItems.push_back(itemEn(rp.label,
            [apply, snapshot, rp]() mutable {
                snapshot.rate = rp.beats;
                snapshot.sync = true;
                snapshot.enabled = true;
                apply(snapshot);
            }, std::abs(cur->rate - rp.beats) > 0.001f));
    }
    rateItems.push_back(separator());
    rateItems.push_back(item("Custom…", [this, apply, snapshot]() mutable {
        promptCustomFloat("LFO Rate (beats per cycle)", snapshot.rate,
            0.01f, 64.0f, 1.0f,
            [apply, snapshot](float v) mutable {
                snapshot.rate = v; snapshot.sync = true; snapshot.enabled = true;
                apply(snapshot);
            });
    }));
    items.push_back(submenu("Rate", std::move(rateItems)));

    // Depth submenu.
    std::vector<MenuEntry> depthItems;
    static const float depths[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f};
    static const char* depthLabels[] = {"10%", "25%", "50%", "75%", "100%"};
    for (int d = 0; d < 5; ++d) {
        float dv = depths[d];
        depthItems.push_back(itemEn(depthLabels[d],
            [apply, snapshot, dv]() mutable {
                snapshot.depth = dv;
                snapshot.enabled = true;
                apply(snapshot);
            }, std::abs(cur->depth - dv) > 0.001f));
    }
    depthItems.push_back(separator());
    depthItems.push_back(item("Custom…", [this, apply, snapshot]() mutable {
        promptCustomFloat("LFO Depth (%)", snapshot.depth, 0.0f, 1.0f, 0.01f,
            [apply, snapshot](float v) mutable {
                snapshot.depth = v; snapshot.enabled = true;
                apply(snapshot);
            });
    }));
    items.push_back(submenu("Depth", std::move(depthItems)));

    ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
}

midi::LFO* App::lfoAt(int track, int chainSlot) {
    if (track < 0 || track >= kMaxTracks) return nullptr;
    auto& chain = m_audioEngine.midiEffectChain(track);
    auto* fx = chain.effect(chainSlot);
    if (!fx || std::string(fx->id()) != "lfo") return nullptr;
    return static_cast<midi::LFO*>(fx);
}

std::string App::lfoTargetName(int track, int chainSlot) {
    midi::LFO* lfo = lfoAt(track, chainSlot);
    if (!lfo) return "";
    const int type  = lfo->modulationTargetType();
    const int chain = lfo->modulationTargetChain();
    const int param = lfo->modulationTargetParam();

    auto pname = [](auto* dev, int p) -> std::string {
        if (!dev || p < 0 || p >= dev->parameterCount()) return "?";
        const char* n = dev->parameterInfo(p).name;
        return n ? n : "?";
    };

    switch (type) {
    case midi::LFO::TgtInstrument: {
        auto* inst = m_audioEngine.instrument(track);
        if (!inst) return "Instrument";
        return std::string(inst->name()) + ": " + pname(inst, param);
    }
    case midi::LFO::TgtAudioEffect: {
        auto* afx = m_audioEngine.mixer().trackEffects(track).effectAt(chain);
        if (!afx) return "Audio FX";
        return "FX" + std::to_string(chain + 1) + ": " + pname(afx, param);
    }
    case midi::LFO::TgtMidiEffect: {
        auto* mfx = m_audioEngine.midiEffectChain(track).effect(chain);
        if (!mfx) return "MIDI FX";
        return "MIDI" + std::to_string(chain + 1) + ": " + pname(mfx, param);
    }
    case midi::LFO::TgtMixer:
        if (param == 0) return "Mixer: Volume";
        if (param == 1) return "Mixer: Pan";
        if (param >= 3) return "Mixer: Send " + std::to_string(param - 2);
        return "Mixer";
    case midi::LFO::TgtVisualKnob: {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "Vis T%d: Knob %c",
                      lfo->visualTrack() + 1, static_cast<char>('A' + (param & 7)));
        return buf;
    }
    case midi::LFO::TgtVisualParam:
        return "Vis T" + std::to_string(lfo->visualTrack() + 1) + ": " +
               lfo->targetName();
    }
    return "";
}

void App::showLfoTargetMenu(int track, int chainSlot, float mx, float my) {
    if (!lfoAt(track, chainSlot)) return;

    // Resolve the LFO fresh inside the action (the device tree may rebuild
    // between menu-open and click) and write the target fields. These are
    // scalar UI-thread writes; the audio thread only reads them.
    auto setTarget = [this, track, chainSlot](int type, int chainIdx, int param,
                                              int vtrack, std::string name) {
        midi::LFO* l = lfoAt(track, chainSlot);
        if (!l) return;
        l->setParameter(midi::LFO::kTargetType,       static_cast<float>(type));
        l->setParameter(midi::LFO::kTargetChainIndex, static_cast<float>(chainIdx));
        l->setParameter(midi::LFO::kTargetParamIndex, static_cast<float>(param));
        l->setVisualTrack(vtrack);
        l->setTargetName(std::move(name));
        markDirty();
    };

    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    std::vector<MenuEntry> items;
    items.push_back(header("LFO Target"));
    items.push_back(separator());
    items.push_back(item("None", [setTarget]() {
        setTarget(midi::LFO::TgtInstrument, 0, 0, -1, std::string());
    }));

    // Instrument params (same track).
    if (auto* inst = m_audioEngine.instrument(track)) {
        std::vector<MenuEntry> sub;
        for (int p = 0; p < inst->parameterCount(); ++p) {
            const auto& info = inst->parameterInfo(p);
            if (info.isPerVoice) continue;
            sub.push_back(item(info.name ? info.name : "?", [setTarget, p]() {
                setTarget(midi::LFO::TgtInstrument, 0, p, -1, std::string());
            }));
        }
        if (!sub.empty())
            items.push_back(submenu(std::string("Instrument: ") + inst->name(),
                                    std::move(sub)));
    }

    // Audio FX (same track).
    {
        auto& fxchain = m_audioEngine.mixer().trackEffects(track);
        for (int slot = 0; slot < effects::kMaxEffectsPerChain; ++slot) {
            auto* afx = fxchain.effectAt(slot);
            if (!afx) continue;
            std::vector<MenuEntry> sub;
            for (int p = 0; p < afx->parameterCount(); ++p) {
                const auto& info = afx->parameterInfo(p);
                sub.push_back(item(info.name ? info.name : "?", [setTarget, slot, p]() {
                    setTarget(midi::LFO::TgtAudioEffect, slot, p, -1, std::string());
                }));
            }
            if (!sub.empty()) {
                char lbl[72];
                std::snprintf(lbl, sizeof(lbl), "FX%d: %s", slot + 1, afx->name());
                items.push_back(submenu(lbl, std::move(sub)));
            }
        }
    }

    // MIDI FX (same track, excluding this LFO's own slot).
    {
        auto& mchain = m_audioEngine.midiEffectChain(track);
        for (int slot = 0; slot < mchain.count(); ++slot) {
            if (slot == chainSlot) continue;
            auto* mfx = mchain.effect(slot);
            if (!mfx) continue;
            std::vector<MenuEntry> sub;
            for (int p = 0; p < mfx->parameterCount(); ++p) {
                const auto& info = mfx->parameterInfo(p);
                if (info.widgetHint == WidgetHint::Hidden) continue;
                sub.push_back(item(info.name ? info.name : "?", [setTarget, slot, p]() {
                    setTarget(midi::LFO::TgtMidiEffect, slot, p, -1, std::string());
                }));
            }
            if (!sub.empty()) {
                char lbl[72];
                std::snprintf(lbl, sizeof(lbl), "MIDI%d: %s", slot + 1, mfx->name());
                items.push_back(submenu(lbl, std::move(sub)));
            }
        }
    }

    // Mixer (same track).
    {
        std::vector<MenuEntry> sub;
        sub.push_back(item("Volume", [setTarget]() {
            setTarget(midi::LFO::TgtMixer, 0, 0, -1, std::string()); }));
        sub.push_back(item("Pan", [setTarget]() {
            setTarget(midi::LFO::TgtMixer, 0, 1, -1, std::string()); }));
        for (int s = 0; s < kMaxSendsPerTrack; ++s) {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "Send %d", s + 1);
            sub.push_back(item(lbl, [setTarget, s]() {
                setTarget(midi::LFO::TgtMixer, 0, 3 + s, -1, std::string());
            }));
        }
        items.push_back(submenu("Mixer", std::move(sub)));
    }

    // Visual layers (any visual track) — knobs A..H + shader @range params.
    {
        std::vector<MenuEntry> visSub;
        for (int vt = 0; vt < m_project.numTracks(); ++vt) {
            if (m_project.track(vt).type != Track::Type::Visual) continue;
            std::vector<MenuEntry> layerSub;
            for (int k = 0; k < 8; ++k) {
                char lbl[12];
                std::snprintf(lbl, sizeof(lbl), "Knob %c",
                              static_cast<char>('A' + k));
                layerSub.push_back(item(lbl, [setTarget, vt, k]() {
                    setTarget(midi::LFO::TgtVisualKnob, 0, k, vt, std::string());
                }));
            }
            for (const auto& pinfo : m_visualEngine.getLayerParams(vt)) {
                std::string pn = pinfo.name;
                layerSub.push_back(item(pn, [setTarget, vt, pn]() {
                    setTarget(midi::LFO::TgtVisualParam, 0, 0, vt, pn);
                }));
            }
            char lbl[48];
            std::snprintf(lbl, sizeof(lbl), "Track %d: %s", vt + 1,
                          m_project.track(vt).name.c_str());
            visSub.push_back(submenu(lbl, std::move(layerSub)));
        }
        if (!visSub.empty())
            items.push_back(submenu("Visual", std::move(visSub)));
    }

    ui::fw2::ContextMenu::show(std::move(items), ui::fw2::Point{mx, my});
}

void App::showSceneContextMenu(int sceneIndex, float mx, float my) {
    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    auto itemEn = [](std::string label, std::function<void()> action, bool enabled) {
        MenuEntry e = item(std::move(label), std::move(action));
        e.enabled = enabled;
        return e;
    };
    std::vector<MenuEntry> items;
    bool canDelete = m_project.numScenes() > 1;

    items.push_back(item("Insert Scene", [this, sceneIndex]() {
        sceneInsert(sceneIndex);
        markDirty();
        m_undoManager.push({"Insert Scene",
            [this, sceneIndex]{
                sceneDelete(sceneIndex);
                markDirty();
            },
            [this, sceneIndex]{
                sceneInsert(sceneIndex);
                markDirty();
            }, ""});
    }));

    items.push_back(item("Duplicate Scene", [this, sceneIndex]() {
        sceneDuplicate(sceneIndex);
        markDirty();
        int dst = sceneIndex + 1;
        m_undoManager.push({"Duplicate Scene",
            [this, dst]{
                sceneDelete(dst);
                markDirty();
            },
            [this, sceneIndex]{
                sceneDuplicate(sceneIndex);
                markDirty();
            }, ""});
    }));

    items.push_back(itemEn("Delete Scene", [this, sceneIndex]() {
        // Capture all clip data for undo
        struct SlotBackup {
            std::unique_ptr<audio::Clip> audioClip;
            std::unique_ptr<midi::MidiClip> midiClip;
            audio::QuantizeMode launchQuantize;
            FollowAction followAction;
            std::vector<automation::AutomationLane> clipAutomation;
        };
        auto sceneName = m_project.scene(sceneIndex).name;
        std::vector<SlotBackup> backups;
        for (int t = 0; t < m_project.numTracks(); ++t) {
            auto* slot = m_project.getSlot(t, sceneIndex);
            SlotBackup b;
            if (slot) {
                if (slot->audioClip) b.audioClip = slot->audioClip->clone();
                if (slot->midiClip)  b.midiClip  = slot->midiClip->clone();
                b.launchQuantize = slot->launchQuantize;
                b.followAction   = slot->followAction;
                b.clipAutomation = slot->clipAutomation;
            }
            backups.push_back(std::move(b));
        }
        // sceneDelete() stops every launched clip first so the audio
        // thread releases its cached clip-automation pointers before the
        // clip-slot vectors reallocate.
        sceneDelete(sceneIndex);
        markDirty();
        auto shared = std::make_shared<std::vector<SlotBackup>>(std::move(backups));
        m_undoManager.push({"Delete Scene",
            [this, sceneIndex, sceneName, shared]{
                sceneInsert(sceneIndex);
                m_project.scene(sceneIndex).name = sceneName;
                for (int t = 0; t < m_project.numTracks() && t < static_cast<int>(shared->size()); ++t) {
                    auto* slot = m_project.getSlot(t, sceneIndex);
                    auto& b = (*shared)[t];
                    if (slot) {
                        if (b.audioClip) slot->audioClip = b.audioClip->clone();
                        if (b.midiClip)  slot->midiClip  = b.midiClip->clone();
                        slot->launchQuantize = b.launchQuantize;
                        slot->followAction   = b.followAction;
                        slot->clipAutomation = b.clipAutomation;
                    }
                }
                markDirty();
            },
            [this, sceneIndex]{
                sceneDelete(sceneIndex);
                markDirty();
            }, ""});
    }, canDelete));

    ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
}

void App::showArrangementClipContextMenu(int trackIndex, int clipIdx,
                                         float mx, float my) {
    if (trackIndex < 0 || trackIndex >= m_project.numTracks()) return;
    auto& clips = m_project.track(trackIndex).arrangementClips;
    if (clipIdx < 0 || clipIdx >= static_cast<int>(clips.size())) return;

    using namespace ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    auto itemEn = [](std::string label, std::function<void()> action, bool enabled) {
        MenuEntry e = item(std::move(label), std::move(action));
        e.enabled = enabled;
        return e;
    };
    std::vector<MenuEntry> items;

    items.push_back(item("Copy", [this, trackIndex, clipIdx]() {
        auto& cs = m_project.track(trackIndex).arrangementClips;
        if (clipIdx >= 0 && clipIdx < static_cast<int>(cs.size())) {
            m_arrangementClipboard = cs[clipIdx];
            m_arrangementClipboardValid = true;
        }
    }));

    items.push_back(item("Cut", [this, trackIndex, clipIdx]() {
        auto& cs = m_project.track(trackIndex).arrangementClips;
        if (clipIdx < 0 || clipIdx >= static_cast<int>(cs.size())) return;
        m_arrangementClipboard      = cs[clipIdx];
        m_arrangementClipboardValid = true;
        // Reuse panel's delete path so selection + undo stay consistent.
        m_arrangementPanel->setSelectedClip(trackIndex, clipIdx);
        m_arrangementPanel->handleAppKey(SDLK_DELETE, /*ctrl=*/false);
    }));

    // Paste: enabled only if the clipboard holds something compatible
    // with this track's type (audio→Audio, midi→Midi, visual→Visual).
    const auto trkType  = m_project.track(trackIndex).type;
    const bool canPaste = m_arrangementClipboardValid && (
        (trkType == Track::Type::Audio  && m_arrangementClipboard.type == ArrangementClip::Type::Audio)  ||
        (trkType == Track::Type::Midi   && m_arrangementClipboard.type == ArrangementClip::Type::Midi)   ||
        (trkType == Track::Type::Visual && m_arrangementClipboard.type == ArrangementClip::Type::Visual));
    items.push_back(itemEn("Paste", [this, trackIndex]() {
        if (!m_arrangementClipboardValid) return;
        ArrangementClip ac = m_arrangementClipboard;
        ac.startBeat = m_audioEngine.transport().positionInBeats();
        m_project.track(trackIndex).arrangementClips.push_back(std::move(ac));
        m_project.track(trackIndex).sortArrangementClips();
        m_project.updateArrangementLength();
        syncArrangementClipsToEngine(trackIndex);
        if (!m_project.track(trackIndex).arrangementActive) {
            m_project.track(trackIndex).arrangementActive = true;
            m_audioEngine.sendCommand(
                audio::SetTrackArrActiveMsg{trackIndex, true});
        }
        markDirty();
    }, canPaste));

    items.push_back(separator());

    items.push_back(item("Duplicate", [this, trackIndex, clipIdx]() {
        m_arrangementPanel->setSelectedClip(trackIndex, clipIdx);
        m_arrangementPanel->handleAppKey(SDLK_D, /*ctrl=*/true);
    }));

    items.push_back(item("Delete", [this, trackIndex, clipIdx]() {
        m_arrangementPanel->setSelectedClip(trackIndex, clipIdx);
        m_arrangementPanel->handleAppKey(SDLK_DELETE, /*ctrl=*/false);
    }));

    // ── Loop / Stretch ──
    // Loop governs the normal edge-drag (extend repeats the content vs.
    // silence). Push the changed clip back to the engine so a playing
    // clip updates live. Applies to every clip type.
    {
        items.push_back(separator());
        auto applyPlayback = [this](int tr, int ci) {
            syncArrangementClipsToEngine(tr);   // audio/MIDI loop
            auto& cs = m_project.track(tr).arrangementClips;
            if (ci >= 0 && ci < static_cast<int>(cs.size()) &&
                cs[ci].type == ArrangementClip::Type::Visual &&
                m_activeArrVisualClip[tr] == ci) {
                const auto& c = cs[ci];
                const int vmode = c.stretch ? 1 : (c.loop ? 0 : 2);
                m_visualEngine.setLayerArrangementVideo(tr, vmode,
                    c.lengthBeats, c.offsetBeats);
            }
        };
        const bool isLooping = clips[clipIdx].loop;
        items.push_back(item(isLooping ? "Loop: On" : "Loop: Off",
            [this, trackIndex, clipIdx, applyPlayback, isLooping]() {
                auto& cs = m_project.track(trackIndex).arrangementClips;
                if (clipIdx < 0 || clipIdx >= static_cast<int>(cs.size())) return;
                cs[clipIdx].loop = !isLooping;
                applyPlayback(trackIndex, clipIdx);
                markDirty();
            }));

        // Stretch to fit — MIDI (scale note times) and video (frame map).
        // Audio stretch (the live time-stretcher) is a later step. Overrides
        // loop: the content fills the slot exactly.
        const bool canStretch =
            clips[clipIdx].type == ArrangementClip::Type::Midi ||
            (clips[clipIdx].type == ArrangementClip::Type::Visual &&
             clips[clipIdx].visualClip &&
             !clips[clipIdx].visualClip->videoPath.empty());
        if (canStretch) {
            const bool isStretch = clips[clipIdx].stretch;
            items.push_back(item(
                isStretch ? "Stretch to fit: On" : "Stretch to fit: Off",
                [this, trackIndex, clipIdx, applyPlayback, isStretch]() {
                    auto& cs = m_project.track(trackIndex).arrangementClips;
                    if (clipIdx < 0 || clipIdx >= static_cast<int>(cs.size())) return;
                    cs[clipIdx].stretch = !isStretch;
                    applyPlayback(trackIndex, clipIdx);
                    markDirty();
                }));
        }
    }

    ui::fw2::ContextMenu::show(std::move(items),
                                 ui::fw::Point{mx, my});
}


} // namespace yawn
