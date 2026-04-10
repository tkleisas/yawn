#pragma once

// LuaEngine — owns a lua_State, registers the yawn.* API, loads and calls
// controller scripts. Each controller gets its own LuaEngine instance.

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <string>
#include <cstdint>

namespace yawn {
namespace controllers {

class ControllerManager;  // forward

class LuaEngine {
public:
    LuaEngine() = default;
    ~LuaEngine() { shutdown(); }

    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    // Create lua_State, register yawn.* API, open standard libs
    bool init(ControllerManager* mgr);

    // Destroy lua_State
    void shutdown();

    // Load and execute a Lua file
    bool loadFile(const std::string& path);

    // Call global Lua callbacks (safe — does nothing if function doesn't exist)
    bool callOnConnect();
    bool callOnDisconnect();
    bool callOnMidi(const uint8_t* data, int length);
    bool callOnTick();

    lua_State* state() const { return m_L; }

private:
    // Register the yawn.* API table
    void registerAPI();

    // Helper: call a global function with no args and no return values
    bool callGlobal(const char* name);

    lua_State* m_L = nullptr;
    ControllerManager* m_manager = nullptr;
};

} // namespace controllers
} // namespace yawn
