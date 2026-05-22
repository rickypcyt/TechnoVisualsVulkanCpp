#include "ControlState.h"
#include "VisualControls.h"
#include "MidiSystem.h"
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
    bool useShuffleMode = true; // New: use shuffle to ensure all videos play before repeat
    std::vector<int> shuffleQueue; // New: shuffled order of video indices
    int currentShuffleIndex = 0; // New: current position in shuffle queue
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
    KVMap values;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            values[line.substr(0, pos)] = line.substr(pos + 1);
        }
    }
    return values;
}

float readFloat(const KVMap& kv, const char* key, float def) {
    auto it = kv.find(key);
    if (it != kv.end()) { try { return std::stof(it->second); } catch (...) {} }
    return def;
}
int readInt(const KVMap& kv, const char* key, int def) {
    auto it = kv.find(key);
    if (it != kv.end()) { try { return std::stoi(it->second); } catch (...) {} }
    return def;
}
bool readBool(const KVMap& kv, const char* key, bool def) {
    auto it = kv.find(key);
    if (it != kv.end()) { try { return std::stoi(it->second) != 0; } catch (...) {} }
    return def;
}
glm::vec3 readVec3(const KVMap& kv, const char* key, glm::vec3 def) {
    auto it = kv.find(key);
    if (it != kv.end()) {
        std::istringstream iss(it->second);
        try { iss >> def.r >> def.g >> def.b; } catch (...) {}
    }
    return def;
}
glm::vec4 readVec4(const KVMap& kv, const char* key, glm::vec4 def) {
    auto it = kv.find(key);
    if (it != kv.end()) {
        std::istringstream iss(it->second);
        try { iss >> def.r >> def.g >> def.b >> def.a; } catch (...) {}
    }
    return def;
}
std::string readString(const KVMap& kv, const char* key, std::string def) {
    auto it = kv.find(key);
    if (it != kv.end()) {
        return it->second;
    }
    return def;
}

} // namespace anónimo

