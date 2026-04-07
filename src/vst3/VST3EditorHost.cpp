// VST3EditorHost — Standalone process that loads a VST3 plugin and shows its
// native editor UI. Launched as a subprocess by YAWN to completely isolate
// the plugin's Win32 message hooks from SDL's event processing.
//
// Usage: yawn_vst3_host.exe <vst3_path> <class_id> <window_title>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#endif

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <string>
#include <atomic>
#include <cstdio>

#ifdef _WIN32

using namespace Steinberg;

static Vst::HostApplication g_hostApp;

// ── IPlugFrame adapter ──

class PlugFrameAdapter : public IPlugFrame {
public:
    explicit PlugFrameAdapter(HWND hwnd) : m_hwnd(hwnd) {}

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!m_hwnd || !newSize) return kInvalidArgument;
        int w = newSize->getWidth();
        int h = newSize->getHeight();
        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
        RECT rc = {0, 0, w, h};
        AdjustWindowRect(&rc, style, FALSE);
        SetWindowPos(m_hwnd, nullptr, 0, 0,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (view) view->onSize(newSize);
        return kResultOk;
    }

    uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    uint32 PLUGIN_API release() override {
        auto r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = static_cast<IPlugFrame*>(this);
            return kResultOk;
        }
        return kNoInterface;
    }

private:
    HWND m_hwnd = nullptr;
    std::atomic<int32> m_refCount{1};
};

// ── Window ──

static const wchar_t* kClassName = L"YawnVST3EditorHost";

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Main ──

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: yawn_vst3_host <vst3_path> <class_id> <window_title>\n");
        return 1;
    }

    std::string vst3Path = argv[1];
    std::string classIdHex = argv[2];
    std::string windowTitle = argv[3];

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Load VST3 module
    std::string error;
    auto module = VST3::Hosting::Module::create(vst3Path, error);
    if (!module) {
        fprintf(stderr, "Failed to load '%s': %s\n", vst3Path.c_str(), error.c_str());
        CoUninitialize();
        return 1;
    }

    // Parse class ID
    auto optUid = VST3::UID::fromString(classIdHex, false);
    if (!optUid) {
        fprintf(stderr, "Invalid class ID: %s\n", classIdHex.c_str());
        CoUninitialize();
        return 1;
    }
    auto uid = *optUid;

    // Create component
    auto& factory = module->getFactory();
    auto component = factory.createInstance<Vst::IComponent>(uid);
    if (!component) {
        fprintf(stderr, "Failed to create component\n");
        CoUninitialize();
        return 1;
    }
    component->initialize(&g_hostApp);

    // Get controller (may be same object or separate)
    IPtr<Vst::IEditController> controller;
    bool controllerIsComponent = false;

    if (component->queryInterface(Vst::IEditController::iid,
                                   reinterpret_cast<void**>(&controller)) == kResultOk) {
        controllerIsComponent = true;
    } else {
        TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == kResultOk) {
            controller = factory.createInstance<Vst::IEditController>(
                VST3::UID::fromTUID(controllerCID));
            if (controller) {
                controller->initialize(&g_hostApp);
            }
        }
    }

    if (!controller) {
        fprintf(stderr, "Failed to get controller\n");
        component->terminate();
        CoUninitialize();
        return 1;
    }

    // Create editor view
    auto* rawView = controller->createView(Vst::ViewType::kEditor);
    if (!rawView) {
        fprintf(stderr, "Plugin has no editor view\n");
        if (!controllerIsComponent) controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }
    auto plugView = owned(rawView);

    if (plugView->isPlatformTypeSupported(kPlatformTypeHWND) != kResultOk) {
        fprintf(stderr, "Plugin does not support HWND\n");
        if (!controllerIsComponent) controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }

    // Query preferred size
    ViewRect viewRect = {};
    if (plugView->getSize(&viewRect) != kResultOk) {
        viewRect.right = 800;
        viewRect.bottom = 600;
    }
    int w = viewRect.getWidth();
    int h = viewRect.getHeight();
    if (w <= 0 || h <= 0) { w = 800; h = 600; }

    // Register window class
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Convert title to wide string
    int wLen = MultiByteToWideChar(CP_UTF8, 0, windowTitle.c_str(), -1, nullptr, 0);
    std::wstring wTitle(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, windowTitle.c_str(), -1, &wTitle[0], wLen);

    // Create window
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0, kClassName, wTitle.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) {
        fprintf(stderr, "Failed to create window\n");
        if (!controllerIsComponent) controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }

    // Attach plugin view
    auto plugFrame = owned(new PlugFrameAdapter(hwnd));
    plugView->setFrame(plugFrame.get());

    auto result = plugView->attached(hwnd, kPlatformTypeHWND);
    if (result != kResultOk) {
        fprintf(stderr, "Failed to attach view (result=%d)\n", (int)result);
        DestroyWindow(hwnd);
        if (!controllerIsComponent) controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Signal parent that we're ready
    fprintf(stdout, "READY\n");
    fflush(stdout);

    // ── Message loop ──
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ── Cleanup ──
    plugView->removed();
    plugView->setFrame(nullptr);
    plugView = nullptr;
    plugFrame = nullptr;

    DestroyWindow(hwnd);

    if (!controllerIsComponent) controller->terminate();
    component->terminate();
    controller = nullptr;
    component = nullptr;
    module = nullptr;

    CoUninitialize();
    return 0;
}

#else
// Non-Windows stub
#include <cstdio>
int main() {
    fprintf(stderr, "VST3 editor host not supported on this platform yet\n");
    return 1;
}
#endif
