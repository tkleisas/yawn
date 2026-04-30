#pragma once
// json.hpp shim — exists solely so NAM's `#include "json.hpp"` lines
// resolve to the same nlohmann::json YAWN already pulls in via
// FetchContent. Without this, NAM expects its own bundled json.hpp
// at NAM/Dependencies/nlohmann/json.hpp (which only exists when
// NAM is cloned --recursive — FetchContent doesn't do submodules).
//
// Single source of truth for the JSON library across the whole
// build, no version drift, no duplicated symbols.
#include <nlohmann/json.hpp>
