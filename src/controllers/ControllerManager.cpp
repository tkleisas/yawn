#include "controllers/ControllerManager.h"
#include "util/Logger.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace yawn {
namespace controllers {

void ControllerManager::init(audio::AudioEngine* engine, Project* project) {
    m_audioEngine = engine;
    m_project = project;
}

void ControllerManager::shutdown() {
    disconnect();
    m_scripts.clear();
    m_audioEngine = nullptr;
    m_project = nullptr;
}

void ControllerManager::scanScripts(const std::string& bundledPath) {
    m_bundledPath = bundledPath;
    m_scripts.clear();

    auto scanDir = [this](const std::string& basePath) {
        std::error_code ec;
        if (!fs::is_directory(basePath, ec)) return;

        for (auto& entry : fs::directory_iterator(basePath, ec)) {
            if (!entry.is_directory()) continue;

            std::string dir = entry.path().string();
            ControllerScript script;
            if (loadManifest(dir, script)) {
                script.scriptDir = dir;
                LOG_INFO("Controller", "Found script: %s (%s)", script.name.c_str(), dir.c_str());
                m_scripts.push_back(std::move(script));
            }
        }
    };

    scanDir(bundledPath);
    LOG_INFO("Controller", "Scanned %d controller script(s)", static_cast<int>(m_scripts.size()));
}

bool ControllerManager::loadManifest(const std::string& dir, ControllerScript& out) {
    std::string manifestPath = dir + "/manifest.lua";
    std::error_code ec;
    if (!fs::exists(manifestPath, ec)) {
        LOG_WARN("Controller", "No manifest.lua in %s", dir.c_str());
        return false;
    }

    // Create a temporary Lua state to execute the manifest
    lua_State* L = luaL_newstate();
    if (!L) return false;

    luaL_openlibs(L);

    bool ok = false;
    int rc = luaL_dofile(L, manifestPath.c_str());
    if (rc == LUA_OK) {
        // manifest.lua should return a table.
        if (!lua_istable(L, -1)) {
            LOG_WARN("Controller", "Manifest %s did not return a table (top type: %s)",
                     manifestPath.c_str(), luaL_typename(L, -1));
        } else {
            lua_getfield(L, -1, "name");
            if (lua_isstring(L, -1)) out.name = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "author");
            if (lua_isstring(L, -1)) out.author = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "version");
            if (lua_isstring(L, -1)) out.version = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "input_port_match");
            if (lua_isstring(L, -1)) out.inputPortMatch = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, -1, "output_port_match");
            if (lua_isstring(L, -1)) out.outputPortMatch = lua_tostring(L, -1);
            lua_pop(L, 1);

            ok = !out.name.empty() && !out.inputPortMatch.empty();
            if (!ok) {
                LOG_WARN("Controller",
                    "Manifest %s missing required fields "
                    "(name='%s', input_port_match='%s')",
                    manifestPath.c_str(),
                    out.name.c_str(), out.inputPortMatch.c_str());
            }
        }
    } else {
        const char* msg = lua_tostring(L, -1);
        LOG_WARN("Controller", "Failed to load manifest %s (rc=%d): %s",
                 manifestPath.c_str(), rc, msg ? msg : "<no error message>");
    }

    lua_close(L);
    return ok;
}

