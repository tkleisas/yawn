#pragma once

#include <filesystem>
#include <string>
#include <utility>

namespace yawn {

// Some external sample browsers offer the exported filename as text as well
// as CF_HDROP. SDL can select the text format first. Only accept existing
// local files; arbitrary text and remote URLs must never become audio drops.
inline std::string droppedFilePath(std::string path) {
    const auto begin = path.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    path = path.substr(begin, path.find_last_not_of(" \t\r\n") - begin + 1);
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
    if (path.find_first_of("\r\n") != std::string::npos) return {};
    if (path.compare(0, 7, "file://") == 0) {
        path.erase(0, 7);
        if (path.compare(0, 10, "localhost/") == 0) path.erase(0, 9);
        if (path.empty() || path.front() != '/') return {}; // remote host
        std::string decoded;
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] != '%') { decoded += path[i]; continue; }
            if (i + 2 >= path.size()) return {};
            const int high = hex(path[i + 1]), low = hex(path[i + 2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0)) return {};
            decoded += static_cast<char>(high * 16 + low);
            i += 2;
        }
        path = std::move(decoded);
#ifdef _WIN32
        if (path.size() >= 3 && path[0] == '/' && path[2] == ':') path.erase(0, 1);
#endif
    }
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::u8path(path), error)
        ? path : std::string{};
}
} // namespace yawn
