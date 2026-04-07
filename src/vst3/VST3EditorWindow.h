#pragma once

// VST3EditorWindow — Launches a separate process (yawn_vst3_host.exe) to show
// a VST3 plugin's native editor UI with bidirectional parameter sync via pipes.
//
// Editor→Host: plugin UI param changes are sent to YAWN and applied to the
//              real audio processor so the sound updates in real time.
// Host→Editor: (future) YAWN knob/automation changes sent to update the editor UI.

#ifdef YAWN_HAS_VST3

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace yawn {
namespace vst3 {

class VST3PluginInstance;

class VST3EditorWindow {
public:
    VST3EditorWindow() = default;
    ~VST3EditorWindow();

    bool open(VST3PluginInstance* instance,
              const std::string& modulePath,
              const std::string& classID,
              const std::string& title);

    void close();
    bool isOpen() const;

    // Call from App::update() to read incoming parameter changes from the editor
    void pollParamChanges();

    // Send a parameter change to the editor process (for knob/automation sync)
    void sendParamChange(unsigned int paramId, double value);

    VST3PluginInstance* instance() const { return m_instance; }

private:
    void processLine(const std::string& line);

    VST3PluginInstance* m_instance = nullptr;

#ifdef _WIN32
    HANDLE m_process = nullptr;
    HANDLE m_processThread = nullptr;
    HANDLE m_pipeRead = INVALID_HANDLE_VALUE;   // read FROM child (child's stdout)
    HANDLE m_pipeWrite = INVALID_HANDLE_VALUE;  // write TO child (child's stdin)
    std::string m_readBuffer;
#endif
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