// -----------------------------------------------------------------------
// ControlState::load
// -----------------------------------------------------------------------
void ControlState::load(
    const std::string&    path,
    VisualControls&       c,
    VideoRandomizerState& r,
    VideoRandomizerState2& r2,
    bool&                 allowDimensionChangeRecreation,
    MidiSystem&           midiSystem,
    OscSystem&            oscSystem,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    const KVMap kv = parseFile(file);

    // Clear existing MIDI mappings before loading
    midiSystem.clearMappings();

    // Clear existing OSC mappings before loading
    oscSystem.clearMappings();
    oscSystem.clearTriggerMappings();

    // Load MIDI mappings
    for (const auto& [key, value] : kv) {
        if (key.find("midi_cc_") == 0) {
            // Parse key to get CC number
            try {
                int ccNumber = std::stoi(key.substr(8)); // "midi_cc_" is 8 chars

                // Parse value: parameterName,minValue,maxValue,invert
                std::istringstream iss(value);
                std::string paramName;
                float minVal, maxVal;
                int invert;
                char comma;

                if (std::getline(iss, paramName, ',') &&
                    (iss >> minVal >> comma >> maxVal >> comma >> invert)) {
                    midiSystem.addMapping(ccNumber, paramName, minVal, maxVal, invert != 0);
                    std::cout << "[ControlState] Loaded MIDI CC " << ccNumber << " -> " << paramName << std::endl;
                }
            } catch (...) {
                std::cerr << "[ControlState] Failed to parse MIDI mapping: " << key << "=" << value << std::endl;
            }
        }
    }

    // Load OSC mappings
    for (const auto& [key, value] : kv) {
        if (key.find("osc_mapping_") == 0) {
            // Parse key to get OSC address
            std::string oscAddress = key.substr(12); // "osc_mapping_" is 12 chars

            try {
                // Parse value: parameterName,minValue,maxValue,invert
                std::istringstream iss(value);
                std::string paramName;
                float minVal, maxVal;
                int invert;
                char comma;

                if (std::getline(iss, paramName, ',') &&
                    (iss >> minVal >> comma >> maxVal >> comma >> invert)) {
                    oscSystem.addMapping(oscAddress, paramName, minVal, maxVal, invert != 0);
                    std::cout << "[ControlState] Loaded OSC mapping " << oscAddress << " -> " << paramName << std::endl;
                }
            } catch (...) {
                std::cerr << "[ControlState] Failed to parse OSC mapping: " << key << "=" << value << std::endl;
            }
        }
    }

    // Load OSC trigger mappings
    for (const auto& [key, value] : kv) {
        if (key.find("osc_trigger_") == 0) {
            // Parse key to get OSC address
            std::string oscAddress = key.substr(12); // "osc_trigger_" is 12 chars

            try {
                oscSystem.addTriggerMapping(oscAddress, value);
                std::cout << "[ControlState] Loaded OSC trigger " << oscAddress << " -> " << value << std::endl;
            } catch (...) {
                std::cerr << "[ControlState] Failed to parse OSC trigger: " << key << "=" << value << std::endl;
            }
        }
    }

    // Leemos sobre una copia para que si algo falla no corromper el estado actual
    VisualControls loaded = c;

    loaded.animationSpeed          = readFloat(kv, "animationSpeed",          loaded.animationSpeed);
    loaded.animationTargetSeconds  = readFloat(kv, "animationTargetSeconds",  loaded.animationTargetSeconds);
    loaded.tempo                   = readFloat(kv, "tempo",                   loaded.tempo);
    loaded.energy                  = readFloat(kv, "energy",                  loaded.energy);
    loaded.bass                    = readFloat(kv, "bass",                    loaded.bass);
    loaded.mid                     = readFloat(kv, "mid",                     loaded.mid);
    loaded.high                    = readFloat(kv, "high",                    loaded.high);
    loaded.colorBlend              = readFloat(kv, "colorBlend",              loaded.colorBlend);
    loaded.primaryColor            = readVec4 (kv, "primaryColor",            loaded.primaryColor);
    loaded.secondaryColor          = readVec4 (kv, "secondaryColor",          loaded.secondaryColor);
    loaded.autoRandomizeColors     = readBool (kv, "autoRandomizeColors",     loaded.autoRandomizeColors);
    loaded.colorRandomizeInterval  = readFloat(kv, "colorRandomizeInterval",  loaded.colorRandomizeInterval);
    loaded.activeMode              = std::clamp(readInt(kv, "activeMode", loaded.activeMode), 0, 1);
    loaded.videoMix                = readFloat(kv, "videoMix",                loaded.videoMix);
    loaded.videoPlaybackRate       = readFloat(kv, "videoPlaybackRate",       loaded.videoPlaybackRate);
    loaded.videoDecodeOversample   = readFloat(kv, "videoDecodeOversample",   loaded.videoDecodeOversample);
    loaded.autoScaleVideo          = readBool (kv, "autoScaleVideo",          loaded.autoScaleVideo);
    loaded.selectedVideoFolder     = readString(kv, "selectedVideoFolder", loaded.selectedVideoFolder);
    loaded.selectedVideo2Folder    = readString(kv, "selectedVideo2Folder", loaded.selectedVideo2Folder);
    loaded.enableDualVideo         = readBool (kv, "enableDualVideo",         loaded.enableDualVideo);
    loaded.video2Mix               = readFloat(kv, "video2Mix",               loaded.video2Mix);
    loaded.video2BlendMode         = std::clamp(readInt(kv, "video2BlendMode", loaded.video2BlendMode), 0, 4);
    loaded.video2PlaybackRate      = readFloat(kv, "video2PlaybackRate",      loaded.video2PlaybackRate);
    loaded.forcedFpsIndex          = std::clamp(readInt(kv, "forcedFpsIndex", loaded.forcedFpsIndex), 0, 4);
    loaded.loopBlendSeconds        = std::clamp(readFloat(kv, "loopBlendSeconds", loaded.loopBlendSeconds), 0.0f, 5.0f);
    loaded.grayscaleAmount         = readFloat(kv, "grayscaleAmount",         loaded.grayscaleAmount);
    loaded.sharpenAmount           = readFloat(kv, "sharpenAmount",           loaded.sharpenAmount);
    loaded.upscaleEnabled          = readBool (kv, "upscaleEnabled",          loaded.upscaleEnabled);
    loaded.crtCurvature            = readFloat(kv, "crtCurvature",            loaded.crtCurvature);
    loaded.crtHorizontalCurvature  = readFloat(kv, "crtHorizontalCurvature",  loaded.crtHorizontalCurvature);
    loaded.crtScanlineIntensity    = readFloat(kv, "crtScanlineIntensity",    loaded.crtScanlineIntensity);
    loaded.crtMaskIntensity        = readFloat(kv, "crtMaskIntensity",        loaded.crtMaskIntensity);
    loaded.crtVignette             = readFloat(kv, "crtVignette",             loaded.crtVignette);
    loaded.crtFishEye              = readFloat(kv, "crtFishEye",              loaded.crtFishEye);
    loaded.bloomIntensity          = readFloat(kv, "bloomIntensity",          loaded.bloomIntensity);
    loaded.bloomThreshold          = readFloat(kv, "bloomThreshold",          loaded.bloomThreshold);
    loaded.aberrationAmount        = readFloat(kv, "aberrationAmount",        loaded.aberrationAmount);
    loaded.grainStrength           = readFloat(kv, "grainStrength",           loaded.grainStrength);
    loaded.bendAmount              = readFloat(kv, "bendAmount",              loaded.bendAmount);
    loaded.glitchAmount            = readFloat(kv, "glitchAmount",            loaded.glitchAmount);
    loaded.randomVideoStart        = readBool (kv, "randomVideoStart",        loaded.randomVideoStart);
    loaded.colorBalance            = readVec3 (kv, "colorBalance",            loaded.colorBalance);
    loaded.gradeBrightness         = readFloat(kv, "gradeBrightness",         loaded.gradeBrightness);
    loaded.gradeContrast           = readFloat(kv, "gradeContrast",           loaded.gradeContrast);
    loaded.gradeSaturation         = readFloat(kv, "gradeSaturation",         loaded.gradeSaturation);
    loaded.gradeHueShift           = readFloat(kv, "gradeHueShift",           loaded.gradeHueShift);
    loaded.gradeGamma              = readFloat(kv, "gradeGamma",              loaded.gradeGamma);
    loaded.colorLUTIndex           = readInt  (kv, "colorLUTIndex",           loaded.colorLUTIndex);
    loaded.splitToneBalance        = readFloat(kv, "splitToneBalance",        loaded.splitToneBalance);
    loaded.splitToneShadows        = readVec3 (kv, "splitToneShadows",        loaded.splitToneShadows);
    loaded.splitToneHighlights     = readVec3 (kv, "splitToneHighlights",     loaded.splitToneHighlights);
    loaded.feedbackAmount          = readFloat(kv, "feedbackAmount",          loaded.feedbackAmount);
    loaded.trailStrength           = readFloat(kv, "trailStrength",           loaded.trailStrength);
    loaded.temporalAccumulation    = readFloat(kv, "temporalAccumulation",    loaded.temporalAccumulation);
    loaded.feedbackDecay           = readFloat(kv, "feedbackDecay",           loaded.feedbackDecay);
    loaded.recursiveBlend          = readFloat(kv, "recursiveBlend",          loaded.recursiveBlend);
    loaded.uvWarpStrength          = readFloat(kv, "uvWarpStrength",          loaded.uvWarpStrength);
    loaded.rippleStrength          = readFloat(kv, "rippleStrength",          loaded.rippleStrength);
    loaded.rippleFrequency         = readFloat(kv, "rippleFrequency",         loaded.rippleFrequency);
    loaded.swirlStrength           = readFloat(kv, "swirlStrength",           loaded.swirlStrength);
    loaded.displacementAmount      = readFloat(kv, "displacementAmount",      loaded.displacementAmount);
    loaded.kaleidoSegments         = readFloat(kv, "kaleidoSegments",         loaded.kaleidoSegments);
    loaded.tunnelDepth             = readFloat(kv, "tunnelDepth",             loaded.tunnelDepth);
    loaded.tunnelCurvature         = readFloat(kv, "tunnelCurvature",         loaded.tunnelCurvature);
    loaded.gaussianBlur            = readFloat(kv, "gaussianBlur",            loaded.gaussianBlur);
    loaded.directionalBlur         = readFloat(kv, "directionalBlur",         loaded.directionalBlur);
    loaded.directionalBlurAngle    = readFloat(kv, "directionalBlurAngle",    loaded.directionalBlurAngle);
    loaded.zoomBlur                = readFloat(kv, "zoomBlur",                loaded.zoomBlur);
    loaded.motionBlur              = readFloat(kv, "motionBlur",              loaded.motionBlur);
    loaded.temporalBlur            = readFloat(kv, "temporalBlur",            loaded.temporalBlur);
    loaded.unsharpMask             = readFloat(kv, "unsharpMask",             loaded.unsharpMask);
    loaded.casAmount               = readFloat(kv, "casAmount",               loaded.casAmount);
    loaded.localContrast           = readFloat(kv, "localContrast",           loaded.localContrast);
    loaded.glitchDatamosh          = readFloat(kv, "glitchDatamosh",          loaded.glitchDatamosh);
    loaded.glitchRGBSplit          = readFloat(kv, "glitchRGBSplit",          loaded.glitchRGBSplit);
    loaded.glitchScanlineBreak     = readFloat(kv, "glitchScanlineBreak",     loaded.glitchScanlineBreak);
    loaded.glitchJitter            = readFloat(kv, "glitchJitter",            loaded.glitchJitter);
    loaded.glitchTearing           = readFloat(kv, "glitchTearing",           loaded.glitchTearing);
    loaded.glitchPixelSort         = readFloat(kv, "glitchPixelSort",         loaded.glitchPixelSort);
    loaded.glitchBufferCorruption  = readFloat(kv, "glitchBufferCorruption",  loaded.glitchBufferCorruption);
    loaded.blendModeProcedural     = readInt  (kv, "blendModeProcedural",     loaded.blendModeProcedural);
    loaded.blendModeVideo          = readInt  (kv, "blendModeVideo",          loaded.blendModeVideo);
    loaded.blendModeFeedback       = readInt  (kv, "blendModeFeedback",       loaded.blendModeFeedback);
    loaded.blendProceduralMix      = readFloat(kv, "blendProceduralMix",      loaded.blendProceduralMix);
    loaded.blendVideoMix           = readFloat(kv, "blendVideoMix",           loaded.blendVideoMix);
    loaded.blendFeedbackMix        = readFloat(kv, "blendFeedbackMix",        loaded.blendFeedbackMix);
    loaded.analogScanlineFocus     = readFloat(kv, "analogScanlineFocus",     loaded.analogScanlineFocus);
    loaded.analogMaskBalance       = readFloat(kv, "analogMaskBalance",       loaded.analogMaskBalance);
    loaded.analogNoise             = readFloat(kv, "analogNoise",             loaded.analogNoise);
    loaded.analogBloom             = readFloat(kv, "analogBloom",             loaded.analogBloom);
    loaded.vhsDistortion           = readFloat(kv, "vhsDistortion",           loaded.vhsDistortion);
    loaded.analogChromaticAberration = readFloat(kv, "analogChromaticAberration", loaded.analogChromaticAberration);
    loaded.enableColorGrading      = readBool (kv, "enableColorGrading",      loaded.enableColorGrading);
    loaded.enableFeedback          = readBool (kv, "enableFeedback",          loaded.enableFeedback);
    loaded.enableDistortion        = readBool (kv, "enableDistortion",        loaded.enableDistortion);
    loaded.enableBlurMotion        = readBool (kv, "enableBlurMotion",        loaded.enableBlurMotion);
    loaded.enableSharpen           = readBool (kv, "enableSharpen",           loaded.enableSharpen);
    loaded.enableGlitch            = readBool (kv, "enableGlitch",            loaded.enableGlitch);
    loaded.enableBlending          = readBool (kv, "enableBlending",          loaded.enableBlending);
    loaded.enableAnalog            = readBool (kv, "enableAnalog",            loaded.enableAnalog);
    loaded.enableAudioReactive     = readBool (kv, "enableAudioReactive",     loaded.enableAudioReactive);
    loaded.enableTemporal          = readBool (kv, "enableTemporal",          loaded.enableTemporal);
    loaded.enablePostCrtCurvature  = readBool (kv, "enablePostCrtCurvature",  loaded.enablePostCrtCurvature);
    loaded.enablePostScanMask      = readBool (kv, "enablePostScanMask",      loaded.enablePostScanMask);
    loaded.enablePostVignette      = readBool (kv, "enablePostVignette",      loaded.enablePostVignette);
    loaded.enablePostFishEye       = readBool (kv, "enablePostFishEye",       loaded.enablePostFishEye);
    loaded.enablePostBloom         = readBool (kv, "enablePostBloom",         loaded.enablePostBloom);
    loaded.enablePostAberration    = readBool (kv, "enablePostAberration",    loaded.enablePostAberration);
    loaded.enablePostGrain         = readBool (kv, "enablePostGrain",         loaded.enablePostGrain);
    loaded.enablePostBend          = readBool (kv, "enablePostBend",          loaded.enablePostBend);
    loaded.enablePostGlitch        = readBool (kv, "enablePostGlitch",        loaded.enablePostGlitch);
    loaded.enablePostColorBalance  = readBool (kv, "enablePostColorBalance",  loaded.enablePostColorBalance);
    loaded.audioWarpResponse       = readFloat(kv, "audioWarpResponse",       loaded.audioWarpResponse);
    loaded.audioFeedbackResponse   = readFloat(kv, "audioFeedbackResponse",   loaded.audioFeedbackResponse);
    loaded.audioBlurResponse       = readFloat(kv, "audioBlurResponse",       loaded.audioBlurResponse);
    loaded.audioColorResponse      = readFloat(kv, "audioColorResponse",      loaded.audioColorResponse);
    loaded.audioGlitchResponse     = readFloat(kv, "audioGlitchResponse",     loaded.audioGlitchResponse);
    loaded.audioBeatSync           = readFloat(kv, "audioBeatSync",           loaded.audioBeatSync);
    loaded.audioLfoRate            = readFloat(kv, "audioLfoRate",            loaded.audioLfoRate);
    loaded.temporalInterpolation   = readFloat(kv, "temporalInterpolation",   loaded.temporalInterpolation);
    loaded.temporalBlendStrength   = readFloat(kv, "temporalBlendStrength",   loaded.temporalBlendStrength);
    loaded.slowMotionFactor        = readFloat(kv, "slowMotionFactor",        loaded.slowMotionFactor);
    loaded.frameAccumulation       = readFloat(kv, "frameAccumulation",       loaded.frameAccumulation);

    loaded.enablePixelate           = readBool (kv, "enablePixelate",           loaded.enablePixelate);
    loaded.enableStrobe             = readBool (kv, "enableStrobe",             loaded.enableStrobe);
    loaded.enableThreshold          = readBool (kv, "enableThreshold",          loaded.enableThreshold);
    loaded.enableSlowZoom           = readBool (kv, "enableSlowZoom",           loaded.enableSlowZoom);
    loaded.enableMirror             = readBool (kv, "enableMirror",             loaded.enableMirror);
    loaded.enableInvert             = readBool (kv, "enableInvert",             loaded.enableInvert);
    loaded.enablePosterize          = readBool (kv, "enablePosterize",          loaded.enablePosterize);
    loaded.enableInfrared           = readBool (kv, "enableInfrared",           loaded.enableInfrared);
    loaded.enableZoomPulse          = readBool (kv, "enableZoomPulse",          loaded.enableZoomPulse);
    loaded.enableRGBShift           = readBool (kv, "enableRGBShift",           loaded.enableRGBShift);
    loaded.enableFXAA               = readBool (kv, "enableFXAA",               loaded.enableFXAA);
    loaded.enableGrid               = readBool (kv, "enableGrid",               loaded.enableGrid);
    loaded.enableEdgeDetect         = readBool (kv, "enableEdgeDetect",         loaded.enableEdgeDetect);
    loaded.enableCameraMovement     = readBool (kv, "enableCameraMovement",     loaded.enableCameraMovement);
    loaded.gridMirrorCells          = readBool (kv, "gridMirrorCells",          loaded.gridMirrorCells);
    loaded.pixelateAmount           = readFloat(kv, "pixelateAmount",           loaded.pixelateAmount);
    loaded.strobeSpeed              = readFloat(kv, "strobeSpeed",              loaded.strobeSpeed);
    loaded.thresholdLevel           = readFloat(kv, "thresholdLevel",           loaded.thresholdLevel);
    loaded.slowZoomAmount           = readFloat(kv, "slowZoomAmount",           loaded.slowZoomAmount);
    loaded.mirrorAmount             = readFloat(kv, "mirrorAmount",             loaded.mirrorAmount);
    loaded.posterizeLevels          = readFloat(kv, "posterizeLevels",          loaded.posterizeLevels);
    loaded.zoomPulseAmount          = readFloat(kv, "zoomPulseAmount",          loaded.zoomPulseAmount);
    loaded.rgbShiftAmount           = readFloat(kv, "rgbShiftAmount",           loaded.rgbShiftAmount);
    loaded.fxaaQualitySubpix        = readFloat(kv, "fxaaQualitySubpix",        loaded.fxaaQualitySubpix);
    loaded.fxaaQualityEdgeThreshold = readFloat(kv, "fxaaQualityEdgeThreshold", loaded.fxaaQualityEdgeThreshold);
    loaded.fxaaQualityEdgeThresholdMin = readFloat(kv, "fxaaQualityEdgeThresholdMin", loaded.fxaaQualityEdgeThresholdMin);
    loaded.gridMode                 = readInt  (kv, "gridMode",                 loaded.gridMode);
    loaded.gridCount                = readInt  (kv, "gridCount",                loaded.gridCount);
    loaded.gridRows                 = readInt  (kv, "gridRows",                 loaded.gridRows);
    loaded.gridColumns              = readInt  (kv, "gridColumns",              loaded.gridColumns);
    loaded.edgeStrength             = readFloat(kv, "edgeStrength",             loaded.edgeStrength);
    loaded.edgeThreshold            = readFloat(kv, "edgeThreshold",            loaded.edgeThreshold);
    loaded.edgeBlend                = readFloat(kv, "edgeBlend",                loaded.edgeBlend);
    loaded.edgeColor                = readVec3 (kv, "edgeColor",                loaded.edgeColor);
    loaded.cameraZoom               = readFloat(kv, "cameraZoom",               loaded.cameraZoom);
    loaded.cameraPanX               = readFloat(kv, "cameraPanX",               loaded.cameraPanX);
    loaded.cameraPanY               = readFloat(kv, "cameraPanY",               loaded.cameraPanY);
    loaded.cameraRotation           = readFloat(kv, "cameraRotation",           loaded.cameraRotation);
    loaded.rgbOverlay               = readVec3 (kv, "rgbOverlay",               loaded.rgbOverlay);
    loaded.enableRgbOverlay         = readBool (kv, "enableRgbOverlay",         loaded.enableRgbOverlay);

    // Randomizer
    VideoRandomizerState rLoaded = r;
    rLoaded.autoRandomize    = readBool (kv, "autoRandomize",    rLoaded.autoRandomize);
    rLoaded.intervalSeconds  = readFloat(kv, "intervalSeconds",  rLoaded.intervalSeconds);
    rLoaded.useVideoDuration = readBool (kv, "useVideoDuration", rLoaded.useVideoDuration);
    rLoaded.useShuffleMode   = readBool (kv, "useShuffleMode",   rLoaded.useShuffleMode);
    
    // Video 2 randomizer
    VideoRandomizerState2 r2Loaded = r2;
    r2Loaded.autoRandomize    = readBool (kv, "autoRandomize2",    r2Loaded.autoRandomize);
    r2Loaded.intervalSeconds  = readFloat(kv, "intervalSeconds2",  r2Loaded.intervalSeconds);
    r2Loaded.useVideoDuration = readBool (kv, "useVideoDuration2", r2Loaded.useVideoDuration);
    r2Loaded.useShuffleMode   = readBool (kv, "useShuffleMode2",   r2Loaded.useShuffleMode);

    allowDimensionChangeRecreation =
        readBool(kv, "allowDimensionChangeRecreation", allowDimensionChangeRecreation);

    selectedVideoAsset  = readInt(kv, "selectedVideoAsset",  selectedVideoAsset);
    selectedVideoAsset2 = readInt(kv, "selectedVideoAsset2", selectedVideoAsset2);

    // Commit: solo si todo fue bien
    c = loaded;
    r = rLoaded;
    r2 = r2Loaded;
    r.currentVideoDuration = 0.0f;
    r.recentHistory.clear();
    r.elapsedSeconds = 0.0f;
}

