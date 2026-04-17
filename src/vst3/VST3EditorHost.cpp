// VST3EditorHost — Standalone process that loads a VST3 plugin and shows its
// native editor UI. Launched as a subprocess by YAWN to completely isolate
// the plugin's Win32 message hooks from SDL's event processing.
//
// Communication with YAWN is via inherited stdin/stdout pipes:
//   stdin  (from YAWN):  "PARAM <id> <value>\n", "CLOSE\n"
//   stdout (to YAWN):    "PARAM <id> <value>\n", "READY\n"
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
#include "pluginterfaces/base/ibstream.h"
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
static HANDLE g_hStdout = INVALID_HANDLE_VALUE;
static IPtr<Vst::IEditController> g_controller;
static IPtr<Vst::IComponent> g_component;
static HWND g_mainHwnd = nullptr;

#define WM_PIPE_PARAM  (WM_APP + 1)
#define WM_SYNC_PARAMS (WM_APP + 2)

// ── Simple MemoryStream for IBStream serialization ──

class MemoryStream : public IBStream {
public:
    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        int32 avail = static_cast<int32>(m_data.size()) - m_pos;
        int32 toRead = (numBytes < avail) ? numBytes : avail;
        if (toRead > 0) { std::memcpy(buffer, m_data.data() + m_pos, toRead); m_pos += toRead; }
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        if (numBytes <= 0) { if (numBytesWritten) *numBytesWritten = 0; return kResultOk; }
        auto newSize = m_pos + numBytes;
        if (newSize > static_cast<int32>(m_data.size())) m_data.resize(newSize);
        std::memcpy(m_data.data() + m_pos, buffer, numBytes);
        m_pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
        int64 newPos = 0;
        switch (mode) {
            case kIBSeekSet: newPos = pos; break;
            case kIBSeekCur: newPos = m_pos + pos; break;
            case kIBSeekEnd: newPos = static_cast<int64>(m_data.size()) + pos; break;
            default: return kInvalidArgument;
        }
        if (newPos < 0) return kInvalidArgument;
        m_pos = static_cast<int32>(newPos);
        if (result) *result = m_pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* pos) override {
        if (pos) *pos = m_pos;
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    uint32 PLUGIN_API release() override {
        auto r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    tresult PLUGIN_API queryInterface(const TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return kNoInterface;
    }
    const std::vector<uint8_t>& data() const { return m_data; }
private:
    std::vector<uint8_t> m_data;
    int32 m_pos = 0;
    std::atomic<int32> m_refCount{1};
};

// ── Hex encoding helpers ──

static std::string toHex(const std::vector<uint8_t>& data) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

// ── Write a line to stdout pipe (thread-safe enough for our use) ──

static void writePipe(const char* data, int len) {
    if (g_hStdout == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_hStdout, data, static_cast<DWORD>(len), &written, nullptr);
}

// ── Send full component state as hex-encoded blob ──

static void sendState() {
    if (!g_component) return;

    auto* stream = new MemoryStream();
    if (g_component->getState(static_cast<IBStream*>(stream)) == kResultOk &&
        !stream->data().empty()) {
        std::string hex = toHex(stream->data());
        std::string msg = "STATE " + hex + "\n";
        writePipe(msg.c_str(), static_cast<int>(msg.size()));
    }
    stream->release();
}

// ── IComponentHandler — receives parameter changes from the plugin editor ──
// Also implements IComponentHandler2 for preset/dirty notifications

class EditorComponentHandler : public Vst::IComponentHandler,
                                public Vst::IComponentHandler2 {
public:
    tresult PLUGIN_API beginEdit(Vst::ParamID /*id*/) override { return kResultOk; }

    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override {
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "PARAM %u %.15g\n",
                           static_cast<unsigned>(id), value);
        writePipe(buf, len);
        return kResultOk;
    }

    tresult PLUGIN_API endEdit(Vst::ParamID /*id*/) override { return kResultOk; }

    tresult PLUGIN_API restartComponent(int32 flags) override {
        if ((flags & Steinberg::Vst::RestartFlags::kParamValuesChanged) && g_mainHwnd) {
            // Defer param sync — let plugin finish updating its internal state first
            PostMessageW(g_mainHwnd, WM_SYNC_PARAMS, 0, 0);
        }
        return kResultOk;
    }

    // IComponentHandler2
    tresult PLUGIN_API setDirty(TBool state) override {
        if (state && g_mainHwnd) {
            PostMessageW(g_mainHwnd, WM_SYNC_PARAMS, 0, 0);
        }
        return kResultOk;
    }

    tresult PLUGIN_API requestOpenEditor(FIDString /*name*/) override { return kResultOk; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }

    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<Vst::IComponentHandler*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler2::iid)) {
            *obj = static_cast<Vst::IComponentHandler2*>(this);
            return kResultOk;
        }
        return kNoInterface;
    }
};

