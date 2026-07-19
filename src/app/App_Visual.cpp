// App_Visual.cpp — visual/VJ glue. The visual-clip state machine
// (launches, follow actions, arrangement playback, knob automation,
// macro mappings, video imports, asset localization) lives in
// VisualClipController; these App methods are thin forwarders kept
// so the ~35 call sites in menus / frame / events / widget-tree
// don't care where the work happens.
#include "app/App.h"
#include "ui/ToastManager.h"

namespace yawn {

// ── VisualClipHost implementation ─────────────────────────────────

void App::vccToast(const std::string& msg, float seconds, int severity) {
    m_toastManager.show(msg, seconds,
        static_cast<ui::ToastManager::Severity>(severity));
}

void App::vccUpdateClipState(int track, bool playing, int64_t playPos, int scene) {
    m_sessionPanel->updateClipState(track, playing, playPos, scene);
}

void App::vccSetSlotImporting(int track, int scene, bool importing) {
    m_sessionPanel->setSlotImporting(track, scene, importing);
}

void App::vccSetSlotImportProgress(int track, int scene, float progress) {
    m_sessionPanel->setSlotImportProgress(track, scene, progress);
}

void App::vccSetPanelKnobValues(const float* knobs) {
    m_visualParamsPanel->setKnobValues(knobs);
}

bool App::vccLoadClipToSlot(const std::string& path, int track, int scene) {
    return loadClipToSlot(path, track, scene);
}

// ── Launch / playback forwarders ──────────────────────────────────

void App::stampVisualLaunch(int track, int scene) {
    m_visualController->stampVisualLaunch(track, scene);
}

void App::launchVisualClipQuantized(int track, int scene, bool transportWillPlay) {
    m_visualController->launchVisualClipQuantized(track, scene, transportWillPlay);
}

void App::launchVisualClipData(int track, const visual::VisualClip& vc,
                               const std::string& shaderPath) {
    m_visualController->launchVisualClipData(track, vc, shaderPath);
}

void App::pollVisualLaunchQueue() {
    m_visualController->pollVisualLaunchQueue();
}

void App::pollArrangementVisualPlayback() {
    m_visualController->pollArrangementVisualPlayback();
}

int App::resolveFollowActionScene(int track, int currentScene,
                                  FollowActionType action) const {
    return m_visualController->resolveFollowActionScene(track, currentScene, action);
}

void App::pollVisualFollowActions() {
    m_visualController->pollVisualFollowActions();
}

void App::stopAllVisualLayers() {
    m_visualController->stopAllVisualLayers();
}

void App::stopAllClips() {
    // Stop every audio/MIDI clip and wipe each track's launch memory so
    // the next transport Play starts nothing (Ableton "Stop Clips").
    // Audio/MIDI grid indicators clear via the engine's play-state
    // feedback; visual layers + their indicators are cleared below.
    for (int t = 0; t < m_project.numTracks(); ++t) {
        m_audioEngine.sendCommand(audio::StopClipMsg{t});
        m_audioEngine.sendCommand(audio::StopMidiClipMsg{t});
        m_project.track(t).defaultScene = -1;
    }
    m_visualController->stopAllVisualLayers();
    markDirty();
}

// ── Automation / modulation forwarders ────────────────────────────

void App::pollVisualKnobAutomation() {
    m_visualController->pollVisualKnobAutomation();
}

void App::applyMacroMappings() {
    m_visualController->applyMacroMappings();
}

void App::applyAudioMacroModulation(double beat, double wall) {
    m_visualController->applyAudioMacroModulation(beat, wall);
}

// ── Asset path forwarders ─────────────────────────────────────────

std::string App::resolveShaderPath(const std::string& stored) const {
    return m_visualController->resolveShaderPath(stored);
}

std::string App::resolveModelPath(const std::string& stored) const {
    return m_visualController->resolveModelPath(stored);
}

std::string App::resolveScenePath(const std::string& stored) const {
    return m_visualController->resolveScenePath(stored);
}

std::string App::localizeShader(const std::string& sourcePath) {
    return m_visualController->localizeShader(sourcePath);
}

std::string App::localizeModel(const std::string& sourcePath) {
    return m_visualController->localizeModel(sourcePath);
}

std::string App::localizeScene(const std::string& sourcePath) {
    return m_visualController->localizeScene(sourcePath);
}

// ── Model clip ops / slot content forwarders ──────────────────────

void App::reloadVisualClipModels(int track, int scene) {
    m_visualController->reloadVisualClipModels(track, scene);
}

void App::addModelToClip(int track, int scene, const std::string& sourcePath) {
    m_visualController->addModelToClip(track, scene, sourcePath);
}

void App::removeModelFromClip(int track, int scene, int listIndex) {
    m_visualController->removeModelFromClip(track, scene, listIndex);
}

void App::assignModelFromLibrary(const std::string& sourcePath) {
    m_visualController->assignModelFromLibrary(sourcePath);
}

void App::addImageToSlot(int track, int scene, const std::string& sourcePath) {
    m_visualController->addImageToSlot(track, scene, sourcePath);
}

void App::startVideoImport(int track, int scene, const std::string& sourcePath) {
    m_visualController->startVideoImport(track, scene, sourcePath);
}

} // namespace yawn