// -----------------------------------------------------------------------
// ControlState::save
// -----------------------------------------------------------------------
void ControlState::save(
    const std::string&        path,
    const VisualControls&     c,
    const VideoRandomizerState& r,
    const VideoRandomizerState2& r2,
    bool                      allowDimensionChangeRecreation,
    const MidiSystem&         midiSystem,
    const OscSystem&          oscSystem,
    int                       selectedVideoAsset,
    int                       selectedVideoAsset2)
{
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "[ControlState] Cannot open for writing: " << path << std::endl;
        return;
    }

    file << "# VJAY Control State - Key=Value\n"
         << "# Add new fields freely, no version bump needed\n\n";

    // Save MIDI mappings
    const auto& mappings = midiSystem.getMappings();
    for (const auto& [cc, mapping] : mappings) {
        file << "midi_cc_" << cc << "=" << mapping.parameterName << ","
             << mapping.minValue << "," << mapping.maxValue << ","
             << (mapping.invert ? 1 : 0) << "\n";
    }

    // Save OSC mappings
    const auto& oscMappings = oscSystem.getMappings();
    for (const auto& [address, mapping] : oscMappings) {
        file << "osc_mapping_" << address << "=" << mapping.parameterName << ","
             << mapping.minValue << "," << mapping.maxValue << ","
             << (mapping.invert ? 1 : 0) << "\n";
    }

    // Save OSC trigger mappings
    const auto& oscTriggers = oscSystem.getTriggerMappings();
    for (const auto& [address, mapping] : oscTriggers) {
        file << "osc_trigger_" << address << "=" << mapping.actionName << "\n";
    }

    file << "\n";

    // Helpers locales con lambdas (mas limpio que repetir file<<key<<"="<<val<<"\n")
    auto wf = [&](const char* k, float v)        { file << k << "=" << v << "\n"; };
    auto wi = [&](const char* k, int v)           { file << k << "=" << v << "\n"; };
    auto wb = [&](const char* k, bool v)          { file << k << "=" << (v?1:0) << "\n"; };
    auto wv3= [&](const char* k, const glm::vec3& v){ file << k<<"="<<v.r<<" "<<v.g<<" "<<v.b<<"\n"; };
    auto wv4= [&](const char* k, const glm::vec4& v){ file << k<<"="<<v.r<<" "<<v.g<<" "<<v.b<<" "<<v.a<<"\n"; };
    auto ws = [&](const char* k, const std::string& v){ file << k << "=" << v << "\n"; };

    wf("animationSpeed",          c.animationSpeed);
    wf("animationTargetSeconds",  c.animationTargetSeconds);
    wf("tempo",                   c.tempo);
    wf("energy",                  c.energy);
    wf("bass",                    c.bass);
    wf("mid",                     c.mid);
    wf("high",                    c.high);
    wf("colorBlend",              c.colorBlend);
    wv4("primaryColor",           c.primaryColor);
    wv4("secondaryColor",         c.secondaryColor);
    wb("autoRandomizeColors",     c.autoRandomizeColors);
    wf("colorRandomizeInterval",  c.colorRandomizeInterval);
    wi("activeMode",              c.activeMode);
    wf("videoMix",                c.videoMix);
    wf("videoPlaybackRate",       c.videoPlaybackRate);
    wf("videoDecodeOversample",   c.videoDecodeOversample);
    wb("autoScaleVideo",          c.autoScaleVideo);
    ws("selectedVideoFolder",     c.selectedVideoFolder);
    ws("selectedVideo2Folder",    c.selectedVideo2Folder);
    wb("enableDualVideo",         c.enableDualVideo);
    wf("video2Mix",               c.video2Mix);
    wi("video2BlendMode",         c.video2BlendMode);
    wf("video2PlaybackRate",      c.video2PlaybackRate);
    wi("forcedFpsIndex",          c.forcedFpsIndex);
    wf("loopBlendSeconds",        c.loopBlendSeconds);
    wf("grayscaleAmount",         c.grayscaleAmount);
    wf("sharpenAmount",           c.sharpenAmount);
    wb("upscaleEnabled",          c.upscaleEnabled);
    wf("crtCurvature",            c.crtCurvature);
    wf("crtHorizontalCurvature",  c.crtHorizontalCurvature);
    wf("crtScanlineIntensity",    c.crtScanlineIntensity);
    wf("crtMaskIntensity",        c.crtMaskIntensity);
    wf("crtVignette",             c.crtVignette);
    wf("crtFishEye",              c.crtFishEye);
    wf("bloomIntensity",          c.bloomIntensity);
    wf("bloomThreshold",          c.bloomThreshold);
    wf("aberrationAmount",        c.aberrationAmount);
    wf("grainStrength",           c.grainStrength);
    wf("bendAmount",              c.bendAmount);
    wf("glitchAmount",            c.glitchAmount);
    wb("randomVideoStart",        c.randomVideoStart);
    wv3("colorBalance",           c.colorBalance);
    wf("gradeBrightness",         c.gradeBrightness);
    wf("gradeContrast",           c.gradeContrast);
    wf("gradeSaturation",         c.gradeSaturation);
    wf("gradeHueShift",           c.gradeHueShift);
    wf("gradeGamma",              c.gradeGamma);
    wi("colorLUTIndex",           c.colorLUTIndex);
    wf("splitToneBalance",        c.splitToneBalance);
    wv3("splitToneShadows",       c.splitToneShadows);
    wv3("splitToneHighlights",    c.splitToneHighlights);
    wf("feedbackAmount",          c.feedbackAmount);
    wf("trailStrength",           c.trailStrength);
    wf("temporalAccumulation",    c.temporalAccumulation);
    wf("feedbackDecay",           c.feedbackDecay);
    wf("recursiveBlend",          c.recursiveBlend);
    wf("uvWarpStrength",          c.uvWarpStrength);
    wf("rippleStrength",          c.rippleStrength);
    wf("rippleFrequency",         c.rippleFrequency);
    wf("swirlStrength",           c.swirlStrength);
    wf("displacementAmount",      c.displacementAmount);
    wf("kaleidoSegments",         c.kaleidoSegments);
    wf("tunnelDepth",             c.tunnelDepth);
    wf("tunnelCurvature",         c.tunnelCurvature);
    wf("gaussianBlur",            c.gaussianBlur);
    wf("directionalBlur",         c.directionalBlur);
    wf("directionalBlurAngle",    c.directionalBlurAngle);
    wf("zoomBlur",                c.zoomBlur);
    wf("motionBlur",              c.motionBlur);
    wf("temporalBlur",            c.temporalBlur);
    wf("unsharpMask",             c.unsharpMask);
    wf("casAmount",               c.casAmount);
    wf("localContrast",           c.localContrast);
    wf("glitchDatamosh",          c.glitchDatamosh);
    wf("glitchRGBSplit",          c.glitchRGBSplit);
    wf("glitchScanlineBreak",     c.glitchScanlineBreak);
    wf("glitchJitter",            c.glitchJitter);
    wf("glitchTearing",           c.glitchTearing);
    wf("glitchPixelSort",         c.glitchPixelSort);
    wf("glitchBufferCorruption",  c.glitchBufferCorruption);
    wi("blendModeProcedural",     c.blendModeProcedural);
    wi("blendModeVideo",          c.blendModeVideo);
    wi("blendModeFeedback",       c.blendModeFeedback);
    wf("blendProceduralMix",      c.blendProceduralMix);
    wf("blendVideoMix",           c.blendVideoMix);
    wf("blendFeedbackMix",        c.blendFeedbackMix);
    wf("analogScanlineFocus",     c.analogScanlineFocus);
    wf("analogMaskBalance",       c.analogMaskBalance);
    wf("analogNoise",             c.analogNoise);
    wf("analogBloom",             c.analogBloom);
    wf("vhsDistortion",           c.vhsDistortion);
    wf("analogChromaticAberration",c.analogChromaticAberration);
    wb("enableColorGrading",      c.enableColorGrading);
    wb("enableFeedback",          c.enableFeedback);
    wb("enableDistortion",        c.enableDistortion);
    wb("enableBlurMotion",        c.enableBlurMotion);
    wb("enableSharpen",           c.enableSharpen);
    wb("enableGlitch",            c.enableGlitch);
    wb("enableBlending",          c.enableBlending);
    wb("enableAnalog",            c.enableAnalog);
    wb("enableAudioReactive",     c.enableAudioReactive);
    wb("enableTemporal",          c.enableTemporal);
    wb("enablePostCrtCurvature",  c.enablePostCrtCurvature);
    wb("enablePostScanMask",      c.enablePostScanMask);
    wb("enablePostVignette",      c.enablePostVignette);
    wb("enablePostFishEye",       c.enablePostFishEye);
    wb("enablePostBloom",         c.enablePostBloom);
    wb("enablePostAberration",    c.enablePostAberration);
    wb("enablePostGrain",         c.enablePostGrain);
    wb("enablePostBend",          c.enablePostBend);
    wb("enablePostGlitch",        c.enablePostGlitch);
    wb("enablePostColorBalance",  c.enablePostColorBalance);
    wf("audioWarpResponse",       c.audioWarpResponse);
    wf("audioFeedbackResponse",   c.audioFeedbackResponse);
    wf("audioBlurResponse",       c.audioBlurResponse);
    wf("audioColorResponse",      c.audioColorResponse);
    wf("audioGlitchResponse",     c.audioGlitchResponse);
    wf("audioBeatSync",           c.audioBeatSync);
    wf("audioLfoRate",            c.audioLfoRate);
    wf("temporalInterpolation",   c.temporalInterpolation);
    wf("temporalBlendStrength",   c.temporalBlendStrength);
    wf("slowMotionFactor",        c.slowMotionFactor);
    wf("frameAccumulation",       c.frameAccumulation);

    // VJAY EXTRA
    wb("enablePixelate",           c.enablePixelate);
    wb("enableStrobe",             c.enableStrobe);
    wb("enableThreshold",          c.enableThreshold);
    wb("enableSlowZoom",           c.enableSlowZoom);
    wb("enableMirror",             c.enableMirror);
    wb("enableInvert",             c.enableInvert);
    wb("enablePosterize",          c.enablePosterize);
    wb("enableInfrared",           c.enableInfrared);
    wb("enableZoomPulse",          c.enableZoomPulse);
    wb("enableRGBShift",           c.enableRGBShift);
    wb("enableFXAA",               c.enableFXAA);
    wb("enableGrid",               c.enableGrid);
    wb("enableEdgeDetect",         c.enableEdgeDetect);
    wb("enableCameraMovement",     c.enableCameraMovement);
    wb("gridMirrorCells",          c.gridMirrorCells);
    wf("pixelateAmount",           c.pixelateAmount);
    wf("strobeSpeed",              c.strobeSpeed);
    wf("thresholdLevel",           c.thresholdLevel);
    wf("slowZoomAmount",           c.slowZoomAmount);
    wf("mirrorAmount",             c.mirrorAmount);
    wf("posterizeLevels",          c.posterizeLevels);
    wf("zoomPulseAmount",          c.zoomPulseAmount);
    wf("rgbShiftAmount",           c.rgbShiftAmount);
    wf("fxaaQualitySubpix",        c.fxaaQualitySubpix);
    wf("fxaaQualityEdgeThreshold", c.fxaaQualityEdgeThreshold);
    wf("fxaaQualityEdgeThresholdMin", c.fxaaQualityEdgeThresholdMin);
    wi("gridMode",                 c.gridMode);
    wi("gridCount",                c.gridCount);
    wi("gridRows",                 c.gridRows);
    wi("gridColumns",              c.gridColumns);
    wf("edgeStrength",             c.edgeStrength);
    wf("edgeThreshold",            c.edgeThreshold);
    wf("edgeBlend",                c.edgeBlend);
    wv3("edgeColor",               c.edgeColor);
    wf("cameraZoom",               c.cameraZoom);
    wf("cameraPanX",               c.cameraPanX);
    wf("cameraPanY",               c.cameraPanY);
    wf("cameraRotation",           c.cameraRotation);
    wv3("rgbOverlay",              c.rgbOverlay);
    wb("enableRgbOverlay",         c.enableRgbOverlay);

    // Randomizer
    wb("autoRandomize",                r.autoRandomize);
    wf("intervalSeconds",              r.intervalSeconds);
    wb("useVideoDuration",             r.useVideoDuration);
    wb("useShuffleMode",               r.useShuffleMode);
    
    // Video 2 randomizer
    wb("autoRandomize2",               r2.autoRandomize);
    wf("intervalSeconds2",             r2.intervalSeconds);
    wb("useVideoDuration2",            r2.useVideoDuration);
    wb("useShuffleMode2",              r2.useShuffleMode);
    
    wb("allowDimensionChangeRecreation", allowDimensionChangeRecreation);
    wi("selectedVideoAsset",           selectedVideoAsset);
    wi("selectedVideoAsset2",          selectedVideoAsset2);
}
