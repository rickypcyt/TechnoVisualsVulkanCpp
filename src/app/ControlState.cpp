#include "ControlState.h"
#include "OscSystem.h"

#include <glm/glm.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <deque>

// VideoRandomizerState definition (moved from main.cpp)
struct VideoRandomizerState {
    bool autoRandomize = false;
    bool useVideoDuration = false;
    float intervalSeconds = 30.0f;
    float elapsedSeconds = 0.0f;
    float currentVideoDuration = 0.0f;
    int historyWindow = 3;
    std::deque<int> recentHistory;
    bool useShuffleMode = true;
    std::vector<int> shuffleQueue;
    int currentShuffleIndex = 0;
};

// VideoRandomizerState2 definition
struct VideoRandomizerState2 {
    bool autoRandomize = false;
    bool useVideoDuration = false;
    float intervalSeconds = 30.0f;
    float elapsedSeconds = 0.0f;
    float currentVideoDuration = 0.0f;
    bool useShuffleMode = true;
    std::vector<int> shuffleQueue;
    int currentShuffleIndex = 0;
};

// -----------------------------------------------------------------------
// Helpers internos (solo visibles en este .cpp)
// -----------------------------------------------------------------------
namespace {

using KVMap = std::unordered_map<std::string, std::string>;

KVMap parseFile(std::ifstream& file) {
    KVMap kv;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            kv[key] = value;
        }
    }
    return kv;
}

float readFloat(const KVMap& kv, const std::string& key, float def) {
    auto it = kv.find(key);
    if (it == kv.end())
        return def;
    try {
        return std::stof(it->second);
    } catch (...) {
        return def;
    }
}

int readInt(const KVMap& kv, const std::string& key, int def) {
    auto it = kv.find(key);
    if (it == kv.end())
        return def;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return def;
    }
}

bool readBool(const KVMap& kv, const std::string& key, bool def) {
    auto it = kv.find(key);
    if (it == kv.end())
        return def;
    std::string val = it->second;
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return (val == "true" || val == "1" || val == "yes");
}

std::string readString(const KVMap& kv, const std::string& key, const std::string& def) {
    auto it = kv.find(key);
    if (it == kv.end())
        return def;
    return it->second;
}

glm::vec3 readVec3(const KVMap& kv, const std::string& key, const glm::vec3& def) {
    auto it = kv.find(key);
    if (it == kv.end())
        return def;
    try {
        std::istringstream iss(it->second);
        float x, y, z;
        char sep;
        if (iss >> x >> sep >> y >> sep >> z) {
            return glm::vec3(x, y, z);
        }
    } catch (...) {}
    return def;
}

glm::vec4 readVec4(const KVMap& kv, const std::string& key, const glm::vec4& def) {
    auto it = kv.find(key);
    if (it == kv.end())
        return def;
    try {
        std::istringstream iss(it->second);
        float x, y, z, w;
        char sep;
        if (iss >> x >> sep >> y >> sep >> z >> sep >> w) {
            return glm::vec4(x, y, z, w);
        }
    } catch (...) {}
    return def;
}

} // namespace anónimo

// -----------------------------------------------------------------------
// ControlState::load
// -----------------------------------------------------------------------
void ControlState::load(
    const std::string&    path,
    VideoRandomizerState& r,
    VideoRandomizerState2& r2,
    VideoRandomizerState2& r3,
    bool&                 allowDimensionChangeRecreation,
    OscSystem&            oscSystem,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2,
    int&                  selectedVideoAsset3,
    std::string&          videoSourcePath,
    std::string&          videoSourcePath2,
    std::string&          videoSourcePath3)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        oscSystem.ensureDefaultTriggers();
        return;
    }

    const KVMap kv = parseFile(file);

    oscSystem.clearMappings();
    oscSystem.clearTriggerMappings();

    oscSystem.ensureDefaultTriggers();

    for (const auto& [key, value] : kv) {
        if (key.find("osc_mapping_") == 0) {
            std::string oscAddress = key.substr(12);
            try {
                std::istringstream iss(value);
                std::string paramName;
                float minVal, maxVal;
                int invert;
                char comma;
                if (std::getline(iss, paramName, ',') &&
                    (iss >> minVal >> comma >> maxVal >> comma >> invert)) {
                    oscSystem.addMapping(oscAddress, paramName, minVal, maxVal, invert != 0);
                }
            } catch (...) {}
        }
    }

    for (const auto& [key, value] : kv) {
        if (key.find("osc_trigger_") == 0) {
            std::string oscAddress = key.substr(12);
            try {
                oscSystem.addTriggerMapping(oscAddress, value);
            } catch (...) {}
        }
    }

    VideoRandomizerState rLoaded = r;
    rLoaded.autoRandomize    = readBool (kv, "autoRandomize",    rLoaded.autoRandomize);
    rLoaded.intervalSeconds  = readFloat(kv, "intervalSeconds",  rLoaded.intervalSeconds);
    rLoaded.useVideoDuration = readBool (kv, "useVideoDuration", rLoaded.useVideoDuration);
    rLoaded.useShuffleMode   = readBool (kv, "useShuffleMode",   rLoaded.useShuffleMode);
    
    VideoRandomizerState2 r2Loaded = r2;
    r2Loaded.autoRandomize    = readBool (kv, "autoRandomize2",    r2Loaded.autoRandomize);
    r2Loaded.intervalSeconds  = readFloat(kv, "intervalSeconds2",  r2Loaded.intervalSeconds);
    r2Loaded.useVideoDuration = readBool (kv, "useVideoDuration2", r2Loaded.useVideoDuration);
    r2Loaded.useShuffleMode   = readBool (kv, "useShuffleMode2",   r2Loaded.useShuffleMode);

    VideoRandomizerState2 r3Loaded = r3;
    r3Loaded.autoRandomize    = readBool (kv, "autoRandomize3",    r3Loaded.autoRandomize);
    r3Loaded.intervalSeconds  = readFloat(kv, "intervalSeconds3",  r3Loaded.intervalSeconds);
    r3Loaded.useVideoDuration = readBool (kv, "useVideoDuration3", r3Loaded.useVideoDuration);
    r3Loaded.useShuffleMode   = readBool (kv, "useShuffleMode3",   r3Loaded.useShuffleMode);

    allowDimensionChangeRecreation = readBool(kv, "allowDimensionChangeRecreation", allowDimensionChangeRecreation);
    selectedVideoAsset  = readInt(kv, "selectedVideoAsset",  selectedVideoAsset);
    selectedVideoAsset2 = readInt(kv, "selectedVideoAsset2", selectedVideoAsset2);
    selectedVideoAsset3 = readInt(kv, "selectedVideoAsset3", selectedVideoAsset3);
    videoSourcePath     = readString(kv, "videoSourcePath",  videoSourcePath);
    videoSourcePath2    = readString(kv, "videoSourcePath2", videoSourcePath2);
    videoSourcePath3    = readString(kv, "videoSourcePath3", videoSourcePath3);

    std::cout << "[ControlState::load] Loaded V1 idx=" << selectedVideoAsset
              << " path='" << videoSourcePath << "'\n";
    std::cout << "[ControlState::load] Loaded V2 idx=" << selectedVideoAsset2
              << " path='" << videoSourcePath2 << "'\n";
    std::cout << "[ControlState::load] Loaded V3 idx=" << selectedVideoAsset3
              << " path='" << videoSourcePath3 << "'\n";

    r = rLoaded;
    r2 = r2Loaded;
    r3 = r3Loaded;
    r.currentVideoDuration = 0.0f;
    r.recentHistory.clear();
    r.elapsedSeconds = 0.0f;
}

