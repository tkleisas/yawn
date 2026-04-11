#pragma once

// ControllerManager — discovers controller scripts, matches MIDI ports,
// manages lifecycle, and polls MIDI input to drive Lua callbacks.

#include "controllers/ControllerMidiPort.h"
#include "controllers/LuaEngine.h"
#include "util/MessageQueue.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace yawn {

class Project;
namespace audio { class AudioEngine; }

namespace controllers {

struct ControllerScript {
    std::string name;
    std::string author;
    std::string version;
    std::string inputPortMatch;   // substring to match in MIDI port name
    std::string outputPortMatch;
    std::string scriptDir;        // absolute path to script directory
    bool connected = false;
};

class ControllerManager {
public:
    ControllerManager() = default;
    ~ControllerManager() { shutdown(); }

    ControllerManager(const ControllerManager&) = delete;
    ControllerManager& operator=(const ControllerManager&) = delete;

    // Lifecycle — called from App
    void init(audio::AudioEngine* engine, Project* project);
    void shutdown();

    // Scan directories for controller manifests
    void scanScripts(const std::string& bundledPath);

    // Match available MIDI ports to discovered scripts and connect
    void autoConnect();

    // Disconnect current controller
    void disconnect();

    // Called from App::update() at ~60Hz — polls MIDI, calls Lua
    void update();

    // Reload all scripts (disconnect, rescan, reconnect)
    void reloadScripts(const std::string& bundledPath);

    // Accessors for Lua API
    audio::AudioEngine* audioEngine() const { return m_audioEngine; }
    Project* project() const { return m_project; }
    int selectedTrack() const { return m_getSelectedTrack ? m_getSelectedTrack() : 0; }
    void setSelectedTrack(int t) { if (m_setSelectedTrack) m_setSelectedTrack(t); }

    // MIDI output — called from Lua
    void sendMidiToController(const uint8_t* data, int length);

    // Send audio command to engine — called from Lua for note forwarding
    void sendCommand(const audio::AudioCommand& cmd);

    // Clip slot state for controller queries
    struct ClipSlotState {
        int type = 0;         // 0=empty, 1=audio, 2=midi
        bool playing = false;
        bool recording = false;
        bool armed = false;   // track is armed
    };

    // Setters for App-provided callbacks
    void setSelectedTrackGetter(std::function<int()> fn) { m_getSelectedTrack = std::move(fn); }
    void setSelectedTrackSetter(std::function<void(int)> fn) { m_setSelectedTrack = std::move(fn); }
    void setCommandSender(std::function<void(const audio::AudioCommand&)> fn) { m_sendCommand = std::move(fn); }
    void setTapTempoHandler(std::function<void()> fn) { m_tapTempo = std::move(fn); }
    void setClipStateGetter(std::function<ClipSlotState(int track, int scene)> fn) { m_getClipState = std::move(fn); }
    void setClipLauncher(std::function<void(int track, int scene)> fn) { m_launchClip = std::move(fn); }
    void setSceneLauncher(std::function<void(int scene)> fn) { m_launchScene = std::move(fn); }

    // Called from Lua
    ClipSlotState getClipSlotState(int track, int scene) const {
        return m_getClipState ? m_getClipState(track, scene) : ClipSlotState{};
    }
    void launchClip(int track, int scene) { if (m_launchClip) m_launchClip(track, scene); }
    void launchScene(int scene) { if (m_launchScene) m_launchScene(scene); }

    // Controller focus rectangle (session grid window visible on Push pads)
    struct SessionFocus {
        int trackOffset = 0;
        int sceneOffset = 0;
        int cols = 8;
        int rows = 8;
        bool active = false;  // true when controller is in session mode
    };
    void setSessionFocus(int trackOff, int sceneOff, bool active) {
        m_sessionFocus = {trackOff, sceneOff, 8, 8, active};
    }
    const SessionFocus& sessionFocus() const { return m_sessionFocus; }

    // Called from Lua — triggers the UI tap tempo
    void tapTempo() { if (m_tapTempo) m_tapTempo(); }

    // Check if a controller is connected
    bool isConnected() const;
    const std::string& connectedName() const;

    // Returns port names claimed by controllers (MidiEngine should skip these)
    std::vector<std::string> claimedInputPortNames() const;
    std::vector<std::string> claimedOutputPortNames() const;

private:
    // Load manifest.lua from a script directory, returns true if valid
    bool loadManifest(const std::string& dir, ControllerScript& out);

    audio::AudioEngine* m_audioEngine = nullptr;
    Project* m_project = nullptr;
    std::function<int()> m_getSelectedTrack;
    std::function<void(int)> m_setSelectedTrack;
    std::function<void(const audio::AudioCommand&)> m_sendCommand;
    std::function<void()> m_tapTempo;
    std::function<ClipSlotState(int, int)> m_getClipState;
    std::function<void(int, int)> m_launchClip;
    std::function<void(int)> m_launchScene;
    SessionFocus m_sessionFocus;

    // Discovered scripts
    std::vector<ControllerScript> m_scripts;

    // Active controller (currently only one at a time)
    std::unique_ptr<ControllerMidiPort> m_port;
    std::unique_ptr<LuaEngine> m_lua;
    int m_activeScriptIndex = -1;

    // Temp buffer for reading raw messages each frame
    std::vector<std::vector<uint8_t>> m_pendingMessages;

    // Throttle on_tick to 30Hz (every 2nd frame at 60fps)
    int m_tickCounter = 0;

    // Cached bundled path for reload
    std::string m_bundledPath;
};

} // namespace controllers
} // namespace yawn
