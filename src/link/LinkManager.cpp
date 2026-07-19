#include "link/LinkManager.h"

namespace yawn {

#ifdef YAWN_HAS_LINK

LinkManager::LinkManager() : m_link(120.0) {}

LinkManager::~LinkManager() = default;

void LinkManager::enable(bool on) {
    m_enabled.store(on, std::memory_order_release);
    m_link.enable(on);
}

void LinkManager::enableStartStopSync(bool on) {
    m_startStopSync.store(on, std::memory_order_release);
    m_link.enableStartStopSync(on);
}

int LinkManager::numPeers() const {
    return static_cast<int>(m_link.numPeers());
}

void LinkManager::onAudioCallback(double& ioBpm, double& ioBeatPosition,
                                    bool isPlaying, bool localTempoChanged) {
    if (!m_enabled.load(std::memory_order_acquire)) return;

    auto sessionState = m_link.captureAudioSessionState();

    // Local-edit wins for this buffer — see header comment. Without
    // the !localTempoChanged guard, the user can never change tempo
    // when peers are connected (every edit gets clobbered by the
    // stale sessionState.tempo() read here).
    if (m_link.numPeers() > 0 && !localTempoChanged) {
        ioBpm = sessionState.tempo();

        // Read beat position synced to Link timeline
        const auto hostTime = m_link.clock().micros();
        ioBeatPosition = sessionState.beatAtTime(hostTime, 4.0);
    }

    // Always commit the resolved tempo (whether it came from the
    // network or from a local edit) so peers stay in sync. Reuse the
    // audio session state captured above and commit through the
    // realtime-safe audio-thread API — capture/commitAppSessionState
    // take internal locks and must never run on this thread.
    sessionState.setTempo(ioBpm, sessionState.timeAtBeat(ioBeatPosition, 4.0));
    sessionState.setIsPlaying(isPlaying, sessionState.timeAtBeat(ioBeatPosition, 4.0));
    m_link.commitAudioSessionState(sessionState);
}

#else

LinkManager::LinkManager() = default;
LinkManager::~LinkManager() = default;
void LinkManager::enable(bool) {}
void LinkManager::enableStartStopSync(bool) {}
int LinkManager::numPeers() const { return 0; }
void LinkManager::onAudioCallback(double&, double&, bool, bool) {}

#endif

} // namespace yawn
