#pragma once

#include <atomic>

// Ableton Link: network beat/tempo sync across devices on the same LAN.
// Gated behind YAWN_HAS_LINK — builds without the library compile a
// no-op stub so the rest of the code doesn't need #ifdefs everywhere.

#ifdef YAWN_HAS_LINK
#include <ableton/Link.hpp>
#endif

namespace yawn {

class LinkManager {
public:
    LinkManager();
    ~LinkManager();

    LinkManager(const LinkManager&) = delete;
    LinkManager& operator=(const LinkManager&) = delete;

    void enable(bool on);
    bool enabled() const { return m_enabled.load(std::memory_order_acquire); }
    int numPeers() const;

    // Start/stop synchronisation — opt-in per Link convention.
    // When on, peers exchange transport play/stop state alongside
    // tempo. Off by default; users that just want tempo sync (a
    // common case for jam-along DJs) leave this off.
    void enableStartStopSync(bool on);
    bool startStopSyncEnabled() const {
        return m_startStopSync.load(std::memory_order_acquire);
    }

    // Called from the audio thread on each callback.
    //
    // When `localTempoChanged` is true (the user just typed/turned the
    // BPM box, or any code path called Transport::setBPM via the
    // command queue), THIS BUFFER's local tempo wins: ioBpm is left
    // alone and pushed out to Link so peers adopt it. Without this
    // gate, every UI tempo edit gets clobbered on the very next
    // audio buffer because we'd read the stale session tempo back
    // over the user's new value (race that hides local edits).
    //
    // When `localTempoChanged` is false and peers > 0, ioBpm is
    // overwritten with the session tempo so we follow the network.
    // The committed app session state always carries the resolved
    // ioBpm so other peers see the latest authoritative value.
    void onAudioCallback(double& ioBpm, double& ioBeatPosition,
                          bool isPlaying, bool localTempoChanged);

private:
#ifdef YAWN_HAS_LINK
    ableton::Link m_link{120.0};
#endif
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_startStopSync{false};
};

} // namespace yawn