void ControllerManager::autoConnect() {
    if (m_scripts.empty()) return;

    auto inputPorts = ControllerMidiPort::enumerateInputPorts();
    auto outputPorts = ControllerMidiPort::enumerateOutputPorts();

    for (int si = 0; si < static_cast<int>(m_scripts.size()); ++si) {
        auto& script = m_scripts[si];
        if (script.connected) continue;

        // Find ALL matching input ports (Push 1 has main + user port)
        std::vector<int> inIndices;
        for (int i = 0; i < static_cast<int>(inputPorts.size()); ++i) {
            if (inputPorts[i].find(script.inputPortMatch) != std::string::npos)
                inIndices.push_back(i);
        }

        // Find ALL matching output ports
        std::vector<int> outIndices;
        if (!script.outputPortMatch.empty()) {
            for (int i = 0; i < static_cast<int>(outputPorts.size()); ++i) {
                if (outputPorts[i].find(script.outputPortMatch) != std::string::npos)
                    outIndices.push_back(i);
            }
        }

        if (inIndices.empty()) continue;  // no matching input port

        // Open ports — open all matching inputs and outputs
        m_port = std::make_unique<ControllerMidiPort>();
        bool anyInput = false;
        for (int idx : inIndices) {
            if (m_port->openInput(idx))
                anyInput = true;
        }
        if (!anyInput) {
            m_port.reset();
            continue;
        }
        for (int idx : outIndices)
            m_port->openOutput(idx);

        // Create Lua engine and load script
        m_lua = std::make_unique<LuaEngine>();
        if (!m_lua->init(this)) {
            m_lua.reset();
            m_port.reset();
            continue;
        }

        std::string initPath = script.scriptDir + "/init.lua";
        if (!m_lua->loadFile(initPath)) {
            m_lua.reset();
            m_port.reset();
            continue;
        }

        m_activeScriptIndex = si;
        script.connected = true;
        m_lua->callOnConnect();

        LOG_INFO("Controller", "Connected: %s (in: %s, out: %s)",
                 script.name.c_str(),
                 m_port->inputName().c_str(),
                 m_port->isOutputOpen() ? m_port->outputName().c_str() : "none");

        break;  // Only one controller at a time for now
    }
}

void ControllerManager::disconnect() {
    if (m_lua) {
        try { m_lua->callOnDisconnect(); } catch (...) {}
        m_lua->shutdown();
        m_lua.reset();
    }
    if (m_port) {
        try { m_port->close(); } catch (...) {}
        m_port.reset();
    }
    if (m_activeScriptIndex >= 0 && m_activeScriptIndex < static_cast<int>(m_scripts.size())) {
        m_scripts[m_activeScriptIndex].connected = false;
    }
    m_activeScriptIndex = -1;
}

void ControllerManager::update() {
    if (!m_lua || !m_port) return;

    // Drain MIDI input and call on_midi for each message
    m_pendingMessages.clear();
    m_port->readRawMessages(m_pendingMessages);

    for (auto& msg : m_pendingMessages) {
        m_lua->callOnMidi(msg.data(), static_cast<int>(msg.size()));
    }

    // Call on_tick at ~30Hz (every other frame)
    if (++m_tickCounter >= 2) {
        m_tickCounter = 0;
        m_lua->callOnTick();
    }
}

void ControllerManager::reloadScripts(const std::string& bundledPath) {
    std::string path = bundledPath.empty() ? m_bundledPath : bundledPath;
    LOG_INFO("Controller", "Reloading controller scripts...");
    disconnect();
    scanScripts(path);
    autoConnect();
}

void ControllerManager::sendMidiToController(const uint8_t* data, int length) {
    if (m_port && m_port->isOutputOpen())
        m_port->sendRawBytes(data, length);
}

void ControllerManager::sendCommand(const audio::AudioCommand& cmd) {
    if (m_sendCommand) m_sendCommand(cmd);
}

bool ControllerManager::isConnected() const {
    return m_activeScriptIndex >= 0 && m_lua && m_port;
}

const std::string& ControllerManager::connectedName() const {
    static const std::string kEmpty;
    if (m_activeScriptIndex >= 0 && m_activeScriptIndex < static_cast<int>(m_scripts.size()))
        return m_scripts[m_activeScriptIndex].name;
    return kEmpty;
}

std::vector<std::string> ControllerManager::claimedInputPortNames() const {
    if (m_port && m_port->isInputOpen())
        return m_port->inputNames();
    return {};
}

std::vector<std::string> ControllerManager::claimedOutputPortNames() const {
    if (m_port && m_port->isOutputOpen())
        return m_port->outputNames();
    return {};
}

} // namespace controllers
} // namespace yawn
