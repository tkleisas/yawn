#include "link/LinkManager.h"

namespace yawn {

#ifdef YAWN_HAS_LINK

LinkManager::LinkManager() : m_link(120.0) {}

LinkManager::~LinkManager() = default;

void LinkManager::enable(bool on) {
    m_enabled.store(on, std::memory_order_release);
    m_link.enable(on);
}

int LinkManager::numPeers() const {
    return static_cast<int>(m_link.numPeers());
}

void LinkManager::onAudioCallback(double& ioBpm, double& ioBeatPosition,
                                    bool isPlaying, bool localTempoChanged) {
    if (!m_enabled.load(std::memory_order_acquire)) return;

    const auto sessionState = m_link.captureAudioSessionState();

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
    // network or from a local edit) so peers stay in sync.
    auto state = m_link.captureAppSessionState();
    state.setTempo(ioBpm, state.timeAtBeat(ioBeatPosition, 4.0));
    state.setIsPlaying(isPlaying, state.timeAtBeat(ioBeatPosition, 4.0));
    m_link.commitAppSessionState(state);
}

#else

LinkManager::LinkManager() = default;
LinkManager::~LinkManager() = default;
void LinkManager::enable(bool) {}
int LinkManager::numPeers() const { return 0; }
void LinkManager::onAudioCallback(double&, double&, bool, bool) {}

#endif

} // namespace yawn
