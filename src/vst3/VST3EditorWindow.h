#pragma once

// VST3EditorWindow — Opens a native OS window hosting a VST3 plugin's editor UI.
// On Windows this creates an HWND; on macOS an NSView (future); on Linux X11 (future).
// Implements IPlugFrame so the plugin can request resize.

#ifdef YAWN_HAS_VST3

#include "vst3/VST3Host.h"
#include "pluginterfaces/gui/iplugview.h"

#include <atomic>
#include <memory>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace yawn {
namespace vst3 {

// IPlugFrame implementation — host-side resize handler
class PlugFrameAdapter : public Steinberg::IPlugFrame {
public:
    explicit PlugFrameAdapter(class VST3EditorWindow* owner) : m_owner(owner) {}

    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView* view,
                                              Steinberg::ViewRect* newSize) override;

    Steinberg::uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    Steinberg::uint32 PLUGIN_API release() override {
        auto r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,
                                                  void** obj) override;

private:
    VST3EditorWindow* m_owner = nullptr;
    std::atomic<Steinberg::int32> m_refCount{1};
};

class VST3EditorWindow {
public:
    VST3EditorWindow() = default;
    ~VST3EditorWindow();

    // Open the editor for a plugin instance. Returns true on success.
    bool open(VST3PluginInstance* instance, const std::string& title);

    // Close the editor window and detach the view.
    void close();

    // Is the editor window currently open?
    bool isOpen() const { return m_isOpen; }

    // Process pending window messages (call from main loop)
    static void pollEvents();

    // Resize the native window (called by IPlugFrame)
    void resizeToView(int width, int height);

    // Get the plugin instance this editor is for
    VST3PluginInstance* instance() const { return m_instance; }

private:
    bool createNativeWindow(const std::string& title, int width, int height);
    void destroyNativeWindow();

    VST3PluginInstance* m_instance = nullptr;
    Steinberg::IPtr<Steinberg::IPlugView> m_plugView;
    Steinberg::IPtr<PlugFrameAdapter> m_plugFrame;
    bool m_isOpen = false;

#ifdef _WIN32
    HWND m_hwnd = nullptr;
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static bool s_classRegistered;
#endif
};

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
