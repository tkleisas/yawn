// App_Generator.cpp — generative-song panel plumbing: Lua engine roots,
// generate preview, and commit to session scenes (with undo).

#include "app/App.h"
#include "generate/SongCommitter.h"
#include "generate/GeneratorParamBus.h"
#include "generate/GeneratorPreset.h"
#include "automation/AutomationTypes.h"
#include "ui/framework/v2/ContextMenu.h"
#include "util/Logger.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace yawn {

namespace {

std::filesystem::path homeDir() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    return home ? std::filesystem::path(home) : std::filesystem::path{};
}

} // namespace

std::vector<std::filesystem::path> App::generatorPresetDirs() const {
    std::vector<std::filesystem::path> dirs;
    if (!m_projectPath.empty())
        dirs.push_back(m_projectPath / "generator" / "presets");
    const auto home = homeDir();
    if (!home.empty())
        dirs.push_back(home / ".yawn" / "generator" / "presets");
    return dirs;
}

std::vector<std::filesystem::path> App::generatorScriptRoots() const {
    std::vector<std::filesystem::path> roots;
    std::error_code ec;

    if (!m_projectPath.empty())
        roots.push_back(m_projectPath / "generator");

    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) roots.push_back(cwd / "generator");

    const auto home = homeDir();
    if (!home.empty()) roots.push_back(home / ".yawn" / "generator");

    if (const char* base = SDL_GetBasePath())
        roots.push_back(std::filesystem::path(base) / "assets" / "generator");
    if (!ec) roots.push_back(cwd / "assets" / "generator");
    return roots;
}

void App::initSongGenerator() {
    m_songGenerator.setScriptRoots(generatorScriptRoots());
    std::string error;
    m_songGeneratorReady = m_songGenerator.reload(&error);
    if (m_songGeneratorReady) {
        LOG_INFO("Generate", "song generator ready: %s",
                 m_songGenerator.scriptsDir().c_str());
    } else {
        LOG_ERROR("Generate", "song generator failed to load: %s",
                  error.c_str());
    }
}

void App::showGeneratorPanel() {
    if (!m_generatorPanel) return;

    // Reload on open so user edits under <project>/generator or
    // ~/.yawn/generator are picked up without restarting.
    m_songGenerator.setScriptRoots(generatorScriptRoots());
    std::string error;
    m_songGeneratorReady = m_songGenerator.reload(&error);
    if (!m_songGeneratorReady) {
        m_toastManager.show("Generator scripts failed: " + error, 4.0f,
                            ui::ToastManager::Severity::Error);
        return;
    }

    std::vector<std::string> trackNames;
    trackNames.reserve(static_cast<size_t>(m_project.numTracks()));
    for (int t = 0; t < m_project.numTracks(); ++t)
        trackNames.push_back(m_project.track(t).name);

    m_generatorPanel->setStyleIds(m_songGenerator.listStyles());
    m_generatorPanel->setScaleIds(m_songGenerator.listScales());
    m_generatorPanel->setTrackNames(trackNames);
    m_generatorPanel->setPresetNames(
        generate::listGeneratorPresets(generatorPresetDirs()));
    if (!m_generatedRoles.empty())
        m_generatorPanel->setGeneratedRoles(m_generatedRoles);
    else
        m_generatorPanel->clearGenerated();

    m_sessionPanel->setVisible(false);
    m_arrangementPanel->setVisible(false);
    m_contentGrid->setTopLeft(m_generatorPanel);
    m_contentGrid->invalidate();
    m_generatorPanel->setVisible(true);
    m_generatorOpen = true;
}

void App::hideGeneratorPanel() {
    if (!m_generatorOpen) return;
    m_generatorOpen = false;
    if (m_generatorPanel) m_generatorPanel->setVisible(false);
    switchToView(m_project.viewMode());
}

