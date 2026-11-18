#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

// Remembers, per video file, how many quarter turns (clockwise) it must be rotated.
// Stored in video_rotations.json next to the app; looked up whenever a clip is loaded.
namespace VideoRotationStore {

inline const char* fileName() { return "video_rotations.json"; }

inline std::string key(const std::string& path) {
    std::string k = std::filesystem::path(path).lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return k;
}

inline std::map<std::string, int>& table() {
    static std::map<std::string, int> t;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            std::ifstream f(fileName());
            if (f) {
                nlohmann::json j;
                f >> j;
                for (auto it = j.begin(); it != j.end(); ++it) {
                    if (it.value().is_number_integer()) t[it.key()] = it.value().get<int>() & 3;
                }
            }
        } catch (...) {
            // corrupt file: start empty
        }
    }
    return t;
}

// Quarter turns clockwise (0..3) saved for this clip, 0 when none.
inline int get(const std::string& path) {
    if (path.empty()) return 0;
    auto& t = table();
    auto it = t.find(key(path));
    return it == t.end() ? 0 : it->second;
}

inline void set(const std::string& path, int quarterTurns) {
    if (path.empty()) return;
    auto& t = table();
    quarterTurns &= 3;
    if (quarterTurns == 0) t.erase(key(path)); else t[key(path)] = quarterTurns;
    try {
        nlohmann::json j = nlohmann::json::object();
        for (const auto& kv : t) j[kv.first] = kv.second;
        std::ofstream f(fileName());
        f << j.dump(2);
    } catch (...) {
        // best effort
    }
}

} // namespace VideoRotationStore