static EditorComponentHandler g_componentHandler;

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

struct ParamMsg {
    Vst::ParamID id;
    double value;
};

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_PIPE_PARAM: {
            auto* pm = reinterpret_cast<ParamMsg*>(lp);
            if (g_controller && pm) {
                g_controller->setParamNormalized(pm->id, pm->value);
            }
            delete pm;
            return 0;
        }
        case WM_SYNC_PARAMS:
            sendState();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Pipe reader thread — reads from stdin, posts messages to main thread ──

static DWORD WINAPI pipeReaderThread(LPVOID param) {
    HANDLE hStdin = static_cast<HANDLE>(param);
    char buf[4096];
    std::string readBuf;

    while (true) {
        DWORD bytesRead = 0;
        if (!ReadFile(hStdin, buf, sizeof(buf) - 1, &bytesRead, nullptr) || bytesRead == 0)
            break;

        readBuf.append(buf, bytesRead);
        size_t pos;
        while ((pos = readBuf.find('\n')) != std::string::npos) {
            std::string line = readBuf.substr(0, pos);
            readBuf.erase(0, pos + 1);

            if (line.size() > 6 && line[0] == 'P' && line[1] == 'A') {
                // "PARAM <id> <value>"
                auto* pm = new ParamMsg;
                unsigned rawId = 0;
                if (sscanf(line.c_str() + 6, "%u %lf", &rawId, &pm->value) == 2) {
                    pm->id = static_cast<Vst::ParamID>(rawId);
                    PostMessageW(g_mainHwnd, WM_PIPE_PARAM, 0, reinterpret_cast<LPARAM>(pm));
                } else {
                    delete pm;
                }
            } else if (line == "CLOSE") {
                PostMessageW(g_mainHwnd, WM_CLOSE, 0, 0);
            }
        }
    }
    return 0;
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

    // Get pipe handles (inherited from parent via stdin/stdout redirection)
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

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
    g_component = component;

    // Get controller (may be same object or separate)
    bool controllerIsComponent = false;

    if (component->queryInterface(Vst::IEditController::iid,
                                   reinterpret_cast<void**>(&g_controller)) == kResultOk) {
        controllerIsComponent = true;
    } else {
        TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == kResultOk) {
            g_controller = factory.createInstance<Vst::IEditController>(
                VST3::UID::fromTUID(controllerCID));
            if (g_controller) {
                g_controller->initialize(&g_hostApp);
            }
        }
    }

    if (!g_controller) {
        fprintf(stderr, "Failed to get controller\n");
        component->terminate();
        CoUninitialize();
        return 1;
    }

    // Set component handler so we receive performEdit/restartComponent callbacks
    g_controller->setComponentHandler(&g_componentHandler);

    // Create editor view
    auto* rawView = g_controller->createView(Vst::ViewType::kEditor);
    if (!rawView) {
        fprintf(stderr, "Plugin has no editor view\n");
        if (!controllerIsComponent) g_controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }
    auto plugView = owned(rawView);

    if (plugView->isPlatformTypeSupported(kPlatformTypeHWND) != kResultOk) {
        fprintf(stderr, "Plugin does not support HWND\n");
        if (!controllerIsComponent) g_controller->terminate();
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
        if (!controllerIsComponent) g_controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }

    g_mainHwnd = hwnd;

    // Attach plugin view
    auto plugFrame = owned(new PlugFrameAdapter(hwnd));
    plugView->setFrame(plugFrame.get());

    auto result = plugView->attached(hwnd, kPlatformTypeHWND);
    if (result != kResultOk) {
        fprintf(stderr, "Failed to attach view (result=%d)\n", (int)result);
        DestroyWindow(hwnd);
        if (!controllerIsComponent) g_controller->terminate();
        component->terminate();
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Start pipe reader thread
    HANDLE hReaderThread = nullptr;
    if (hStdin != INVALID_HANDLE_VALUE) {
        hReaderThread = CreateThread(nullptr, 0, pipeReaderThread, hStdin, 0, nullptr);
    }

    // Signal parent that we're ready
    writePipe("READY\n", 6);

    // ── Message loop ──
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ── Cleanup ──
    // Stop reader thread first (close stdin to unblock ReadFile)
    if (hReaderThread) {
        CancelSynchronousIo(hReaderThread);
        CloseHandle(hStdin);
        hStdin = INVALID_HANDLE_VALUE;
        WaitForSingleObject(hReaderThread, 2000);
        CloseHandle(hReaderThread);
        hReaderThread = nullptr;
    }

    // Drain any remaining messages (pending WM_PIPE_PARAM)
    MSG cleanupMsg;
    while (PeekMessageW(&cleanupMsg, hwnd, 0, 0, PM_REMOVE)) {
        if (cleanupMsg.message == WM_PIPE_PARAM) {
            delete reinterpret_cast<ParamMsg*>(cleanupMsg.lParam);
        }
    }

    // Detach plugin view before destroying window
    if (plugView) {
        plugView->removed();
        plugView->setFrame(nullptr);
        plugView = nullptr;
    }
    plugFrame = nullptr;

    DestroyWindow(hwnd);
    g_mainHwnd = nullptr;

    g_controller->setComponentHandler(nullptr);
    if (!controllerIsComponent) g_controller->terminate();
    component->terminate();
    g_controller = nullptr;
    g_component = nullptr;
    component = nullptr;
    module = nullptr;

    CoUninitialize();
    return 0;
}

