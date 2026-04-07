#ifdef YAWN_HAS_VST3

#include "vst3/VST3EditorWindow.h"
#include "vst3/VST3Host.h"
#include "util/Logger.h"

#ifdef _WIN32
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#endif

namespace yawn {
namespace vst3 {

VST3EditorWindow::~VST3EditorWindow() {
    close();
}

#ifdef _WIN32

bool VST3EditorWindow::open(VST3PluginInstance* instance,
                             const std::string& modulePath,
                             const std::string& classID,
                             const std::string& title)
{
    if (isOpen()) close();
    if (!instance) return false;

    m_instance = instance;

    // Find yawn_vst3_host.exe next to YAWN.exe
    wchar_t exeDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    PathRemoveFileSpecW(exeDir);

    std::wstring hostExe(exeDir);
    hostExe += L"\\yawn_vst3_host.exe";

    // Build command line with proper quoting
    std::string cmdLine = "\"";
    // Convert hostExe to UTF-8 for the command line
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, hostExe.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string hostExeUtf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, hostExe.c_str(), -1, &hostExeUtf8[0], utf8Len, nullptr, nullptr);
    // Remove trailing null
    if (!hostExeUtf8.empty() && hostExeUtf8.back() == '\0')
        hostExeUtf8.pop_back();

    cmdLine = "\"" + hostExeUtf8 + "\" ";
    cmdLine += "\"" + modulePath + "\" ";
    cmdLine += "\"" + classID + "\" ";
    cmdLine += "\"" + title + "\"";

    // Convert full command line to wide string
    int wLen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    std::wstring wCmdLine(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, &wCmdLine[0], wLen);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = CreateProcessW(
        hostExe.c_str(),
        &wCmdLine[0],
        nullptr, nullptr, FALSE,
        0,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) {
        LOG_ERROR("VST3", "Failed to launch editor process for '%s' (error=%lu)",
                  title.c_str(), GetLastError());
        m_instance = nullptr;
        return false;
    }

    m_process = pi.hProcess;
    m_thread_handle = pi.hThread;

    LOG_INFO("VST3", "Editor process launched for '%s' (PID=%lu)",
             title.c_str(), pi.dwProcessId);
    return true;
}

void VST3EditorWindow::close() {
    if (m_process) {
        // Give the process a chance to close gracefully
        if (WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(m_process, 0);
            WaitForSingleObject(m_process, 2000);
        }
        CloseHandle(m_process);
        m_process = nullptr;
    }
    if (m_thread_handle) {
        CloseHandle(m_thread_handle);
        m_thread_handle = nullptr;
    }
    m_instance = nullptr;
}

bool VST3EditorWindow::isOpen() const {
    if (!m_process) return false;
    return WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
}

#else

bool VST3EditorWindow::open(VST3PluginInstance*, const std::string&,
                             const std::string&, const std::string&)
{
    LOG_WARN("VST3", "Plugin editor not yet supported on this platform");
    return false;
}

void VST3EditorWindow::close() {}

bool VST3EditorWindow::isOpen() const { return false; }

#endif // _WIN32

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