void App::onGeneratorGenerate(const generate::GeneratorParams& params) {
    if (!m_songGeneratorReady) {
        m_generatorPanel->setStatus("Generator scripts are not loaded.", true);
        return;
    }

    generate::GeneratedSong song;
    std::string error;
    if (!m_songGenerator.generate(params, song, &error)) {
        m_generatorPanel->setStatus(error, true);
        m_contentGrid->invalidate();
        return;
    }

    m_generatedSong = std::move(song);
    m_generatedRoles = generate::collectRoles(m_generatedSong);

    std::vector<std::string> trackNames;
    trackNames.reserve(static_cast<size_t>(m_project.numTracks()));
    for (int t = 0; t < m_project.numTracks(); ++t)
        trackNames.push_back(m_project.track(t).name);
    m_generatorPanel->setTrackNames(trackNames);
    m_generatorPanel->setGeneratedRoles(m_generatedRoles);

    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "%d sections · %d notes · %d BPM · %d lanes",
                  static_cast<int>(m_generatedSong.sections.size()),
                  m_generatedSong.totalNotes(), m_generatedSong.tempo,
                  static_cast<int>(m_generatedRoles.size()));
    m_generatorPanel->setStatus(buf, false);
    m_contentGrid->invalidate();
}

void App::onGeneratorLoadPreset(const std::string& name) {
    if (!m_generatorPanel) return;
    const auto file = generate::findGeneratorPreset(generatorPresetDirs(), name);
    if (file.empty()) {
        m_generatorPanel->setStatus("Preset not found: " + name, true);
        m_contentGrid->invalidate();
        return;
    }
    generate::GeneratorParams params;
    std::string error;
    if (!generate::loadGeneratorPreset(file, params, &error)) {
        m_generatorPanel->setStatus(error, true);
        m_contentGrid->invalidate();
        return;
    }
    m_generatorPanel->setParams(params);
    m_generatorPanel->setStatus("Preset loaded — press Generate to apply.",
                                false);
    m_contentGrid->invalidate();
}

void App::onGeneratorSavePreset() {
    if (!m_generatorPanel) return;
    const auto home = homeDir();
    if (home.empty()) {
        m_generatorPanel->setStatus("No home directory for presets.", true);
        m_contentGrid->invalidate();
        return;
    }
    m_textInputDialog.prompt("Save Generator Preset", "My Preset",
        [this](const std::string& raw) {
            SDL_StopTextInput(m_mainWindow.getHandle());
            std::string name;
            for (char c : raw) {
                if (std::isalnum(static_cast<unsigned char>(c)) ||
                    c == ' ' || c == '-' || c == '_')
                    name += c;
                else if (c == '.')
                    continue;
                else
                    name += '_';
            }
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (name.empty()) return;

            const auto home = homeDir();
            const auto dir = home / ".yawn" / "generator" / "presets";
            std::string error;
            if (!generate::saveGeneratorPreset(
                    dir / (name + ".json"), m_generatorPanel->buildParams(),
                    &error)) {
                m_generatorPanel->setStatus(error, true);
                m_contentGrid->invalidate();
                return;
            }
            m_generatorPanel->setPresetNames(
                generate::listGeneratorPresets(generatorPresetDirs()));
            m_generatorPanel->setStatus("Preset saved: " + name, false);
            m_toastManager.show("Preset saved: " + name, 2.0f,
                                ui::ToastManager::Severity::Info);
            m_contentGrid->invalidate();
        });
}

void App::pollGeneratorParams() {
    auto& bus = generate::GeneratorParamBus::instance();
    const uint32_t version = bus.version();
    if (version == m_generatorParamVersion) return;
    m_generatorParamVersion = version;
    if (!m_generatorPanel || !m_generatorOpen) return;

    bool changed = false;
    for (const auto& mapping : m_midiLearnManager.mappings()) {
        if (mapping.target.type !=
            automation::TargetType::GeneratorParam) continue;
        const int slot = mapping.target.paramIndex;
        const float value = bus.read(slot);
        using namespace generate;
        if (slot == kGenDensity) {
            m_generatorPanel->setDensity(value);
            changed = true;
        } else if (slot == kGenNoteLength) {
            m_generatorPanel->setNoteLength(value);
            changed = true;
        } else if (slot == kGenSwing) {
            m_generatorPanel->setSwing(value);
            changed = true;
        } else if (slot == kGenTempo) {
            m_generatorPanel->setTempo(value);
            changed = true;
        } else if (slot >= kGenStyleBase &&
                   slot < kGenStyleBase + 32) {
            const int idx = slot - kGenStyleBase;
            if (idx < m_generatorPanel->styleCount()) {
                m_generatorPanel->setStyleWeight(idx, value);
                changed = true;
            }
        }
    }
    if (changed) m_contentGrid->invalidate();
}