#elif defined(__linux__)

#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>

using namespace Steinberg;

// ── Forward decls ──
class RunLoopImpl;
static RunLoopImpl* runLoop();

// ── HostApplication that also exposes Linux::IRunLoop ──
// Surge XT (and other JUCE-based plugins) query the host context for IRunLoop
// during initialize() / createView(), before any editor attachment — so we
// must provide it here, not only via IPlugFrame. We also advertise IRunLoop
// via the host's IPlugInterfaceSupport so the plugin's isPlugInterfaceSupported
// check returns true.
class HostAppWithRunLoop : public Vst::HostApplication {
public:
    HostAppWithRunLoop() {
        if (auto* pis = getPlugInterfaceSupport())
            pis->addPlugInterfaceSupported(Linux::IRunLoop::iid);
    }
    tresult PLUGIN_API queryInterface(const char* iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            *obj = reinterpret_cast<Linux::IRunLoop*>(runLoop());
            return kResultOk;
        }
        return Vst::HostApplication::queryInterface(iid, obj);
    }
};

// ── Globals ──
static HostAppWithRunLoop g_hostApp;
static IPtr<Vst::IEditController> g_controller;
static IPtr<Vst::IComponent> g_component;
static Display* g_display = nullptr;
static ::Window g_window = 0;
static ::Atom g_wmDeleteAtom = 0;
static const int g_stdoutFd = STDOUT_FILENO;
static const int g_stdinFd = STDIN_FILENO;
static volatile bool g_quit = false;

