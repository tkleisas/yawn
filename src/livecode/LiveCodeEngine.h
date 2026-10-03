#pragma once

// LiveCodeEngine — owns the live-coding lua_State: sandboxed stdlib,
// instruction-budget hook, generation-tagged improv.* scheduling API and
// the yawn.* performer subset, plus yawn.state migration across state
// recreation. See docs/live-coding.md §3.
//
// Lifecycle nuance (§3.3): the lua_State persists across RUN gestures —
// re-running a script re-executes its body in the same state, so locals
// and closures survive (the classic live-coding cycle). The state is only
// recreated on startup / shutdown / explicit reload; yawn.state shallow
// values are migrated across recreation by the manager.

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "livecode/LiveCodeSong.h"

namespace yawn {
namespace livecode {

class LiveCodeManager;  // forward

class LiveCodeEngine {
public:
    // A migrated yawn.state entry: number / string / boolean, or a one-level
    // table of primitives (string or integer keys).
    struct StateVal {
        enum class Kind : uint8_t { Nil, Bool, Num, Str, Table };
        Kind kind = Kind::Nil;
        bool b = false;
        double num = 0.0;
        std::string str;
        // One-level table: key string (or "" for integer key with index
        // stored in num), value primitive.
        std::vector<std::pair<std::string, StateVal>> table;
        int intKey = -1;  // >= 0 when the key was an array index
    };
    using StateMap = std::map<std::string, StateVal>;

    LiveCodeEngine() = default;
    ~LiveCodeEngine() { shutdown(); }

    LiveCodeEngine(const LiveCodeEngine&) = delete;
    LiveCodeEngine& operator=(const LiveCodeEngine&) = delete;

    bool init(LiveCodeManager* mgr);
    void shutdown();

    bool valid() const { return m_L != nullptr; }
    lua_State* state() const { return m_L; }

    uint32_t generation() const { return m_generation; }
    void setGeneration(uint32_t g) { m_generation = g; }

    // Execute a file body as the current generation's program.
    bool runFile(const std::string& path);

    // Execute a source string (in-app editor evaluates the buffer as a
    // new generation — same semantics as runFile minus file I/O).
    bool runString(const std::string& code);

    // Harvest the declarative `song` global table (§5.1) into a model.
    // Returns true when there is no song table (nothing to apply) or
    // parsing succeeded; false with `err` on malformed input.
    bool harvestSong(SongModel& out, std::string& err) const;

    // Call a registry-ref'd function with one numeric argument (scheduler
    // dispatch). Protected (pcall + budget); errors go to the manager's
    // console, never unwind into the scheduler. Returns false on error so
    // the manager can track per-entry failure streaks.
    bool callRef(int ref, double arg);

    // Registry-ref helpers (UI thread only).
    int  refFunction(lua_State* L);          // ref the function at stack top
    void unrefFunction(int ref);

    // yawn.state harvest / inject (state recreation).
    StateMap harvestState() const;
    void injectState(const StateMap& harvested);

private:
    void registerAPI();
    bool protectedCall(int nargs, int budgetInstructions);
    void pushConsoleError(const std::string& what);

    lua_State*       m_L = nullptr;
    LiveCodeManager* m_mgr = nullptr;
    uint32_t         m_generation = 0;
};

} // namespace livecode
} // namespace yawn
