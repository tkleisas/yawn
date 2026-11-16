#pragma once

// GeneratorEngine — hosts the Lua composition layer in an isolated,
// sandboxed lua_State. Scripts are found under an ordered list of roots
// (project, user, bundled); require() is reimplemented by the host so
// only .lua files under those roots can load, and io/os/package are not
// exposed to scripts. Output is validated before it reaches the app.
//
// See docs/generative-song-design.md.

#include "generate/GeneratorTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct lua_State;

namespace yawn {
namespace generate {

class GeneratorEngine {
public:
    GeneratorEngine();
    ~GeneratorEngine();

    GeneratorEngine(const GeneratorEngine&) = delete;
    GeneratorEngine& operator=(const GeneratorEngine&) = delete;

    // Highest priority first, e.g.
    //   { <project>/generator, ~/.yawn/generator, <bundled>/assets/generator }
    void setScriptRoots(std::vector<std::filesystem::path> roots);
    const std::vector<std::filesystem::path>& scriptRoots() const {
        return m_roots;
    }

    // Creates the Lua state and loads `init.lua`. Errors are reported in
    // *error and leave the engine not-ready.
    bool reload(std::string* error = nullptr);

    bool ready() const { return m_ready; }

    // Runs init.generate(params). On success `out` is fully validated.
    // On failure `out` is untouched and *error carries the Lua
    // traceback or a validation message.
    bool generate(const GeneratorParams& params, GeneratedSong& out,
                  std::string* error = nullptr);

    // First script root that contains init.lua (empty when none).
    std::string scriptsDir() const;

    // Exposed for tests: deterministic RNG unit value.
    double rngUnit();

private:
    void closeState();
    bool initState(std::string* error);
    void pushParams(const GeneratorParams& params);
    bool readSong(GeneratedSong& out, std::string* error);
    std::string tracebackFromTop(const char* context);

    std::vector<std::filesystem::path> m_roots;
    lua_State* m_L = nullptr;
    uint64_t m_rngState = 1;
    bool m_ready = false;
};

} // namespace generate
} // namespace yawn