// -----------------------------------------------------------------------
// ControlState::save
// -----------------------------------------------------------------------
void ControlState::save(
    const std::string&        path,
    const VideoRandomizerState& r,
    const VideoRandomizerState2& r2,
    const VideoRandomizerState2& r3,
    bool                      allowDimensionChangeRecreation,
    const OscSystem&          oscSystem,
    int                       selectedVideoAsset,
    int                       selectedVideoAsset2,
    int                       selectedVideoAsset3,
    const std::string&        videoSourcePath,
    const std::string&        videoSourcePath2,
    const std::string&        videoSourcePath3)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[ControlState] Cannot open for writing: " << path << std::endl;
        return;
    }

    file << "# VJAY Control State - Key=Value\n"
         << "# VisualControls now handled by ParameterRegistry/JsonSerializer\n"
         << "# MIDI mappings now handled separately by MidiSystem\n\n";

    const auto& oscMappings = oscSystem.getMappings();
    for (const auto& [address, mapping] : oscMappings) {
        file << "osc_mapping_" << address << "=" << mapping.parameterName << ","
             << mapping.minValue << "," << mapping.maxValue << ","
             << (mapping.invert ? 1 : 0) << "\n";
    }

    const auto& oscTriggers = oscSystem.getTriggerMappings();
    for (const auto& [address, mapping] : oscTriggers) {
        file << "osc_trigger_" << address << "=" << mapping.actionName << "\n";
    }

    file << "autoRandomize=" << (r.autoRandomize ? 1 : 0) << "\n";
    file << "intervalSeconds=" << r.intervalSeconds << "\n";
    file << "useVideoDuration=" << (r.useVideoDuration ? 1 : 0) << "\n";
    file << "useShuffleMode=" << (r.useShuffleMode ? 1 : 0) << "\n";
    
    file << "autoRandomize2=" << (r2.autoRandomize ? 1 : 0) << "\n";
    file << "intervalSeconds2=" << r2.intervalSeconds << "\n";
    file << "useVideoDuration2=" << (r2.useVideoDuration ? 1 : 0) << "\n";
    file << "useShuffleMode2=" << (r2.useShuffleMode ? 1 : 0) << "\n";

    file << "autoRandomize3=" << (r3.autoRandomize ? 1 : 0) << "\n";
    file << "intervalSeconds3=" << r3.intervalSeconds << "\n";
    file << "useVideoDuration3=" << (r3.useVideoDuration ? 1 : 0) << "\n";
    file << "useShuffleMode3=" << (r3.useShuffleMode ? 1 : 0) << "\n";

    file << "allowDimensionChangeRecreation=" << (allowDimensionChangeRecreation ? 1 : 0) << "\n";
    file << "selectedVideoAsset=" << selectedVideoAsset << "\n";
    file << "selectedVideoAsset2=" << selectedVideoAsset2 << "\n";
    file << "selectedVideoAsset3=" << selectedVideoAsset3 << "\n";
    file << "videoSourcePath=" << videoSourcePath << "\n";
    file << "videoSourcePath2=" << videoSourcePath2 << "\n";
    file << "videoSourcePath3=" << videoSourcePath3 << "\n";

    std::cout << "[ControlState::save] Saved V1 idx=" << selectedVideoAsset
              << " path='" << videoSourcePath << "'\n";
    std::cout << "[ControlState::save] Saved V2 idx=" << selectedVideoAsset2
              << " path='" << videoSourcePath2 << "'\n";
    std::cout << "[ControlState::save] Saved V3 idx=" << selectedVideoAsset3
              << " path='" << videoSourcePath3 << "'\n";
}
