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

    // ── Create anonymous pipes for bidirectional IPC ──
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hChildStdinRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdoutWr = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&hChildStdinRd, &m_pipeWrite, &sa, 0) ||
        !CreatePipe(&m_pipeRead, &hChildStdoutWr, &sa, 0)) {
        LOG_ERROR("VST3", "Failed to create pipes for editor process");
        m_instance = nullptr;
        return false;
    }

    // Parent's ends must NOT be inheritable
    SetHandleInformation(m_pipeWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_pipeRead, HANDLE_FLAG_INHERIT, 0);

    // ── Find yawn_vst3_host.exe next to YAWN.exe ──
    wchar_t exeDir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    PathRemoveFileSpecW(exeDir);

    std::wstring hostExe(exeDir);
    hostExe += L"\\yawn_vst3_host.exe";

    // Build command line
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, hostExe.c_str(), -1,
                                       nullptr, 0, nullptr, nullptr);
    std::string hostExeUtf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, hostExe.c_str(), -1,
                         &hostExeUtf8[0], utf8Len, nullptr, nullptr);
    if (!hostExeUtf8.empty() && hostExeUtf8.back() == '\0')
        hostExeUtf8.pop_back();

    std::string cmdLine = "\"" + hostExeUtf8 + "\" "
                        + "\"" + modulePath + "\" "
                        + "\"" + classID + "\" "
                        + "\"" + title + "\"";

    int wLen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    std::wstring wCmdLine(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, &wCmdLine[0], wLen);

    // ── Launch child with redirected stdin/stdout ──
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hChildStdinRd;
    si.hStdOutput = hChildStdoutWr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        hostExe.c_str(), &wCmdLine[0],
        nullptr, nullptr, TRUE,  // bInheritHandles = TRUE
        0, nullptr, nullptr,
        &si, &pi);

    // Close child-side pipe handles in parent (child inherited them)
    CloseHandle(hChildStdinRd);
    CloseHandle(hChildStdoutWr);

    if (!ok) {
        LOG_ERROR("VST3", "Failed to launch editor process for '%s' (error=%lu)",
                  title.c_str(), GetLastError());
        CloseHandle(m_pipeRead);  m_pipeRead = INVALID_HANDLE_VALUE;
        CloseHandle(m_pipeWrite); m_pipeWrite = INVALID_HANDLE_VALUE;
        m_instance = nullptr;
        return false;
    }

    m_process = pi.hProcess;
    m_processThread = pi.hThread;

    LOG_INFO("VST3", "Editor process launched for '%s' (PID=%lu)",
             title.c_str(), pi.dwProcessId);
    return true;
}

void VST3EditorWindow::close() {
    // Send CLOSE command if pipe is still open
    if (m_pipeWrite != INVALID_HANDLE_VALUE) {
        const char cmd[] = "CLOSE\n";
        DWORD written;
        WriteFile(m_pipeWrite, cmd, sizeof(cmd) - 1, &written, nullptr);
    }

    if (m_process) {
        // Wait up to 3 seconds for graceful exit
        if (WaitForSingleObject(m_process, 3000) == WAIT_TIMEOUT) {
            TerminateProcess(m_process, 0);
            WaitForSingleObject(m_process, 1000);
        }
        CloseHandle(m_process);
        m_process = nullptr;
    }
    if (m_processThread) {
        CloseHandle(m_processThread);
        m_processThread = nullptr;
    }
    if (m_pipeRead != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeRead);
        m_pipeRead = INVALID_HANDLE_VALUE;
    }
    if (m_pipeWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeWrite);
        m_pipeWrite = INVALID_HANDLE_VALUE;
    }
    m_readBuffer.clear();
    m_instance = nullptr;
}

bool VST3EditorWindow::isOpen() const {
    if (!m_process) return false;
    return WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
}

void VST3EditorWindow::pollParamChanges() {
    if (m_pipeRead == INVALID_HANDLE_VALUE) return;

    DWORD available = 0;
    if (!PeekNamedPipe(m_pipeRead, nullptr, 0, nullptr, &available, nullptr) || available == 0)
        return;

    char buf[4096];
    DWORD toRead = (available < sizeof(buf) - 1) ? available : sizeof(buf) - 1;
    DWORD bytesRead = 0;
    if (!ReadFile(m_pipeRead, buf, toRead, &bytesRead, nullptr) || bytesRead == 0)
        return;

    m_readBuffer.append(buf, bytesRead);

    // Process complete lines
    size_t pos;
    while ((pos = m_readBuffer.find('\n')) != std::string::npos) {
        std::string line = m_readBuffer.substr(0, pos);
        m_readBuffer.erase(0, pos + 1);
        processLine(line);
    }
}

void VST3EditorWindow::processLine(const std::string& line) {
    if (line.size() > 6 && line[0] == 'P' && line[1] == 'A') {
        // "PARAM <id> <value>"
        unsigned rawId = 0;
        double value = 0.0;
        if (sscanf(line.c_str() + 6, "%u %lf", &rawId, &value) == 2) {
            if (m_instance) {
                m_instance->setParameterNormalized(
                    static_cast<Steinberg::Vst::ParamID>(rawId), value);
            }
        }
    } else if (line.size() > 6 && line[0] == 'S' && line[1] == 'T') {
        // "STATE <hex>"
        std::string hex = line.substr(6);
        std::vector<uint8_t> data;
        data.reserve(hex.size() / 2);
        for (size_t i = 0; i + 1 < hex.size(); i += 2) {
            unsigned byte = 0;
            if (sscanf(hex.c_str() + i, "%2x", &byte) == 1) {
                data.push_back(static_cast<uint8_t>(byte));
            }
        }
        if (m_instance && !data.empty()) {
            LOG_INFO("VST3", "Applying preset state (%zu bytes)", data.size());
            m_instance->setProcessorState(data);
        }
    } else if (line == "READY") {
        LOG_INFO("VST3", "Editor process ready");
    }
}

void VST3EditorWindow::sendParamChange(unsigned int paramId, double value) {
    if (m_pipeWrite == INVALID_HANDLE_VALUE) return;
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "PARAM %u %.15g\n", paramId, value);
    DWORD written;
    WriteFile(m_pipeWrite, buf, static_cast<DWORD>(len), &written, nullptr);
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
void VST3EditorWindow::pollParamChanges() {}
void VST3EditorWindow::sendParamChange(unsigned int, double) {}

#endif // _WIN32

} // namespace vst3
} // namespace yawn

#endif // YAWN_HAS_VST3