// ── MemoryStream ──
class MemoryStream : public IBStream {
public:
    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        int32 avail = static_cast<int32>(m_data.size()) - m_pos;
        int32 toRead = (numBytes < avail) ? numBytes : avail;
        if (toRead > 0) { std::memcpy(buffer, m_data.data() + m_pos, toRead); m_pos += toRead; }
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        if (numBytes <= 0) { if (numBytesWritten) *numBytesWritten = 0; return kResultOk; }
        auto newSize = m_pos + numBytes;
        if (newSize > static_cast<int32>(m_data.size())) m_data.resize(newSize);
        std::memcpy(m_data.data() + m_pos, buffer, numBytes);
        m_pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
        int64 newPos = 0;
        switch (mode) {
            case kIBSeekSet: newPos = pos; break;
            case kIBSeekCur: newPos = m_pos + pos; break;
            case kIBSeekEnd: newPos = static_cast<int64>(m_data.size()) + pos; break;
            default: return kInvalidArgument;
        }
        if (newPos < 0) return kInvalidArgument;
        m_pos = static_cast<int32>(newPos);
        if (result) *result = m_pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* pos) override { if (pos) *pos = m_pos; return kResultOk; }
    uint32 PLUGIN_API addRef() override { return ++m_refCount; }
    uint32 PLUGIN_API release() override {
        auto r = --m_refCount;
        if (r == 0) delete this;
        return r;
    }
    tresult PLUGIN_API queryInterface(const TUID, void** obj) override {
        if (obj) *obj = nullptr;
        return kNoInterface;
    }
    const std::vector<uint8_t>& data() const { return m_data; }
private:
    std::vector<uint8_t> m_data;
    int32 m_pos = 0;
    std::atomic<int32> m_refCount{1};
};

static std::string toHex(const std::vector<uint8_t>& data) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

static void writePipe(const char* data, int len) {
    ssize_t r = ::write(g_stdoutFd, data, len);
    (void)r;
}

static void sendState() {
    if (!g_component) return;
    auto* stream = new MemoryStream();
    if (g_component->getState(static_cast<IBStream*>(stream)) == kResultOk &&
        !stream->data().empty()) {
        std::string hex = toHex(stream->data());
        std::string msg = "STATE " + hex + "\n";
        writePipe(msg.c_str(), static_cast<int>(msg.size()));
    }
    stream->release();
}

// ── IRunLoop impl (static-lifetime singleton) ──
class RunLoopImpl : public Linux::IRunLoop {
public:
    struct EHandler { Linux::IEventHandler* handler; Linux::FileDescriptor fd; };
    struct Timer    { Linux::ITimerHandler* handler; Linux::TimerInterval intervalMs; int64_t nextFireMs; };

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler,
                                             Linux::FileDescriptor fd) override {
        if (!handler) return kInvalidArgument;
        m_eventHandlers.push_back({handler, fd});
        return kResultOk;
    }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override {
        if (!handler) return kInvalidArgument;
        m_eventHandlers.erase(
            std::remove_if(m_eventHandlers.begin(), m_eventHandlers.end(),
                [handler](const EHandler& e) { return e.handler == handler; }),
            m_eventHandlers.end());
        return kResultOk;
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler,
                                      Linux::TimerInterval ms) override {
        if (!handler || ms == 0) return kInvalidArgument;
        m_timers.push_back({handler, ms, nowMs() + static_cast<int64_t>(ms)});
        return kResultOk;
    }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override {
        if (!handler) return kInvalidArgument;
        m_timers.erase(
            std::remove_if(m_timers.begin(), m_timers.end(),
                [handler](const Timer& t) { return t.handler == handler; }),
            m_timers.end());
        return kResultOk;
    }

    uint32 PLUGIN_API addRef() override { return 1000; }   // static lifetime
    uint32 PLUGIN_API release() override { return 1000; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<Linux::IRunLoop*>(this);
            return kResultOk;
        }
        return kNoInterface;
    }

    static int64_t nowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    std::vector<EHandler> m_eventHandlers;
    std::vector<Timer>    m_timers;
};

static RunLoopImpl g_runLoop;
static RunLoopImpl* runLoop() { return &g_runLoop; }