void App::showGeneratorParamMenu(int slot, float mx, float my) {
    using namespace yawn::ui::fw2::Menu;
    using ui::fw2::MenuEntry;
    using namespace generate;

    automation::AutomationTarget target;
    target.type = automation::TargetType::GeneratorParam;
    target.paramIndex = slot;

    std::string name;
    float lo = 0.0f, hi = 1.0f;
    if (slot == kGenDensity) {
        name = "Density";
    } else if (slot == kGenNoteLength) {
        name = "Note Length";
        lo = 0.1f; hi = 2.0f;
    } else if (slot == kGenSwing) {
        name = "Swing";
        hi = 0.75f;
    } else if (slot == kGenTempo) {
        name = "Tempo";
        hi = 300.0f;
    } else if (slot >= kGenStyleBase && slot < kGenStyleBase + 32) {
        const int idx = slot - kGenStyleBase;
        const std::string id = m_generatorPanel
            ? m_generatorPanel->styleId(idx) : std::string{};
        if (id.empty()) return;
        name = "Style: " + id;
    } else {
        return;
    }

    std::vector<MenuEntry> items;
    items.push_back(header(name));
    items.push_back(separator());

    bool mapped = false;
    std::string mappedLabel;
    for (const auto& mm : m_midiLearnManager.mappings()) {
        if (mm.target == target) {
            mapped = true;
            mappedLabel = mm.label();
            break;
        }
    }
    const bool learning = m_midiLearnManager.isLearning() &&
                          m_midiLearnManager.learnTarget() == target;

    if (learning) {
        items.push_back(item("Cancel MIDI Learn", [this]() {
            m_midiLearnManager.cancelLearn();
        }));
    } else if (mapped) {
        items.push_back(item("MIDI: " + mappedLabel + "  (click to re-learn)",
            [this, target, lo, hi]() {
                m_midiLearnManager.startLearn(target, lo, hi);
            }));
        items.push_back(item("Remove MIDI Mapping", [this, target]() {
            m_midiLearnManager.removeByTarget(target);
            markDirty();
        }));
    } else {
        items.push_back(item("MIDI Learn...", [this, target, lo, hi]() {
            m_midiLearnManager.startLearn(target, lo, hi);
        }));
    }

    ui::fw2::ContextMenu::show(std::move(items),
                               ui::fw::Point{ mx, my });
}

void App::onGeneratorCommit(const std::vector<std::string>& roles,
                            const std::vector<int>& tracks) {
    if (roles != m_generatedRoles) {
        m_generatorPanel->setStatus("Lane layout changed — generate again.",
                                    true);
        m_contentGrid->invalidate();
        return;
    }

    generate::CommitResult result;
    std::string error;
    if (!generate::commitGeneratedSong(m_project, m_generatedSong, tracks,
                                       &result, &error)) {
        m_generatorPanel->setStatus(error, true);
        m_contentGrid->invalidate();
        return;
    }

    markDirty();
    auto song = std::make_shared<generate::GeneratedSong>(m_generatedSong);
    const int sceneCount = result.sceneCount;

    m_undoManager.push({"Generate Song",
        [this, sceneCount]() {
            stopAllClipsForSceneEdit();
            generate::removeLastScenesPreservingNames(m_project, sceneCount);
            markDirty();
        },
        [this, song, tracks]() {
            stopAllClipsForSceneEdit();
            generate::CommitResult r;
            std::string e;
            generate::commitGeneratedSong(m_project, *song, tracks, &r, &e);
            markDirty();
        },
        ""});

    LOG_INFO("Generate", "committed %d scenes across %d lanes",
             result.sceneCount, static_cast<int>(roles.size()));

    char msg[160];
    std::snprintf(msg, sizeof(msg), "Generated %d scenes — see Session view",
                  result.sceneCount);
    m_toastManager.show(msg, 3.0f, ui::ToastManager::Severity::Info);

    m_selectedScene = result.firstScene;
    m_generatorOpen = false;
    m_generatorPanel->setVisible(false);
    switchToView(ViewMode::Session);
    m_sessionPanel->setSelectedScene(result.firstScene);
    m_sessionPanel->ensureSelectionVisible();
}

} // namespace yawn
