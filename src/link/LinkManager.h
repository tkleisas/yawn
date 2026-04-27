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

    // Called from the audio thread on each callback.
    // Reads Link's current tempo into bpm and beat reference.
    // If playing, advances the transport beat position.
    void onAudioCallback(double& ioBpm, double& ioBeatPosition, bool isPlaying);

private:
#ifdef YAWN_HAS_LINK
    ableton::Link m_link{120.0};
#endif
    std::atomic<bool> m_enabled{false};
};

} // namespace yawn