// ── IPlugFrame — delegates IRunLoop queries to the singleton ──
class XPlugFrame : public IPlugFrame {
public:
    XPlugFrame(Display* d, ::Window w) : m_display(d), m_window(w) {}

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!newSize) return kInvalidArgument;
        unsigned int w = static_cast<unsigned int>(std::max(1, newSize->getWidth()));
        unsigned int h = static_cast<unsigned int>(std::max(1, newSize->getHeight()));
        XResizeWindow(m_display, m_window, w, h);
        XFlush(m_display);
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
            addRef(); *obj = static_cast<IPlugFrame*>(this); return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, Linux::IRunLoop::iid)) {
            std::fprintf(stderr, "[yawn_vst3_host] PlugFrame queried for IRunLoop — returning it\n");
            *obj = static_cast<Linux::IRunLoop*>(&g_runLoop);
            return kResultOk;
        }
        std::fprintf(stderr, "[yawn_vst3_host] PlugFrame queried for unknown IID\n");
        return kNoInterface;
    }

private:
    Display* m_display;
    ::Window m_window;
    std::atomic<int32> m_refCount{1};
};

static IPtr<XPlugFrame> g_plugFrame;

// ── IComponentHandler ──
class EditorComponentHandler : public Vst::IComponentHandler,
                                public Vst::IComponentHandler2 {
public:
    tresult PLUGIN_API beginEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue value) override {
        char buf[128];
        int len = std::snprintf(buf, sizeof(buf), "PARAM %u %.15g\n",
                                static_cast<unsigned>(id), value);
        if (len > 0) writePipe(buf, len);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(Vst::ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        if (flags & Steinberg::Vst::RestartFlags::kParamValuesChanged) sendState();
        return kResultOk;
    }
    tresult PLUGIN_API setDirty(TBool state) override {
        if (state) sendState();
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString) override { return kResultOk; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (!obj) return kInvalidArgument;
        *obj = nullptr;
        if (FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<Vst::IComponentHandler*>(this); return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, Vst::IComponentHandler2::iid)) {
            *obj = static_cast<Vst::IComponentHandler2*>(this); return kResultOk;
        }
        return kNoInterface;
    }
};
static EditorComponentHandler g_componentHandler;

// ── stdin command parser ──
static std::string g_stdinBuf;

static void handleStdinLine(const std::string& line) {
    if (line.size() > 6 && line[0] == 'P' && line[1] == 'A') {
        unsigned rawId = 0;
        double value = 0.0;
        if (std::sscanf(line.c_str() + 6, "%u %lf", &rawId, &value) == 2) {
            if (g_controller) {
                g_controller->setParamNormalized(static_cast<Vst::ParamID>(rawId), value);
            }
        }
    } else if (line == "CLOSE") {
        g_quit = true;
    }
}

static void readStdinAndDispatch() {
    char buf[4096];
    while (true) {
        ssize_t n = ::read(g_stdinFd, buf, sizeof(buf));
        if (n > 0) { g_stdinBuf.append(buf, n); continue; }
        if (n == 0) { g_quit = true; return; }
        if (errno == EINTR) continue;
        break;  // EAGAIN
    }
    size_t pos;
    while ((pos = g_stdinBuf.find('\n')) != std::string::npos) {
        std::string line = g_stdinBuf.substr(0, pos);
        g_stdinBuf.erase(0, pos + 1);
        handleStdinLine(line);
    }
}

static void handleXEvent(XEvent& e) {
    switch (e.type) {
        case ClientMessage:
            if (static_cast<Atom>(e.xclient.data.l[0]) == g_wmDeleteAtom)
                g_quit = true;
            break;
        case ReparentNotify:
            std::fprintf(stderr, "[yawn_vst3_host] ReparentNotify: window 0x%lx reparented to 0x%lx\n",
                         (unsigned long)e.xreparent.window, (unsigned long)e.xreparent.parent);
            break;
        case MapNotify:
            std::fprintf(stderr, "[yawn_vst3_host] MapNotify: 0x%lx\n",
                         (unsigned long)e.xmap.window);
            break;
        case UnmapNotify:
            std::fprintf(stderr, "[yawn_vst3_host] UnmapNotify: 0x%lx\n",
                         (unsigned long)e.xunmap.window);
            break;
        case CreateNotify:
            std::fprintf(stderr, "[yawn_vst3_host] CreateNotify: 0x%lx (%dx%d)\n",
                         (unsigned long)e.xcreatewindow.window,
                         e.xcreatewindow.width, e.xcreatewindow.height);
            break;
        case ConfigureNotify:
            if (e.xconfigure.window == g_window)
                std::fprintf(stderr, "[yawn_vst3_host] ConfigureNotify parent: %dx%d\n",
                             e.xconfigure.width, e.xconfigure.height);
            break;
        default: break;
    }
}

