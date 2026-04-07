#pragma once

// VST3EditorWindow — Launches a separate process (yawn_vst3_host.exe) to show
// a VST3 plugin's native editor UI. This completely isolates the plugin's
// Win32 message hooks from SDL's event processing, preventing the freeze
// caused by JUCE-based plugins installing process-wide hooks.

#ifdef YAWN_HAS_VST3

#include <atomic>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace yawn {

namespace vst3 {
class VST3PluginInstance;
}

namespace vst3 {

class VST3EditorWindow {
public:
    VST3EditorWindow() = default;
    ~VST3EditorWindow();

    // Launch the editor subprocess. modulePath is the .vst3 file, classID the hex UID.
    bool open(VST3PluginInstance* instance,
              const std::string& modulePath,
              const std::string& classID,
              const std::string& title);

    void close();
    bool isOpen() const;

    VST3PluginInstance* instance() const { return m_instance; }

private:
    VST3PluginInstance* m_instance = nullptr;

#ifdef _WIN32
    HANDLE m_process = nullptr;
    HANDLE m_thread_handle = nullptr;  // process main thread handle
#endif
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