int main(int argc, char* argv[]) {
    // Unbuffered stderr so diagnostics survive even on abrupt exit.
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::fprintf(stderr, "[yawn_vst3_host] starting, argc=%d\n", argc);

    if (argc < 4) {
        std::fprintf(stderr, "Usage: yawn_vst3_host <vst3_path> <class_id> <window_title>\n");
        return 1;
    }
    std::string vst3Path    = argv[1];
    std::string classIdHex  = argv[2];
    std::string windowTitle = argv[3];

    std::fprintf(stderr, "[yawn_vst3_host] path=%s classID=%s\n",
                 vst3Path.c_str(), classIdHex.c_str());

    int flags = ::fcntl(g_stdinFd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(g_stdinFd, F_SETFL, flags | O_NONBLOCK);

    ::signal(SIGPIPE, SIG_IGN);

    if (!XInitThreads()) {
        std::fprintf(stderr, "XInitThreads failed\n");
    }

    g_display = XOpenDisplay(nullptr);
    if (!g_display) {
        const char* d = std::getenv("DISPLAY");
        std::fprintf(stderr, "Failed to open X display (DISPLAY=%s)\n", d ? d : "");
        return 1;
    }

    std::string error;
    auto module = VST3::Hosting::Module::create(vst3Path, error);
    if (!module) {
        std::fprintf(stderr, "Failed to load '%s': %s\n", vst3Path.c_str(), error.c_str());
        XCloseDisplay(g_display);
        return 1;
    }

    auto optUid = VST3::UID::fromString(classIdHex, false);
    if (!optUid) {
        std::fprintf(stderr, "Invalid class ID: %s\n", classIdHex.c_str());
        XCloseDisplay(g_display);
        return 1;
    }
    auto uid = *optUid;

    auto& factory = module->getFactory();
    auto component = factory.createInstance<Vst::IComponent>(uid);
    if (!component) {
        std::fprintf(stderr, "Failed to create component\n");
        XCloseDisplay(g_display);
        return 1;
    }
    component->initialize(&g_hostApp);
    g_component = component;

    bool controllerIsComponent = false;
    if (component->queryInterface(Vst::IEditController::iid,
                                   reinterpret_cast<void**>(&g_controller)) == kResultOk) {
        controllerIsComponent = true;
    } else {
        TUID controllerCID;
        if (component->getControllerClassId(controllerCID) == kResultOk) {
            g_controller = factory.createInstance<Vst::IEditController>(
                VST3::UID::fromTUID(controllerCID));
            if (g_controller) g_controller->initialize(&g_hostApp);
        }
    }
    if (!g_controller) {
        std::fprintf(stderr, "Failed to get controller\n");
        component->terminate();
        g_component = nullptr;
        component = nullptr;
        XCloseDisplay(g_display);
        return 1;
    }

    // Connect component and controller via IConnectionPoint if present.
    // Required by some plugins (incl. JUCE-based ones like Surge XT) before
    // createView will succeed.
    IPtr<Vst::ConnectionProxy> compProxy, ctrlProxy;
    if (!controllerIsComponent) {
        FUnknownPtr<Vst::IConnectionPoint> compCP(g_component);
        FUnknownPtr<Vst::IConnectionPoint> ctrlCP(g_controller);
        if (compCP && ctrlCP) {
            compProxy = owned(new Vst::ConnectionProxy(compCP));
            ctrlProxy = owned(new Vst::ConnectionProxy(ctrlCP));
            compProxy->connect(ctrlCP);
            ctrlProxy->connect(compCP);
        }
    }

    // Transfer the component's state to the controller so parameter defaults line up.
    if (!controllerIsComponent) {
        MemoryStream stateStream;
        if (g_component->getState(&stateStream) == kResultOk) {
            int64 zero = 0;
            stateStream.seek(0, IBStream::kIBSeekSet, &zero);
            g_controller->setComponentState(&stateStream);
        }
    }

    g_controller->setComponentHandler(&g_componentHandler);

    std::fprintf(stderr, "[yawn_vst3_host] creating editor view\n");
    auto* rawView = g_controller->createView(Vst::ViewType::kEditor);
    if (!rawView) {
        std::fprintf(stderr, "[yawn_vst3_host] createView returned null\n");
        g_controller->setComponentHandler(nullptr);
        if (!controllerIsComponent) g_controller->terminate();
        component->terminate();
        g_controller = nullptr;
        g_component = nullptr;
        component = nullptr;
        XCloseDisplay(g_display);
        return 1;
    }
    auto plugView = owned(rawView);
    std::fprintf(stderr, "[yawn_vst3_host] view created\n");

    if (plugView->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultOk) {
        std::fprintf(stderr, "Plugin does not support X11EmbedWindowID\n");
        plugView = nullptr;
        g_controller->setComponentHandler(nullptr);
        if (!controllerIsComponent) g_controller->terminate();
        component->terminate();
        g_controller = nullptr;
        g_component = nullptr;
        component = nullptr;
        XCloseDisplay(g_display);
        return 1;
    }

    ViewRect viewRect = {};
    if (plugView->getSize(&viewRect) != kResultOk) {
        viewRect.right = 800; viewRect.bottom = 600;
    }
    int w = viewRect.getWidth();
    int h = viewRect.getHeight();
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;

    int screen = DefaultScreen(g_display);
    g_window = XCreateSimpleWindow(
        g_display, RootWindow(g_display, screen),
        0, 0, static_cast<unsigned>(w), static_cast<unsigned>(h), 0,
        BlackPixel(g_display, screen), BlackPixel(g_display, screen));

    XStoreName(g_display, g_window, windowTitle.c_str());
    g_wmDeleteAtom = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, g_window, &g_wmDeleteAtom, 1);
    XSelectInput(g_display, g_window,
                 ExposureMask | StructureNotifyMask | SubstructureNotifyMask);
    XMapWindow(g_display, g_window);
    XSync(g_display, False);  // ensure window is realized before plugin reparents into it

    std::fprintf(stderr, "[yawn_vst3_host] created X window id=0x%lx (%dx%d)\n",
                 (unsigned long)g_window, w, h);

    g_plugFrame = owned(new XPlugFrame(g_display, g_window));
    plugView->setFrame(g_plugFrame.get());

    std::fprintf(stderr, "[yawn_vst3_host] calling attached(X11EmbedWindowID)\n");
    auto attachResult = plugView->attached(
        reinterpret_cast<void*>(static_cast<uintptr_t>(g_window)),
        kPlatformTypeX11EmbedWindowID);
    std::fprintf(stderr, "[yawn_vst3_host] attached returned %d\n", (int)attachResult);
    if (attachResult != kResultOk) {
        std::fprintf(stderr, "Failed to attach view (result=%d)\n", static_cast<int>(attachResult));
        plugView->setFrame(nullptr);
        plugView = nullptr;
        g_plugFrame = nullptr;
        XDestroyWindow(g_display, g_window);
        g_controller->setComponentHandler(nullptr);
        if (!controllerIsComponent) g_controller->terminate();
        component->terminate();
        g_controller = nullptr;
        g_component = nullptr;
        component = nullptr;
        XCloseDisplay(g_display);
        return 1;
    }

    // Log child windows of our parent to see what the plugin placed inside
    {
        ::Window rootRet, parentRet;
        ::Window* children = nullptr;
        unsigned int nChildren = 0;
        if (XQueryTree(g_display, g_window, &rootRet, &parentRet, &children, &nChildren)) {
            std::fprintf(stderr, "[yawn_vst3_host] parent has %u children after attach\n", nChildren);
            for (unsigned int i = 0; i < nChildren; ++i) {
                XWindowAttributes attr = {};
                XGetWindowAttributes(g_display, children[i], &attr);
                std::fprintf(stderr, "[yawn_vst3_host]   child[%u]=0x%lx %dx%d map=%d\n",
                             i, (unsigned long)children[i], attr.width, attr.height,
                             attr.map_state);
                if (attr.map_state != IsViewable) {
                    std::fprintf(stderr, "[yawn_vst3_host]   mapping child[%u]\n", i);
                    XMapWindow(g_display, children[i]);
                }
            }
            if (children) XFree(children);
            XFlush(g_display);
        }
    }

    writePipe("READY\n", 6);

    int xfd = ConnectionNumber(g_display);

    // ── Main loop: poll X11, stdin, and plugin-registered FDs; fire timers ──
    while (!g_quit) {
        while (XPending(g_display)) {
            XEvent e;
            XNextEvent(g_display, &e);
            handleXEvent(e);
        }

        int64_t now = RunLoopImpl::nowMs();
        int64_t nextDeadline = now + 50;  // 50 ms ceiling so we re-poll regularly
        for (auto& t : g_runLoop.m_timers) {
            if (t.nextFireMs < nextDeadline) nextDeadline = t.nextFireMs;
        }
        int timeoutMs = static_cast<int>(std::max<int64_t>(0, nextDeadline - now));

        auto handlersSnapshot = g_runLoop.m_eventHandlers;

        std::vector<pollfd> fds;
        fds.reserve(2 + handlersSnapshot.size());
        fds.push_back({xfd,       POLLIN, 0});
        fds.push_back({g_stdinFd, POLLIN, 0});
        for (auto& eh : handlersSnapshot) fds.push_back({eh.fd, POLLIN, 0});

        int n = ::poll(fds.data(), fds.size(), timeoutMs);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            while (XPending(g_display)) {
                XEvent e;
                XNextEvent(g_display, &e);
                handleXEvent(e);
            }
        }

        if (fds[1].revents & POLLIN) {
            readStdinAndDispatch();
        } else if (fds[1].revents & (POLLHUP | POLLERR)) {
            g_quit = true;
        }

        for (size_t i = 0; i < handlersSnapshot.size(); ++i) {
            if (!(fds[i + 2].revents & POLLIN)) continue;
            auto& snap = handlersSnapshot[i];
            bool stillReg = false;
            for (auto& cur : g_runLoop.m_eventHandlers) {
                if (cur.handler == snap.handler && cur.fd == snap.fd) { stillReg = true; break; }
            }
            if (stillReg) {
                static int nFires = 0;
                if (++nFires <= 10)
                    std::fprintf(stderr, "[yawn_vst3_host] firing onFDIsSet fd=%d (#%d)\n",
                                 (int)snap.fd, nFires);
                snap.handler->onFDIsSet(snap.fd);
            }
        }

        now = RunLoopImpl::nowMs();
        std::vector<RunLoopImpl::Timer> due;
        for (auto& t : g_runLoop.m_timers) {
            if (t.nextFireMs <= now) {
                due.push_back(t);
                t.nextFireMs = now + static_cast<int64_t>(t.intervalMs);
            }
        }
        for (auto& t : due) {
            bool stillReg = false;
            for (auto& cur : g_runLoop.m_timers) {
                if (cur.handler == t.handler) { stillReg = true; break; }
            }
            if (stillReg) t.handler->onTimer();
        }
    }

    // ── Cleanup ──
    if (plugView) {
        plugView->removed();
        plugView->setFrame(nullptr);
        plugView = nullptr;
    }
    g_plugFrame = nullptr;

    if (g_window) {
        XDestroyWindow(g_display, g_window);
        g_window = 0;
    }

    g_controller->setComponentHandler(nullptr);
    if (!controllerIsComponent) g_controller->terminate();
    component->terminate();
    g_controller = nullptr;
    g_component = nullptr;
    component = nullptr;
    module = nullptr;

    XCloseDisplay(g_display);
    g_display = nullptr;

    return 0;
}

#else
// Other platforms stub
#include <cstdio>
int main() {
    fprintf(stderr, "VST3 editor host not supported on this platform yet\n");
    return 1;
}
#endif
