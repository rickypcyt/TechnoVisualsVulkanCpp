// UISystem.cpp — refactorizado
#include "UISystem.h"
#include "OscSystem.h"
#include "AudioSystem.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/VisualControls.h"
#include "app/MidiSystem.h"
#include "video/VideoPlayer.h"
#include "video/VideoRegistry.h"
#include "app/ProjectState.h"
#include "render/RenderJob.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <iterator>
#include <regex>
#include <random>
#include <unordered_map>
#include <set>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static std::string pickFolderDialog(const std::string& currentPath) {
    std::string result;
    IFileOpenDialog* pDlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&pDlg));
    if (FAILED(hr) || !pDlg) return result;

    pDlg->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    if (!currentPath.empty()) {
        std::wstring wPath(currentPath.begin(), currentPath.end());
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wPath.c_str(), nullptr, IID_PPV_ARGS(&pItem))) && pItem) {
            pDlg->SetFolder(pItem);
            pItem->Release();
        }
    }

    hr = pDlg->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* pResult = nullptr;
        hr = pDlg->GetResult(&pResult);
        if (SUCCEEDED(hr) && pResult) {
            PWSTR pwszPath = nullptr;
            hr = pResult->GetDisplayName(SIGDN_FILESYSPATH, &pwszPath);
            if (SUCCEEDED(hr) && pwszPath) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pwszPath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    result.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pwszPath, -1, result.data(), len, nullptr, nullptr);
                }
                CoTaskMemFree(pwszPath);
            }
            pResult->Release();
        }
    }
    pDlg->Release();
    return result;
}
#endif

// ── Tipos públicos ────────────────────────────────────────────────────────────

struct VideoRandomizerState {
    bool  autoRandomize      = false;
    bool  useVideoDuration   = false;
    float intervalSeconds    = 30.0f;
    float elapsedSeconds     = 0.0f;
    float currentVideoDuration = 0.0f;
    int   historyWindow      = 3;
    std::deque<int>    recentHistory;
    bool               useShuffleMode      = true;
    std::vector<int>   shuffleQueue;
    int                currentShuffleIndex = 0;
};

struct VideoRandomizerState2 {
    bool  autoRandomize        = false;
    bool  useVideoDuration     = false;
    float intervalSeconds      = 30.0f;
    float elapsedSeconds       = 0.0f;
    float currentVideoDuration = 0.0f;
    bool  useShuffleMode       = true;
    std::vector<int> shuffleQueue;
    int   currentShuffleIndex  = 0;
};

// ── Constantes de UI ──────────────────────────────────────────────────────────

static const std::array<int, 5> FORCED_FPS_OPTIONS_UI = {0, 15, 24, 30, 60};

static const char* BLEND_ITEMS = "Add\0Screen\0Multiply\0Overlay\0Difference\0Soft Light\0";

static constexpr float kPreviewMinAspect = 16.0f / 9.0f;
static constexpr float kPreviewMaxWidth  = 420.0f;
static constexpr float kPreviewMaxHeight = 240.0f;

// ── Namespace interno ─────────────────────────────────────────────────────────
namespace {

// ── Layer combo ──────────────────────────────────────────────────────────────

struct LayerOption {
    int mode = 0;
    std::string label;
};

const std::vector<LayerOption>& layerOptions() {
    static const std::vector<LayerOption> options = [] {
        std::unordered_map<int, std::string> labelsByMode;

        auto add = [&](int mode, std::string label) {
            if (mode < 0) return;
            labelsByMode.emplace(mode, std::move(label));
        };

        // Built-in layers currently exposed in the UI.
        add(0,  "Layer 0");
        add(1,  "Layer 1");
        add(2,  "Plasma Wave");
        add(3,  "Radial Burst");
        add(4,  "Grid Pulse");
        add(5,  "Noise Flow");
        add(6,  "Cellular");
        add(7,  "Mandala");
        add(8,  "Terrain Scan");
        add(9,  "Wire Cube");
        add(10, "Oscilloscope");
        add(11, "Corner X");
        add(12, "Electric Rays");
        add(40, "Anaglyph 3D");

        // Procedural folder integration: use the metadata embedded in each shader file.
        // We scan all *.glsl files under /procedural and collect every @EFFECT entry.
        const std::filesystem::path proceduralDir = "procedural";
        if (std::filesystem::exists(proceduralDir) && std::filesystem::is_directory(proceduralDir)) {
            const std::regex effectRegex(R"(@EFFECT\s+name\s*=\s*\"([^\"]+)\".*index\s*=\s*(\d+))");

            for (const auto& entry : std::filesystem::directory_iterator(proceduralDir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".glsl") {
                    continue;
                }

                std::ifstream file(entry.path());
                if (!file) continue;

                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                for (auto it = std::sregex_iterator(content.begin(), content.end(), effectRegex);
                     it != std::sregex_iterator(); ++it) {
                    const int mode = std::stoi((*it)[2].str());
                    const std::string name = (*it)[1].str();
                    labelsByMode.emplace(mode, name);
                }
            }
        }

        std::vector<LayerOption> out;
        out.reserve(labelsByMode.size());
        for (const auto& [mode, label] : labelsByMode) {
            out.push_back({mode, label});
        }
        std::sort(out.begin(), out.end(), [](const LayerOption& a, const LayerOption& b) {
            return a.mode < b.mode;
        });
        return out;
    }();

    return options;
}

int layerIndexFromMode(int mode) {
    const auto& options = layerOptions();
    for (int i = 0; i < static_cast<int>(options.size()); ++i)
        if (options[i].mode == mode) return i;
    return 0;
}

bool cycleLayerModeInPlace(int& activeMode, int delta) {
    const auto& options = layerOptions();
    if (options.empty()) return false;

    int idx = layerIndexFromMode(activeMode);
    idx = (idx + delta) % static_cast<int>(options.size());
    if (idx < 0) idx += static_cast<int>(options.size());
    activeMode = options[idx].mode;
    return true;
}

bool drawActiveLayerCombo(const char* label, int& activeMode) {
    const auto& options = layerOptions();
    if (options.empty()) return false;

    std::vector<const char*> items;
    items.reserve(options.size());
    for (const auto& opt : options) {
        items.push_back(opt.label.c_str());
    }

    int idx = layerIndexFromMode(activeMode);
    if (ImGui::Combo(label, &idx, items.data(), static_cast<int>(items.size()))) {
        activeMode = options[idx].mode;
        return true;
    }
    return false;
}

int cycleLayerMode(int currentMode, int delta) {
    const auto& options = layerOptions();
    if (options.empty()) return currentMode;

    int idx = layerIndexFromMode(currentMode);
    idx = (idx + delta) % static_cast<int>(options.size());
    if (idx < 0) idx += static_cast<int>(options.size());
    return options[idx].mode;
}

// ── Helpers aleatorios ────────────────────────────────────────────────────────

float randFloat(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}
int randInt(std::mt19937& rng, int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}
bool randBool(std::mt19937& rng) { return randInt(rng, 0, 1) == 1; }

// ── HSV → RGB (antes lambda duplicada) ───────────────────────────────────────

glm::vec3 hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if      (h < 60.f)  { r = c; g = x; b = 0; }
    else if (h < 120.f) { r = x; g = c; b = 0; }
    else if (h < 180.f) { r = 0; g = c; b = x; }
    else if (h < 240.f) { r = 0; g = x; b = c; }
    else if (h < 300.f) { r = x; g = 0; b = c; }
    else                { r = c; g = 0; b = x; }
    return {r + m, g + m, b + m};
}

// ── Tabla de parámetros ───────────────────────────────────────────────────────

struct ParameterInfo { const char* category; const char* name; float minVal; float maxVal; };

static const ParameterInfo PARAMETER_INFOS[] = {
    // Procedural
    {"Procedural","animationSpeed",    0.f, 3.f},
    {"Procedural","tempo",             0.f, 2.f},
    {"Procedural","energy",            0.f, 1.f},
    {"Procedural","bass",              0.f, 1.f},
    {"Procedural","mid",               0.f, 1.f},
    {"Procedural","high",              0.f, 1.f},
    {"Procedural","colorBlend",        0.f, 1.f},
    {"Procedural","uvWarpStrength",    0.f, 1.f},
    {"Procedural","rippleStrength",    0.f, 1.f},
    {"Procedural","rippleFrequency",   0.f, 5.f},
    {"Procedural","swirlStrength",     0.f, 1.f},
    {"Procedural","displacementAmount",0.f, 1.f},
    {"Procedural","kaleidoSegments",   1.f,12.f},
    {"Procedural","tunnelDepth",       0.f, 1.f},
    {"Procedural","tunnelCurvature",   0.f, 1.f},
    // Post FX
    {"Post FX","bloomIntensity",            0.f, 1.f},
    {"Post FX","bloomThreshold",            0.f, 1.f},
    {"Post FX","aberrationAmount",          0.f,0.1f},
    {"Post FX","grainStrength",             0.f, 1.f},
    {"Post FX","crtCurvature",              0.f,0.5f},
    {"Post FX","crtScanlineIntensity",      0.f, 1.f},
    {"Post FX","crtMaskIntensity",          0.f, 1.f},
    {"Post FX","crtVignette",               0.f, 1.f},
    {"Post FX","crtFishEye",                0.f,0.2f},
    {"Post FX","gaussianBlur",              0.f, 1.f},
    {"Post FX","directionalBlur",           0.f, 1.f},
    {"Post FX","directionalBlurAngle",      0.f,360.f},
    {"Post FX","zoomBlur",                  0.f, 1.f},
    {"Post FX","motionBlur",                0.f, 1.f},
    {"Post FX","temporalBlur",              0.f, 1.f},
    {"Post FX","unsharpMask",               0.f, 1.f},
    {"Post FX","casAmount",                 0.f, 1.f},
    {"Post FX","localContrast",             0.f, 1.f},
    {"Post FX","pixelateAmount",            0.f, 1.f},
    {"Post FX","strobeSpeed",               0.f,10.f},
    {"Post FX","thresholdLevel",            0.f, 1.f},
    {"Post FX","enableThreshold",           0.f, 1.f},
    {"Post FX","slowZoomAmount",            0.f, 1.f},
    {"Post FX","edgeStrength",              0.f, 2.f},
    {"Post FX","edgeThreshold",             0.f, 1.f},
    {"Post FX","edgeBlend",                 0.f, 1.f},
    {"Post FX","fxaaQualitySubpix",         0.f, 1.f},
    {"Post FX","fxaaQualityEdgeThreshold",  0.f,0.5f},
    {"Post FX","fxaaQualityEdgeThresholdMin",0.f,0.2f},
    // VJay Basics
    {"VJay Basics","videoPlaybackRate",    0.1f,3.f},
    {"VJay Basics","videoDecodeOversample",1.f, 4.f},
    {"VJay Basics","videoMix",             0.f, 1.f},
    {"VJay Basics","video2Mix",            0.f, 1.f},
    {"VJay Basics","video2PlaybackRate",   0.1f,5.f},
    {"VJay Basics","grayscaleAmount",      0.f, 1.f},
    {"VJay Basics","sharpenAmount",        0.f, 1.f},
    {"VJay Basics","gradeBrightness",     -1.f, 1.f},
    {"VJay Basics","gradeContrast",        0.f, 2.f},
    {"VJay Basics","gradeSaturation",      0.f, 2.f},
    {"VJay Basics","gradeHueShift",        0.f,360.f},
    {"VJay Basics","gradeGamma",           0.1f,3.f},
    {"VJay Basics","colorLUTIndex",        0.f,10.f},
    {"VJay Basics","splitToneBalance",     0.f, 1.f},
    {"VJay Basics","blendProceduralMix",   0.f, 1.f},
    {"VJay Basics","blendVideoMix",        0.f, 1.f},
    {"VJay Basics","blendFeedbackMix",     0.f, 1.f},
    {"VJay Basics","video2BlendMode",      0.f, 4.f},
    {"VJay Basics","enableRgbOverlay",     0.f, 1.f},
    {"VJay Basics","rgbOverlay",           0.f, 2.f},
    {"VJay Basics","rgbOverlayR",          0.f, 2.f},
    {"VJay Basics","rgbOverlayG",          0.f, 2.f},
    {"VJay Basics","rgbOverlayB",          0.f, 2.f},
    // VJay Extra
    {"VJay Extra","feedbackAmount",         0.f, 1.f},
    {"VJay Extra","trailStrength",          0.f, 1.f},
    {"VJay Extra","temporalAccumulation",   0.f, 1.f},
    {"VJay Extra","feedbackDecay",          0.f, 1.f},
    {"VJay Extra","recursiveBlend",         0.f, 1.f},
    {"VJay Extra","glitchAmount",           0.f, 1.f},
    {"VJay Extra","glitchDatamosh",         0.f, 1.f},
    {"VJay Extra","glitchRGBSplit",         0.f, 1.f},
    {"VJay Extra","glitchScanlineBreak",    0.f, 1.f},
    {"VJay Extra","glitchJitter",           0.f, 1.f},
    {"VJay Extra","glitchTearing",          0.f, 1.f},
    {"VJay Extra","glitchPixelSort",        0.f, 1.f},
    {"VJay Extra","glitchBufferCorruption", 0.f, 1.f},
    {"VJay Extra","analogScanlineFocus",    0.f, 1.f},
    {"VJay Extra","analogMaskBalance",      0.f, 1.f},
    {"VJay Extra","analogNoise",            0.f, 1.f},
    {"VJay Extra","analogBloom",            0.f, 1.f},
    {"VJay Extra","vhsDistortion",          0.f, 1.f},
    {"VJay Extra","analogChromaticAberration",0.f,0.1f},
    {"VJay Extra","mirrorAmount",           0.f, 1.f},
    {"VJay Extra","posterizeLevels",        2.f,16.f},
    {"VJay Extra","zoomPulseAmount",        0.f, 1.f},
    {"VJay Extra","rgbShiftAmount",         0.f,0.1f},
    {"VJay Extra","audioWarpResponse",      0.f, 1.f},
    {"VJay Extra","audioFeedbackResponse",  0.f, 1.f},
    {"VJay Extra","audioBlurResponse",      0.f, 1.f},
    {"VJay Extra","audioColorResponse",     0.f, 1.f},
    {"VJay Extra","audioGlitchResponse",    0.f, 1.f},
    {"VJay Extra","audioBeatSync",          0.f, 2.f},
    {"VJay Extra","audioLfoRate",           0.f, 2.f},
    {"VJay Extra","temporalInterpolation",  0.f, 1.f},
    {"VJay Extra","temporalBlendStrength",  0.f, 1.f},
    {"VJay Extra","slowMotionFactor",       0.1f,2.f},
    {"VJay Extra","frameAccumulation",      0.f, 1.f},
};
constexpr int PARAMETER_COUNT = static_cast<int>(std::size(PARAMETER_INFOS));

static const char* TRIGGER_ACTIONS[] = {
    "randomizeVideo", "randomizeVideo2",
    "randomizePreviewVideo1", "randomizePreviewVideo2",
    "jumpRandom", "folderChanged", "applyChanges"
};
constexpr int TRIGGER_ACTION_COUNT = static_cast<int>(std::size(TRIGGER_ACTIONS));

// ── Lazily-built display strings para combos ──────────────────────────────────

const std::vector<std::string>& paramDisplayStrings() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> tmp;
        tmp.reserve(PARAMETER_COUNT);
        for (auto& p : PARAMETER_INFOS)
            tmp.emplace_back(std::string(p.category) + ": " + p.name);
        return tmp;
    }();
    return v;
}
const std::vector<const char*>& paramDisplayPtrs() {
    static const std::vector<const char*> v = [] {
        std::vector<const char*> tmp;
        for (auto& s : paramDisplayStrings()) tmp.push_back(s.c_str());
        return tmp;
    }();
    return v;
}

// ── Folder list (derived from registry assets) ───────────────────────────────

const std::vector<std::string>& getAvailableFolders() {
    static std::vector<std::string> folders;
    static bool scanned = false;
    if (!scanned) {
        folders.push_back("All Folders");
        // Fallback: scan mp4s/ if it exists
        try {
            fs::path root("mp4s");
            if (fs::exists(root) && fs::is_directory(root))
                for (auto& e : fs::directory_iterator(root))
                    if (e.is_directory())
                        folders.push_back(e.path().filename().string());
        } catch (const std::exception& ex) {
            std::cerr << "[UISystem] Error scanning mp4s: " << ex.what() << "\n";
        }
        scanned = true;
    }
    return folders;
}

// Rebuild folder list from registry assets (called when registry is available)
void rebuildAvailableFolders(const VideoRegistry& registry) {
    auto& folders = const_cast<std::vector<std::string>&>(getAvailableFolders());
    folders.clear();
    folders.push_back("All Folders");

    std::set<std::string> seen;
    for (const auto& asset : registry.getAssets()) {
        fs::path p(asset.metadata.path);
        if (p.has_parent_path()) {
            std::string folderName = p.parent_path().filename().string();
            if (!folderName.empty() && seen.find(folderName) == seen.end()) {
                seen.insert(folderName);
                folders.push_back(folderName);
            }
        }
    }
}

// Devuelve el índice en la lista para un nombre de carpeta dado
int folderIndex(const std::string& name) {
    auto& folders = getAvailableFolders();
    for (int i = 0; i < (int)folders.size(); ++i)
        if (folders[i] == name) return i;
    return 0;
}

// ── Effective playback rate (antes lambda duplicada) ──────────────────────────

double effectivePlaybackRate(const VisualControls& c, double clipFps) {
    double base  = std::clamp((double)c.playback.videoPlaybackRate, 0.05, 8.0);
    int    idx   = std::clamp(c.playback.forcedFpsIndex, 0, (int)FORCED_FPS_OPTIONS_UI.size() - 1);
    int    forced = FORCED_FPS_OPTIONS_UI[idx];
    return (forced > 0) ? std::clamp(forced / clipFps, 0.05, 8.0) : base;
}

// ── Sección con toggle — macro de archivo (no se repite por función) ──────────
// Se define aquí una sola vez y se usa en drawVJayBasicsContent y drawVJayExtra.

#define TOGGLED_SECTION(label, toggleRef, ...)          \
    {                                                    \
        ImGui::Text("%s", label); ImGui::SameLine();    \
        changed |= ImGui::Checkbox("On##" label, &(toggleRef)); \
        ImGui::Separator();                              \
        ImGui::BeginDisabled(!(toggleRef));              \
        __VA_ARGS__                                      \
        ImGui::EndDisabled();                            \
        ImGui::Spacing();                                \
    }

// ── Randomizers ───────────────────────────────────────────────────────────────

void randomizePostFxControls(VisualControls& c, std::mt19937& rng) {
    auto rr = [&](float lo, float hi){ return randFloat(rng, lo, hi); };
    auto u  = [&](){ return randFloat(rng, 0.f, 1.f); };
    if (c.post.enablePostCrtCurvature) { c.post.crtCurvature = rr(0,0.6f); c.post.crtHorizontalCurvature = rr(0,0.6f); }
    if (c.post.enablePostScanMask)     { c.post.crtScanlineIntensity = u(); c.post.crtMaskIntensity = u(); }
    if (c.post.enablePostVignette)     { c.post.crtVignette = u(); }
    if (c.post.enablePostFishEye)      { c.post.crtFishEye = rr(-1,1); }
    if (c.post.enablePostBloom)        { c.post.bloomIntensity = rr(0,2); c.post.bloomThreshold = u(); }
    if (c.post.enablePostAberration)   { c.post.aberrationAmount = rr(-0.05f,0.05f); }
    if (c.post.enablePostGrain)        { c.post.grainStrength = rr(0,0.5f); }
    if (c.post.enablePostBend)         { c.fx.bendAmount = randFloat(rng, 0.f, 0.4f); }
    if (c.post.enablePostGlitch)       { c.fx.glitchAmount = u(); }
    if (c.post.enablePostColorBalance && !c.locks.lockColorBalance) { c.color.colorBalance = {rr(0,2),rr(0,2),rr(0,2)}; }
}

void randomizeVJayExtraControls(VisualControls& c, std::mt19937& rng) {
    if (c.fx.enablePixelate)    c.fx.pixelateAmount  = randFloat(rng, 0.f, 1.f);
    if (c.fx.enableStrobe)      c.fx.strobeSpeed     = randFloat(rng, 0.f, 20.f);
    if (c.fx.enableThreshold && !c.locks.lockThreshold)   c.fx.thresholdLevel  = randFloat(rng, 0.f, 1.f);
    if (c.fx.enableSlowZoom)    c.fx.slowZoomAmount  = randFloat(rng, 0.f, 1.f);

    if (c.fx.enableEdgeDetect) {
        c.fx.edgeStrength  = randFloat(rng, 0.1f, 5.f);
        c.fx.edgeThreshold = randFloat(rng, 0.f, 1.f);
        c.fx.edgeBlend     = randFloat(rng, 0.f, 1.f);
        c.fx.edgeColor     = {randFloat(rng,0,1), randFloat(rng,0,1), randFloat(rng,0,1)};
    }
    if (c.system.enableFXAA) {
        c.system.fxaaQualitySubpix            = randFloat(rng, 0.f, 1.f);
        c.system.fxaaQualityEdgeThreshold     = randFloat(rng, 0.f, 0.5f);
        c.system.fxaaQualityEdgeThresholdMin  = randFloat(rng, 0.f, 0.2f);
    }
    if (c.fx.enableMirror)   c.fx.mirrorAmount    = randFloat(rng, 0.f, 1.f);
    if (c.fx.enablePosterize)c.fx.posterizeLevels = randFloat(rng, 2.f, 16.f);
    if (c.fx.enableZoomPulse)c.fx.zoomPulseAmount = randFloat(rng, 0.f, 1.f);
    if (c.fx.enableRGBShift) c.fx.rgbShiftAmount  = randFloat(rng, 0.f, 0.1f);

    if (c.grid.enabled && !c.locks.lockGrid) {
        c.grid.mode = randInt(rng, 0, 2);
        if (c.grid.mode == 2) {
            c.grid.rows    = randInt(rng, 1, 8);
            c.grid.columns = randInt(rng, 1, 8);
            c.grid.count   = std::max(2, c.grid.columns);
        } else {
            c.grid.count = randInt(rng, 2, 8);
        }
        c.grid.mirrorCells = randBool(rng);
    }
    if (c.camera.enableMovement) {
        c.camera.zoom     = randFloat(rng, 0.5f, 2.f);
        c.camera.panX     = randFloat(rng, -1.f, 1.f);
        c.camera.panY     = randFloat(rng, -1.f, 1.f);
        c.camera.rotation = randFloat(rng, -3.14159f, 3.14159f);
    }
}

void randomizeVJayBasicsControls(VisualControls& c, std::mt19937& rng) {
    auto rr  = [&](float lo, float hi) { return randFloat(rng, lo, hi); };
    auto u01 = [&]() { return randFloat(rng, 0.f, 1.f); };
    auto ri  = [&](int lo, int hi)     { return randInt(rng, lo, hi); };

    if (c.color.enableColorGrading) {
        c.color.gradeBrightness      = rr(-0.2f, 0.2f);
        c.color.gradeContrast        = rr(0.8f, 1.4f);
        c.color.gradeSaturation      = rr(0.6f, 1.6f);
        c.color.gradeHueShift        = rr(-90.f, 90.f);
        c.color.gradeGamma           = rr(0.8f, 1.4f);
        c.color.colorLUTIndex        = ri(0, 5);
        c.color.splitToneBalance     = u01() * 0.5f;
        c.color.splitToneShadows     = {u01(), u01(), u01()};
        c.color.splitToneHighlights  = {u01(), u01(), u01()};
    }
    if (c.fx.enableDistortion) {
        c.fx.uvWarpStrength    = rr(0.f, 0.5f);
        c.fx.rippleStrength    = rr(0.f, 0.5f);
        c.fx.rippleFrequency   = rr(0.5f, 6.f);
        c.fx.swirlStrength     = rr(-0.5f, 0.5f);
        c.fx.displacementAmount= rr(0.f, 0.5f);
        c.fx.kaleidoSegments   = rr(3.f, 12.f);
        c.fx.tunnelDepth       = rr(0.f, 0.5f);
        c.fx.tunnelCurvature   = rr(-0.5f, 0.5f);
    }
    if (c.fx.enableBlurMotion) {
        c.fx.gaussianBlur         = u01();
        c.fx.directionalBlur      = u01();
        c.fx.directionalBlurAngle = rr(0.f, 360.f);
        c.fx.zoomBlur             = u01();
        c.fx.motionBlur           = u01();
        c.fx.temporalBlur         = u01();
    }
    if (c.fx.enableSharpen) {
        c.fx.unsharpMask  = u01();
        c.fx.casAmount    = u01();
        c.fx.localContrast= u01();
    }
    if (c.fx.enableGlitch) {
        c.fx.glitchDatamosh        = u01();
        c.fx.glitchRGBSplit        = u01();
        c.fx.glitchScanlineBreak   = u01();
        c.fx.glitchJitter          = u01();
        c.fx.glitchTearing         = u01();
        c.fx.glitchPixelSort       = u01();
        c.fx.glitchBufferCorruption= u01();
    }
    if (c.blending.enableBlending) {
        c.blending.blendModeProcedural = ri(0, 5);
        c.blending.blendModeVideo      = ri(0, 5);
        c.blending.blendProceduralMix  = rr(0.f, 2.f);
        c.blending.blendVideoMix       = rr(0.f, 2.f);
        // blendModeFeedback / blendFeedbackMix intentionally skipped —
        // they belong to "feedback blending" and are preserved here.
    }
    if (c.post.enableAnalog) {
        c.post.analogScanlineFocus        = u01();
        c.post.analogMaskBalance          = u01();
        c.post.analogNoise                = u01();
        c.post.analogBloom                = rr(0.f, 2.f);
        c.post.vhsDistortion              = u01();
        c.post.analogChromaticAberration  = rr(0.f, 0.25f);
    }
    if (c.temporal.enableFeedback) {
        c.temporal.feedbackAmount      = u01();
        c.temporal.trailStrength       = u01();
        c.temporal.temporalAccumulation= u01();
        c.temporal.feedbackDecay       = u01();
        c.temporal.recursiveBlend      = u01();
    }
    if (c.temporal.enableTemporal) {
        c.playback.temporalInterpolation  = u01();
        c.playback.temporalBlendStrength  = u01();
        c.playback.slowMotionFactor       = rr(0.1f, 4.f);
        c.playback.frameAccumulation      = u01();
    }
}

// ── Referencia de triggers (helper compartido por MIDI y OSC) ─────────────────

void drawTriggerAndRgbReferenceSection() {
    ImGui::Separator();
    ImGui::TextColored({0.95f,0.8f,0.4f,1}, "Trigger actions disponibles");
    for (int i = 0; i < TRIGGER_ACTION_COUNT; ++i)
        ImGui::BulletText("%s", TRIGGER_ACTIONS[i]);

    ImGui::Separator();
    ImGui::TextColored({0.8f,1,0.8f,1}, "RGB overlay mapping");
    ImGui::BulletText("enableRgbOverlay - toggle (0=off, 1=on)");
    ImGui::BulletText("rgbOverlay  - vec3 color (0-2 range)");
    ImGui::BulletText("rgbOverlayR/G/B - control channels individually");
    ImGui::BulletText("OSC multi-float: /vjay/rgbOverlay R G B (0-1 floats)");
}

// ── Widget de folder selector (evita repetición en Video1 / Video2) ───────────

// Devuelve true si la selección cambió. newFolder queda vacío → "All Folders".
bool drawFolderCombo(const char* label, std::string& currentFolder) {
    auto& folders = getAvailableFolders();
    static std::vector<const char*> ptrs;          // se reconstruye solo si cambia tamaño
    if (ptrs.size() != folders.size()) {
        ptrs.clear();
        for (auto& f : folders) ptrs.push_back(f.c_str());
    }
    int idx = folderIndex(currentFolder);
    if (ImGui::Combo(label, &idx, ptrs.data(), (int)ptrs.size())) {
        currentFolder = (idx == 0) ? "" : folders[idx];
        return true;
    }
    return false;
}

// ── Randomizer block (igual para V1 y V2, parametrizado) ─────────────────────

template<typename Randomizer>
bool drawRandomizerBlock(const char* suffix,
                         Randomizer& r,
                         float& transitionDuration,
                         size_t assetCount,
                         const std::function<void()>& onRandomizePreview,
                         const std::function<void()>& onRandomizeFinal)
{
    bool changed = false;
    const bool hasChoice = assetCount > 1;
    char lbl[64];

    ImGui::BeginDisabled(r.useVideoDuration);
    snprintf(lbl, sizeof(lbl), "Random interval (s)%s", suffix);
    if (ImGui::SliderFloat(lbl, &r.intervalSeconds, 1.f, 300.f, "%.0f s")) {
        r.intervalSeconds = std::clamp(r.intervalSeconds, 1.f, 600.f);
        r.elapsedSeconds  = 0.f;
        changed = true;
    }
    ImGui::EndDisabled();

    snprintf(lbl, sizeof(lbl), "Sync shuffle to clip duration%s", suffix);
    if (ImGui::Checkbox(lbl, &r.useVideoDuration)) { r.elapsedSeconds = 0.f; changed = true; }
    if (r.useVideoDuration)
        ImGui::Text("Current clip: %.1f s", std::max(0.f, r.currentVideoDuration));

    snprintf(lbl, sizeof(lbl), "Transition duration (s)%s", suffix);
    changed |= ImGui::SliderFloat(lbl, &transitionDuration, 0.1f, 2.f, "%.2f s");

    ImGui::BeginDisabled(!hasChoice);
    snprintf(lbl, sizeof(lbl), "Randomize video%s", suffix);
    if (ImGui::Button(lbl)) {
        if (onRandomizeFinal)  onRandomizeFinal();
        if (onRandomizePreview) onRandomizePreview();
        changed = true;
    }
    ImGui::SameLine();
    snprintf(lbl, sizeof(lbl), "Auto randomize%s", suffix);
    changed |= ImGui::Checkbox(lbl, &r.autoRandomize);
    if (!hasChoice) {
        ImGui::SameLine(); ImGui::TextDisabled("Need at least 2 videos");
    } else if (r.autoRandomize) {
        float target = (r.useVideoDuration && r.currentVideoDuration > 0.f)
                       ? r.currentVideoDuration : r.intervalSeconds;
        ImGui::Text("Next shuffle in %.1f s", std::max(0.f, target - r.elapsedSeconds));
    }
    ImGui::Indent();
    snprintf(lbl, sizeof(lbl), "Shuffle all videos%s", suffix);
    changed |= ImGui::Checkbox(lbl, &r.useShuffleMode);
    if (r.useShuffleMode && !r.shuffleQueue.empty())
        ImGui::Text("Playing %d/%d", r.currentShuffleIndex + 1, (int)r.shuffleQueue.size());
    ImGui::Unindent();
    ImGui::EndDisabled();

    return changed;
}

} // namespace

void UISystem::drawPreviewContent(
    VisualControls&       controls,
    VideoRegistry&        registry,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2,
    int&                  selectedVideoAsset3,
    VideoRandomizerState& r1,
    VideoRandomizerState2& r2,
    VideoRandomizerState2& r3,
    float&                transDur,
    float&                transDur2,
    float&                transDur3,
    bool&                 controlsDirty,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks,
    const std::string&    video1Path,
    const std::string&    video2Path,
    const std::string&    video3Path,
    std::mt19937&         rng)
{
    // Rebuild folder list from registry assets so all subfolders are available
    rebuildAvailableFolders(registry);

    // Performance control at the top of Preview tab
    ImGui::Text("Performance Settings:");
    if (ImGui::Checkbox("Enable Video Previews", &enableVideoPreviews)) {
        if (!enableVideoPreviews) {
            // Clear existing preview textures to free memory
            destroyPreviewSlot(previewSlotVideo1);
            destroyPreviewSlot(previewSlotVideo2);
            destroyPreviewSlot(previewSlotVideo3);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause All")) {
        previewSlotVideo1.paused = true;
        previewSlotVideo2.paused = true;
        previewSlotVideo3.paused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Resume All")) {
        previewSlotVideo1.paused = false;
        previewSlotVideo2.paused = false;
        previewSlotVideo3.paused = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Unload All")) {
        destroyPreviewSlot(previewSlotVideo1);
        previewSlotVideo1.manualUnload = true;
        destroyPreviewSlot(previewSlotVideo2);
        previewSlotVideo2.manualUnload = true;
        destroyPreviewSlot(previewSlotVideo3);
        previewSlotVideo3.manualUnload = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load All")) {
        auto reloadSlot = [&](VideoPreviewSlot& slot, const std::string& folder, int fallbackSelection) {
            const auto& assets = registry.getFilteredAssets(folder);
            if (assets.empty()) return;

            int selection = slot.previewSelection;
            if (selection < 0 || selection >= (int)assets.size()) {
                selection = std::clamp(fallbackSelection, 0, (int)assets.size() - 1);
            }

            slot.manualUnload = false;
            loadPreview(slot, assets[selection].metadata.path);
        };

        reloadSlot(previewSlotVideo1, controls.playback.selectedVideoFolder, selectedVideoAsset);
        reloadSlot(previewSlotVideo2, controls.playback.selectedVideo2Folder, selectedVideoAsset2);
        reloadSlot(previewSlotVideo3, controls.playback.selectedVideo3Folder, selectedVideoAsset3);
    }
    if (!enableVideoPreviews) {
        ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "Video previews disabled for better performance");
        ImGui::Separator();
    } else {
        ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "Previews running at 5 FPS for performance");
        ImGui::Separator();
    }
    
    float deltaTime = ImGui::GetIO().DeltaTime;
    updatePreviewSlot(previewSlotVideo1, deltaTime);
    updatePreviewSlot(previewSlotVideo2, deltaTime);
    updatePreviewSlot(previewSlotVideo3, deltaTime);

    // ── Auto-randomize preview (independent from final renderer) ──
    auto tickPreviewAutoRandomize = [&](auto& r, int slot) {
        if (!r.autoRandomize) return;
        previewAutoRandomizeElapsed[slot] += deltaTime;
        float target = (r.useVideoDuration && r.currentVideoDuration > 0.f)
                       ? r.currentVideoDuration : r.intervalSeconds;
        if (previewAutoRandomizeElapsed[slot] >= target) {
            previewAutoRandomizeElapsed[slot] = 0.f;
            forcePreviewShuffle(slot);
        }
    };
    tickPreviewAutoRandomize(r1, 0);
    tickPreviewAutoRandomize(r2, 1);
    tickPreviewAutoRandomize(r3, 2);

    bool changed = false;

    auto drawAssetCombo = [&](const char* label, const std::string& folder,
                              int& sel, std::function<void(const std::string&)> onLoad)
    {
        const auto& assets = registry.getFilteredAssets(folder);

        if (assets.empty()) {
            ImGui::TextDisabled("No videos found in this folder");
            return;
        }

        if (sel < 0 || sel >= (int)assets.size())
            sel = 0;

        if (ImGui::BeginCombo(label, assets[sel].metadata.filename.c_str()))
        {
            for (int i = 0; i < (int)assets.size(); ++i)
            {
                if (ImGui::Selectable(assets[i].metadata.filename.c_str(), i == sel))
                {
                    if (sel != i)
                    {
                        sel = i;
                        if (onLoad) onLoad(assets[i].metadata.path);
                    }
                }

                if (i == sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Text("Videos in folder: %zu", assets.size());
    };

    // ── Helper: update slot state and draw just the image ──
    auto updateSlotAndDrawImage = [&](const char* tag,
                                      VideoPreviewSlot& slot,
                                      std::string& folder,
                                      int& activeSelection,
                                      int slotIndex)
    {
        const auto& assets = registry.getFilteredAssets(folder);
        if (assets.empty()) {
            // Don't destroy the slot — just skip preview update. The slot stays
            // alive so switching to a valid folder restores it immediately.
            ImGui::PushID(tag);
            if (slot.texture) {
                float aspect = (slot.textureHeight > 0) ? (float)slot.textureWidth / (float)slot.textureHeight : 1.0f;
                float width  = kPreviewMaxWidth;
                float height = width / std::max(0.001f, aspect);
                if (height > kPreviewMaxHeight) {
                    height = kPreviewMaxHeight;
                    width  = height * aspect;
                }
                ImGui::Image((ImTextureID)slot.texture, {width, height});
            }
            ImGui::TextColored({0.8f, 0.6f, 0.2f, 1.0f}, "  [No videos in folder '%s']", folder.c_str());
            ImGui::PopID();
            return;
        }

        activeSelection = std::clamp(activeSelection, 0, (int)assets.size() - 1);
        if (slot.previewSelection < 0 || slot.previewSelection >= (int)assets.size())
            slot.previewSelection = activeSelection;
        slot.previewSelection = std::clamp(slot.previewSelection, 0, (int)assets.size() - 1);
        if (slotIndex >= 0 && slotIndex < 3 && previewShuffleRequested[slotIndex]) {
            previewShuffleRequested[slotIndex] = false;
            if (assets.size() > 1) {
                std::uniform_int_distribution<int> dist(0, static_cast<int>(assets.size()) - 1);
                int newSelection;
                do { newSelection = dist(previewRng); }
                while (newSelection == slot.previewSelection);
                slot.previewSelection = newSelection;
                slot.manualUnload = false;
            }
        }
        // Final safety clamp after any shuffle
        slot.previewSelection = std::clamp(slot.previewSelection, 0, (int)assets.size() - 1);
        if (slot.confirmedSelection != activeSelection)
            slot.confirmedSelection = activeSelection;

        const std::string& desiredPath = assets[slot.previewSelection].metadata.path;
        if (slot.loadedPath != desiredPath && !slot.manualUnload)
            loadPreview(slot, desiredPath);

        if (slot.player && callbacks.onGetVideoSpeed) {
            float speed = callbacks.onGetVideoSpeed(desiredPath);
            slot.player->setPlaybackRate(speed);
        }

        ImGui::PushID(tag);
        if (slot.texture) {
            float aspect = (slot.textureHeight > 0) ? (float)slot.textureWidth / (float)slot.textureHeight : 1.0f;
            float width  = kPreviewMaxWidth;
            float height = width / std::max(0.001f, aspect);
            if (height > kPreviewMaxHeight) {
                height = kPreviewMaxHeight;
                width  = height * aspect;
            }
            ImGui::Image((ImTextureID)slot.texture, {width, height});
            if (slot.paused) {
                ImGui::TextColored({0.8f, 0.6f, 0.2f, 1.0f}, "  [PAUSED - frame frozen]");
            }
        } else if (slot.manualUnload) {
            ImGui::TextColored({0.6f, 0.6f, 0.6f, 1.0f}, "  [UNLOADED - click Load to reload]");
        } else if (!slot.lastError.empty()) {
            ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "%s", slot.lastError.c_str());
        } else {
            ImGui::TextDisabled("Cargando preview...");
        }
        ImGui::PopID();
    };

    // ── Helper: draw all controls for a slot ──
    auto drawControls = [&](const char* tag,
                            VideoPreviewSlot& slot,
                            std::string& folder,
                            int& activeSelection,
                            const std::function<void(const std::string&)>& applyCallback,
                            int slotIndex)
    {
        ImGui::PushID(tag);
        ImGui::Separator();
        ImGui::Text("Loader / Actions %s", tag);

        // ── Folder selector is always available, even if the current folder is empty ──
        if (drawFolderCombo("Source Folder", folder))
        {
            if (slotIndex == 0 && callbacks.onFolderChanged)
                callbacks.onFolderChanged();
            else if (slotIndex == 1 && callbacks.onFolderChanged2)
                callbacks.onFolderChanged2();
            else if (slotIndex == 2 && callbacks.onFolderChanged3)
                callbacks.onFolderChanged3();
        }

        ImGui::Text("Current folder: %s",
            folder.empty() ? "All Folders" : folder.c_str());

        const auto& assets = registry.getFilteredAssets(folder);
        if (assets.empty()) {
            ImGui::TextDisabled("No hay videos en esta carpeta");
            ImGui::PopID();
            return;
        }

        // Defensive: selection may be stale if the folder changed or was empty before
        activeSelection = std::clamp(activeSelection, 0, (int)assets.size() - 1);
        slot.previewSelection = std::clamp(slot.previewSelection, 0, (int)assets.size() - 1);

        drawAssetCombo("Asset",
            folder,
            activeSelection,
            [](const std::string&){});

        // ── Clip selector ──
        const char* currentLabel = assets[slot.previewSelection].metadata.filename.c_str();
        std::string comboId = std::string("Clip##") + tag;
        if (ImGui::BeginCombo(comboId.c_str(), currentLabel)) {
            for (int i = 0; i < (int)assets.size(); ++i) {
                bool sel = (slot.previewSelection == i);
                if (ImGui::Selectable(assets[i].metadata.filename.c_str(), sel)) {
                    if (slot.previewSelection != i) slot.manualUnload = false;
                    slot.previewSelection = i;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const auto& meta = assets[slot.previewSelection].metadata;
        ImGui::Text("%dx%d  %.2f fps  %.1f s",
                    meta.width, meta.height, meta.fps, meta.duration);

        bool confirmClick = ImGui::Button("Enviar a escena");
        ImGui::SameLine();
        if (slotIndex == 0)
            ImGui::TextDisabled("Q = enviar");
        else if (slotIndex == 1)
            ImGui::TextDisabled("W = enviar");
        else
            ImGui::TextDisabled("E = enviar");
        if (confirmClick && applyCallback) {
            activeSelection = slot.previewSelection;
            applyCallback(meta.path);
        }

        // ── Randomizer ──
        ImGui::Separator();
        ImGui::Text("Randomizer %s", tag);
        if (slotIndex == 0) {
            changed |= drawRandomizerBlock(
                "##V1", r1, transDur,
                registry.getFilteredAssets(folder).size(),
                callbacks.onRandomizePreviewVideo1,
                callbacks.onRandomizeVideo
            );
        } else if (slotIndex == 1) {
            changed |= drawRandomizerBlock(
                "##V2", r2, transDur2,
                registry.getFilteredAssets(folder).size(),
                callbacks.onRandomizePreviewVideo2,
                callbacks.onRandomizeVideo2
            );
        } else {
            changed |= drawRandomizerBlock(
                "##V3", r3, transDur3,
                registry.getFilteredAssets(folder).size(),
                callbacks.onRandomizePreviewVideo3,
                callbacks.onRandomizeVideo3
            );
        }

        ImGui::PopID();
    };

    ImGui::Text("PREVIEW");
    ImGui::Separator();

    // ── Quick randomizers (on top) ──
    if (ImGui::Button("Randomize Post FX")) {
        randomizePostFxControls(controls, rng);
        changed = controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Randomize VJAY Basics")) {
        randomizeVJayBasicsControls(controls, rng);
        changed = controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Randomize VJAY Extra")) {
        randomizeVJayExtraControls(controls, rng);
        changed = controlsDirty = true;
    }
    ImGui::Separator();

    // ── Active renderer videos (always on top) ──
    auto baseName = [](const std::string& p) {
        size_t s = p.find_last_of("/\\");
        return (s == std::string::npos) ? p : p.substr(s + 1);
    };
    ImGui::PushStyleColor(ImGuiCol_Text, {0.4f, 1.0f, 0.4f, 1.0f});
    ImGui::Text("🎬 MIX 1: %s", video1Path.empty() ? "(vacío)" : baseName(video1Path).c_str());
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, {0.4f, 0.7f, 1.0f, 1.0f});
    ImGui::Text("🎬 MIX 2: %s", video2Path.empty() ? "(vacío)" : baseName(video2Path).c_str());
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.7f, 0.4f, 1.0f});
    ImGui::Text("🎬 MIX 3: %s", video3Path.empty() ? "(vacío)" : baseName(video3Path).c_str());
    ImGui::PopStyleColor();

    // ── Section 1: Video 1 ──
    if (ImGui::CollapsingHeader("Video 1", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(diag.videoReady ? "online" : "offline");
        updateSlotAndDrawImage("V1", previewSlotVideo1,
                               controls.playback.selectedVideoFolder,
                               selectedVideoAsset, 0);
        if (ImGui::SliderFloat("Speed##V1", &controls.playback.videoPlaybackRate, 0.1f, 5.f, "%.2fx")) {
            if (previewSlotVideo1.player)
                previewSlotVideo1.player->setPlaybackRate(controls.playback.videoPlaybackRate);
            if (callbacks.onSetVideoSpeed && !previewSlotVideo1.previewPath.empty())
                callbacks.onSetVideoSpeed(previewSlotVideo1.previewPath, controls.playback.videoPlaybackRate);
        }
        drawControls("V1", previewSlotVideo1,
                     controls.playback.selectedVideoFolder,
                     selectedVideoAsset,
                     callbacks.onReloadVideo, 0);
    }

    // ── Section 2: Video 2 ──
    if (ImGui::CollapsingHeader("Video 2", ImGuiTreeNodeFlags_DefaultOpen)) {
        updateSlotAndDrawImage("V2", previewSlotVideo2,
                               controls.playback.selectedVideo2Folder,
                               selectedVideoAsset2, 1);
        ImGui::BeginDisabled(!controls.playback.enableDualVideo);
        if (ImGui::SliderFloat("Speed##V2", &controls.playback.video2PlaybackRate, 0.1f, 5.f, "%.2fx")) {
            if (previewSlotVideo2.player)
                previewSlotVideo2.player->setPlaybackRate(controls.playback.video2PlaybackRate);
            if (callbacks.onSetVideoSpeed && !previewSlotVideo2.previewPath.empty())
                callbacks.onSetVideoSpeed(previewSlotVideo2.previewPath, controls.playback.video2PlaybackRate);
        }
        ImGui::EndDisabled();
        drawControls("V2", previewSlotVideo2,
                     controls.playback.selectedVideo2Folder,
                     selectedVideoAsset2,
                     callbacks.onReloadVideo2, 1);
    }

    // ── Section 3: Video 3 ──
    if (ImGui::CollapsingHeader("Video 3", ImGuiTreeNodeFlags_DefaultOpen)) {
        updateSlotAndDrawImage("V3", previewSlotVideo3,
                               controls.playback.selectedVideo3Folder,
                               selectedVideoAsset3, 2);
        if (ImGui::SliderFloat("Speed##V3", &controls.playback.video3PlaybackRate, 0.1f, 5.f, "%.2fx")) {
            if (previewSlotVideo3.player)
                previewSlotVideo3.player->setPlaybackRate(controls.playback.video3PlaybackRate);
            if (callbacks.onSetVideoSpeed && !previewSlotVideo3.previewPath.empty())
                callbacks.onSetVideoSpeed(previewSlotVideo3.previewPath, controls.playback.video3PlaybackRate);
        }
        drawControls("V3", previewSlotVideo3,
                     controls.playback.selectedVideo3Folder,
                     selectedVideoAsset3,
                     callbacks.onReloadVideo3, 2);
    }

    // ── Section 4: Video Preview Mix ──
    if (ImGui::CollapsingHeader("Video Preview Mix", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto baseName = [](const std::string& p) {
            size_t s = p.find_last_of("/\\");
            return (s == std::string::npos) ? p : p.substr(s + 1);
        };

        ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "🎬 MIX 1: %s", video1Path.empty() ? "(vacío)" : baseName(video1Path).c_str());
        ImGui::TextColored({0.4f, 0.7f, 1.0f, 1.0f}, "🎬 MIX 2: %s", video2Path.empty() ? "(vacío)" : baseName(video2Path).c_str());
        ImGui::TextColored({1.0f, 0.7f, 0.4f, 1.0f}, "🎬 MIX 3: %s", video3Path.empty() ? "(vacío)" : baseName(video3Path).c_str());

        ImGui::Separator();
        changed |= ImGui::SliderFloat("Mix V1", &controls.playback.videoMix, 0.f, 1.f);
        changed |= ImGui::Combo("Blend mode V1", &controls.blending.blendModeProcedural, BLEND_ITEMS);
        changed |= ImGui::SliderFloat("Blend amount V1", &controls.blending.blendProceduralMix, 0.f, 2.f, "%.2f");
        ImGui::BeginDisabled(!controls.playback.enableDualVideo);
        changed |= ImGui::SliderFloat("Mix V2", &controls.playback.video2Mix, 0.f, 1.f);
        changed |= ImGui::Combo("Blend mode V2", &controls.playback.video2BlendMode,
                                "Mix\0Add\0Multiply\0Screen\0Difference\0", 5);
        changed |= ImGui::SliderFloat("Mix V3", &controls.playback.video3Mix, 0.f, 1.f);
        changed |= ImGui::Combo("Blend mode V3", &controls.playback.video3BlendMode,
                                "Mix\0Add\0Multiply\0Screen\0Difference\0", 5);
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::Button("Randomize Post FX")) {
            randomizePostFxControls(controls, rng);
            changed = controlsDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize VJAY Basics")) {
            randomizeVJayBasicsControls(controls, rng);
            changed = controlsDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize VJAY Extra")) {
            randomizeVJayExtraControls(controls, rng);
            changed = controlsDirty = true;
        }

        ImGui::Separator();
        changed |= ImGui::SliderFloat("Brightness", &controls.post.masterBrightness, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Contrast", &controls.color.gradeContrast, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::Checkbox("Grid overlay", &controls.grid.enabled);
        ImGui::SameLine();
        if (ImGui::Checkbox("##lock_grid", &controls.locks.lockGrid)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock grid during randomize");
        ImGui::SameLine(); ImGui::TextDisabled(controls.locks.lockGrid ? "(locked)" : "(unlocked)");
        if (controls.grid.enabled) {
            changed |= ImGui::Combo("Grid mode", &controls.grid.mode, "Vertical\0Horizontal\0Matrix\0");
            if (controls.grid.mode == 2) {
                changed |= ImGui::SliderInt("Rows",    &controls.grid.rows,    1, 8);
                changed |= ImGui::SliderInt("Columns", &controls.grid.columns, 1, 8);
            } else {
                changed |= ImGui::SliderInt("Grid count", &controls.grid.count, 1, 8);
            }
            changed |= ImGui::Checkbox("Mirror cells",  &controls.grid.mirrorCells);
            changed |= ImGui::Checkbox("Show grid lines", &controls.grid.showLines);
        }

        ImGui::Separator();
        ImGui::TextColored({1.f,0.85f,0.3f,1.f}, "Locked Parameters");
        ImGui::Checkbox("Enable RGB Mix", &controls.post.enablePostColorBalance);
        ImGui::SameLine();
        if (ImGui::Checkbox("##lock_rgbmix", &controls.locks.lockColorBalance)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock RGB Mix during randomize");
        ImGui::SameLine(); ImGui::TextDisabled(controls.locks.lockColorBalance ? "(locked)" : "(unlocked)");
        if (controls.post.enablePostColorBalance) {
            changed |= ImGui::SliderFloat3("RGB Mix", glm::value_ptr(controls.color.colorBalance), 0.f, 2.f);
        }

        ImGui::Checkbox("Enable Threshold", &controls.fx.enableThreshold);
        ImGui::SameLine();
        if (ImGui::Checkbox("##lock_threshold", &controls.locks.lockThreshold)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock Threshold during randomize");
        ImGui::SameLine(); ImGui::TextDisabled(controls.locks.lockThreshold ? "(locked)" : "(unlocked)");
        if (controls.fx.enableThreshold) {
            changed |= ImGui::SliderFloat("Threshold level", &controls.fx.thresholdLevel, 0.0f, 1.0f, "%.2f");
        }
    }

    // ── Global keyboard shortcuts ──
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
        const auto& assets1 = registry.getFilteredAssets(controls.playback.selectedVideoFolder);
        if (!assets1.empty() && previewSlotVideo1.previewSelection >= 0 &&
            previewSlotVideo1.previewSelection < (int)assets1.size()) {
            selectedVideoAsset = previewSlotVideo1.previewSelection;
            if (callbacks.onReloadVideo)
                callbacks.onReloadVideo(assets1[previewSlotVideo1.previewSelection].metadata.path);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
        const auto& assets2 = registry.getFilteredAssets(controls.playback.selectedVideo2Folder);
        if (!assets2.empty() && previewSlotVideo2.previewSelection >= 0 &&
            previewSlotVideo2.previewSelection < (int)assets2.size()) {
            selectedVideoAsset2 = previewSlotVideo2.previewSelection;
            if (callbacks.onReloadVideo2)
                callbacks.onReloadVideo2(assets2[previewSlotVideo2.previewSelection].metadata.path);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
        const auto& assets3 = registry.getFilteredAssets(controls.playback.selectedVideo3Folder);
        int sel = std::clamp(previewSlotVideo3.previewSelection, 0, (int)assets3.size() - 1);
        if (!assets3.empty() && sel >= 0 && sel < (int)assets3.size()) {
            selectedVideoAsset3 = sel;
            if (callbacks.onReloadVideo3)
                callbacks.onReloadVideo3(assets3[sel].metadata.path);
        }
    }

    if (changed) {
        controlsDirty = true;
        if (callbacks.onControlsChanged)
            callbacks.onControlsChanged();
    }
}

void UISystem::forcePreviewShuffle(int slotIndex) {
    if (slotIndex < 0 || slotIndex > 2) return;
    previewShuffleRequested[slotIndex] = true;
    previewAutoRandomizeElapsed[slotIndex] = 0.0f;
}

void UISystem::processPreviewShuffles(VideoRegistry& registry,
                                      int& selAsset, int& selAsset2, int& selAsset3,
                                      const std::string& video1Folder,
                                      const std::string& video2Folder,
                                      const std::string& video3Folder) {
    auto apply = [&](int slotIndex, VideoPreviewSlot& slot, const std::string& folder, int& activeSelection) {
        if (!previewShuffleRequested[slotIndex]) return;
        previewShuffleRequested[slotIndex] = false;
        const auto& assets = registry.getFilteredAssets(folder);
        if (assets.empty()) {
            std::cout << "[shuffle] slot " << slotIndex << " folder empty: " << folder << "\n";
            return;
        }
        activeSelection = std::clamp(activeSelection, 0, (int)assets.size() - 1);
        if (slot.previewSelection < 0)
            slot.previewSelection = activeSelection;
        slot.previewSelection = std::clamp(slot.previewSelection, 0, (int)assets.size() - 1);
        if (assets.size() > 1) {
            std::uniform_int_distribution<int> dist(0, static_cast<int>(assets.size()) - 1);
            int newSelection;
            do { newSelection = dist(previewRng); }
            while (newSelection == slot.previewSelection);
            slot.previewSelection = newSelection;
            slot.previewSelection = std::clamp(slot.previewSelection, 0, (int)assets.size() - 1);
            std::cout << "[shuffle] slot " << slotIndex << " -> " << slot.previewSelection
                      << " (" << assets[slot.previewSelection].metadata.filename << ")\n";
        } else {
            std::cout << "[shuffle] slot " << slotIndex << " only 1 video in folder\n";
        }
    };

    apply(0, previewSlotVideo1, video1Folder, selAsset);
    apply(1, previewSlotVideo2, video2Folder, selAsset2);
    apply(2, previewSlotVideo3, video3Folder, selAsset3);
}

void UISystem::randomizeVJayBasics(VisualControls& controls) {
    randomizeVJayBasicsControls(controls, previewRng);
}

void UISystem::randomizeVJayExtra(VisualControls& controls) {
    randomizeVJayExtraControls(controls, previewRng);
}

void UISystem::destroyPreviewSlot(VideoPreviewSlot& slot) {
    if (slot.texture) {
        SDL_DestroyTexture(slot.texture);
        slot.texture = nullptr;
    }
    if (slot.player) {
        slot.player->shutdown();
        slot.player.reset();
    }
    slot.frameBuffer.clear();
    slot.loadedPath.clear();
    slot.previewPath.clear();
    slot.lastError.clear();
    slot.textureWidth = slot.textureHeight = 0;
    slot.frameAccumulator = 0.0f;
    slot.paused = false;
}

bool UISystem::loadPreview(VideoPreviewSlot& slot, const std::string& path) {
    slot.previewPath = path;
    slot.manualUnload = false;
    if (!renderer || path.empty()) {
        slot.lastError = path.empty() ? "Selecciona un video para previsualizar" : "Renderer no disponible";
        return false;
    }

    if (!slot.player) slot.player = std::make_unique<VideoPlayer>();
    slot.player->shutdown();
    slot.frameBuffer.clear();
    slot.textureWidth = slot.textureHeight = 0;
    slot.frameAccumulator = 0.0f;

    // Decode preview at lower resolution (320x180) to improve performance
    // The UI never shows previews larger than this and it reduces CPU load
    if (!slot.player->initialize(path, 320, 180)) {
        slot.lastError = "No se pudo abrir el video";
        slot.loadedPath.clear();
        return false;
    }

    slot.loadedPath = path;
    slot.lastError.clear();
    slot.paused = false;
    return true;
}

bool UISystem::ensurePreviewTexture(VideoPreviewSlot& slot, int width, int height) {
    if (!renderer || width <= 0 || height <= 0) return false;

    if (slot.texture && (slot.textureWidth != width || slot.textureHeight != height)) {
        SDL_DestroyTexture(slot.texture);
        slot.texture = nullptr;
    }

    if (!slot.texture) {
        slot.texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height);
        if (!slot.texture) {
            slot.lastError = "No se pudo crear textura SDL";
            return false;
        }
        SDL_SetTextureBlendMode(slot.texture, SDL_BLENDMODE_BLEND);
    }

    slot.textureWidth  = width;
    slot.textureHeight = height;
    return true;
}

bool UISystem::decodePreviewFrame(VideoPreviewSlot& slot) {
    if (!slot.player || !slot.player->isReady()) return false;

    int width = 0;
    int height = 0;
    if (!slot.player->grabFrame(slot.frameBuffer, width, height)) return false;
    
    // Create texture at original size first, then let SDL handle the scaling
    // This is more efficient than re-decoding at different resolutions
    if (!ensurePreviewTexture(slot, width, height)) return false;

    const int stride = width * 4;
    SDL_UpdateTexture(slot.texture, nullptr, slot.frameBuffer.data(), stride);
    return true;
}

void UISystem::updatePreviewSlot(VideoPreviewSlot& slot, float deltaTime) {
    if (!slot.player || !slot.player->isReady() || !enableVideoPreviews || slot.paused) return;

    // Very aggressive preview optimization - only 5 FPS for previews
    // This significantly reduces CPU load from video decoding
    constexpr float previewFrameTime = 1.0f / 5.0f;
    slot.frameAccumulator += deltaTime;
    if (slot.frameAccumulator < previewFrameTime) return;

    // Clamp accumulator to avoid a catch-up burst after switching to this tab
    if (slot.frameAccumulator > previewFrameTime * 2.0f)
        slot.frameAccumulator = previewFrameTime * 2.0f;

    slot.frameAccumulator -= previewFrameTime;
    
    // Skip frame decoding if we're running too slow (simple frame skip)
    static float lastDecodeTime = 0.0f;
    if (deltaTime > 0.1f) { // If frame time > 100ms, skip this decode
        return;
    }
    
    decodePreviewFrame(slot);
}

// ═════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════════════════

UISystem::~UISystem() { shutdown(); }

bool UISystem::initialize(SDL_Window* win, SDL_Renderer* ren, float scale) {
    if (initialized) return true;
    window   = win;
    renderer = ren;
    dpiScale = scale;

    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();

    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigNavCaptureKeyboard = false; // Don't let keyboard navigation eat global hotkeys
    io.IniFilename  = "imgui.ini";

    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.f;
    ImGui::GetStyle().FrameRounding  = 4.f;

    // Apply DPI scaling to ImGui style and fonts
    if (dpiScale > 1.0f) {
        ImGui::GetStyle().ScaleAllSizes(dpiScale);
        ImGui::GetStyle().FontScaleDpi = dpiScale;
    }

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ImGui::DestroyContext(context); context = nullptr; return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(context); context = nullptr; return false;
    }
    initialized = true;
    return true;
}

void UISystem::shutdown() {
    if (!initialized) return;
    ImGui::SetCurrentContext(context);
    const char* ini = ImGui::GetIO().IniFilename ? ImGui::GetIO().IniFilename : "imgui.ini";
    std::cout << "[UISystem] Saving ImGui settings to: " << ini << "\n";
    ImGui::SaveIniSettingsToDisk(ini);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(context);
    context     = nullptr;
    initialized = false;

    destroyPreviewSlot(previewSlotVideo1);
    destroyPreviewSlot(previewSlotVideo2);
    destroyPreviewSlot(previewSlotVideo3);
}

bool UISystem::processEvent(const SDL_Event& ev) {
    if (initialized) return ImGui_ImplSDL2_ProcessEvent(&ev);
    return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// Frame
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::render(
    VisualControls& controls, VideoRandomizerState& randomizer,
    VideoRandomizerState2& randomizer2, VideoRandomizerState2& randomizer3,
    VideoPlayer& player, VideoPlayer& player2, VideoPlayer& player3,
    VideoRegistry& registry, int& selAsset, int& selAsset2, int& selAsset3,
    float& transDur, float& transDur2, float& transDur3,
    bool& allowDimChange,
    bool& controlsDirty, std::mt19937& rng, const UIDiagnostics& diag,
    const UICallbacks& callbacks, MidiSystem& midiSystem,
    OscSystem& oscSystem, AudioSystem& audioSystem,
    const std::string& video1Path, const std::string& video2Path, const std::string& video3Path,
    const std::string& videoAssetsRoot)
{
    if (!initialized || !renderer) return;
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    try {
    // Apply preview shuffles requested by hotkeys (1/2/3) even when the Preview tab is not open.
    processPreviewShuffles(registry, selAsset, selAsset2, selAsset3,
                           controls.playback.selectedVideoFolder,
                           controls.playback.selectedVideo2Folder,
                           controls.playback.selectedVideo3Folder);

    if (rendererPaused) {
        // Minimal paused overlay: negligible CPU/GPU cost
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize({280, 100});
        if (ImGui::Begin("UI Paused", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextColored({1.f, 0.85f, 0.3f, 1.f}, "UI Renderer is PAUSED");
            ImGui::TextDisabled("CPU/GPU usage reduced");
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            if (ImGui::Button("Resume (P)", {200, 30})) {
                rendererPaused = false;
            }
            ImGui::PopItemFlag();
        }
        ImGui::End();
    } else {
        drawMainNavbar(controls, randomizer, randomizer2, randomizer3, player, player2, player3, registry,
                       selAsset, selAsset2, selAsset3, transDur, transDur2, transDur3,
                       allowDimChange, controlsDirty, rng, diag, callbacks,
                       midiSystem, oscSystem, audioSystem, video1Path, video2Path, video3Path,
                       videoAssetsRoot);

        if (showDemoWindow) ImGui::ShowDemoWindow(&showDemoWindow);
    }
    } catch (const std::exception& e) {
        std::cerr << "[UISystem] Exception during UI render: " << e.what() << "\n";
        ImGui::EndFrame();
        return;
    } catch (...) {
        std::cerr << "[UISystem] Unknown exception during UI render\n";
        ImGui::EndFrame();
        return;
    }

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    SDL_SetRenderDrawColor(renderer, 12, 12, 12, 255);
    SDL_RenderClear(renderer);
    if (dd && dd->DisplaySize.x > 0 && dd->DisplaySize.y > 0) {
        SDL_RenderSetScale(renderer, dd->FramebufferScale.x, dd->FramebufferScale.y);
        ImGui_ImplSDLRenderer2_RenderDrawData(dd, renderer);
    }
    SDL_RenderPresent(renderer);
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Presets
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawPresetsContent(VisualControls& controls, bool& controlsDirty, const UICallbacks& callbacks) {
    if (!callbacks.onListPresets || !callbacks.onSavePreset || !callbacks.onLoadPreset || !callbacks.onDeletePreset || !callbacks.onRenamePreset) {
        ImGui::Text("Preset callbacks not configured.");
        return;
    }

    ImGui::InputText("Name", presetNameBuffer, sizeof(presetNameBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string name(presetNameBuffer);
        if (!name.empty()) {
            if (callbacks.onSavePreset(name)) {
                // clear name after save
                presetNameBuffer[0] = '\0';
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Saved Presets:");

    auto presets = callbacks.onListPresets();
    if (presets.empty()) {
        ImGui::TextDisabled("No presets saved yet.");
    }

    for (const auto& name : presets) {
        ImGui::PushID(name.c_str());
        if (ImGui::Button("Load")) {
            if (callbacks.onLoadPreset(name)) {
                controlsDirty = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename")) {
            renamingPreset = name;
            std::strncpy(presetNameBuffer, name.c_str(), sizeof(presetNameBuffer) - 1);
            presetNameBuffer[sizeof(presetNameBuffer) - 1] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            callbacks.onDeletePreset(name);
        }
        ImGui::SameLine();
        ImGui::Text("%s", name.c_str());

        // Inline rename controls
        if (renamingPreset == name) {
            ImGui::Indent();
            ImGui::InputText("New name", presetNameBuffer, sizeof(presetNameBuffer));
            if (ImGui::Button("Confirm")) {
                std::string newName(presetNameBuffer);
                if (!newName.empty() && newName != name) {
                    callbacks.onRenamePreset(name, newName);
                }
                renamingPreset.clear();
                presetNameBuffer[0] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                renamingPreset.clear();
                presetNameBuffer[0] = '\0';
            }
            ImGui::Unindent();
        }

        ImGui::PopID();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Main navbar (entry point de tabs)
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawMainNavbar(
    VisualControls& controls, VideoRandomizerState& randomizer,
    VideoRandomizerState2& randomizer2, VideoRandomizerState2& randomizer3,
    VideoPlayer& player, VideoPlayer& player2, VideoPlayer& player3,
    VideoRegistry& registry, int& selAsset, int& selAsset2, int& selAsset3,
    float& transDur, float& transDur2, float& transDur3,
    bool& allowDimChange,
    bool& controlsDirty, std::mt19937& rng, const UIDiagnostics& diag,
    const UICallbacks& callbacks, MidiSystem& midiSystem,
    OscSystem& oscSystem, AudioSystem& audioSystem,
    const std::string& video1Path, const std::string& video2Path, const std::string& video3Path,
    const std::string& videoAssetsRoot)
{
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (!ImGui::Begin("VulkanCpp Controls", nullptr, kFlags)) { ImGui::End(); return; }

    if (ImGui::BeginTabBar("MainTabBar")) {
        if (ImGui::BeginTabItem("Procedural")) {
            drawProceduralControlsContent(controls, randomizer, player, registry,
                selAsset, selAsset2, selAsset3, transDur, allowDimChange,
                controlsDirty, rng, diag, callbacks);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Video")) {
            drawVideoContent(controls, randomizer, randomizer2, randomizer3, registry,
                selAsset, selAsset2, selAsset3, transDur, transDur2, transDur3,
                allowDimChange, controlsDirty, diag, callbacks, videoAssetsRoot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Post FX"))     { drawPostFxContent(controls, controlsDirty, rng);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("VJAY Basics")) { drawVJayBasicsContent(controls, controlsDirty, rng); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("VJAY Extra"))  { drawVJayExtraContent(controls, controlsDirty, rng);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("NLE Editor"))  { drawNLEEditorContent(callbacks, video1Path, video2Path, video3Path); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Preview")) {
            drawPreviewContent(controls, registry, selAsset, selAsset2, selAsset3,
                randomizer, randomizer2, randomizer3, transDur, transDur2, transDur3,
                controlsDirty, diag, callbacks, video1Path, video2Path, video3Path, rng);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics")) {
            drawDiagnosticsContent(diag, player, player2, player3, registry,
                selAsset, selAsset2, selAsset3, controls, callbacks);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Performance")) {
            drawPerformanceContent(diag);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MIDI"))       { drawMidiControlsContent(midiSystem);         ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("OSC"))        { drawOscControlsContent(oscSystem);           ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Audio"))      { drawAudioDebugContent(audioSystem, controls); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Parameters")) { drawParameterIndexContent();                  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Presets"))    { drawPresetsContent(controls, controlsDirty, callbacks); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Procedural
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawProceduralControlsContent(
    VisualControls& controls, VideoRandomizerState& /*randomizer*/,
    VideoPlayer& /*player*/, VideoRegistry& /*registry*/,
    int& /*selAsset*/, int& /*selAsset2*/, int& /*selAsset3*/,
    float& /*transDur*/, bool& /*allowDimChange*/,
    bool& controlsDirty, std::mt19937& rng,
    const UIDiagnostics& /*diag*/, const UICallbacks& callbacks)
{
    bool changed = false;

    ImGui::Text("Animation");
    changed |= ImGui::SliderFloat("Speed",            &controls.playback.animationSpeed,         0.1f, 8.f, "%.2fx");
    changed |= ImGui::SliderFloat("Target change (s)",&controls.playback.animationTargetSeconds, 0.1f, 5.f, "%.2fs");
    ImGui::SameLine();
    if (ImGui::Button("Snap 1s")) {
        controls.playback.animationTargetSeconds = 1.f;
        controls.playback.animationSpeed         = 1.f;
        changed = true;
    }

    ImGui::Separator();
    ImGui::Text("Layers");
    changed |= drawActiveLayerCombo("Active Layer", controls.playback.activeMode);

    // Arrow buttons for cycling procedural layers
    ImGui::SameLine();
    if (ImGui::Button("<")) {
        controls.playback.activeMode = cycleLayerMode(controls.playback.activeMode, -1);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        controls.playback.activeMode = cycleLayerMode(controls.playback.activeMode, 1);
        changed = true;
    }

    ImGui::Separator();
    ImGui::Text("Camera");
    changed |= ImGui::SliderFloat("Zoom", &controls.camera.zoom, 0.01f, 5.0f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Color Palette");
    changed |= ImGui::ColorEdit4("Primary",   glm::value_ptr(controls.color.primaryColor));
    changed |= ImGui::ColorEdit4("Secondary", glm::value_ptr(controls.color.secondaryColor));
    changed |= ImGui::SliderFloat("Blend",    &controls.color.colorBlend, 0.f, 1.f);

    if (ImGui::Button("Randomize Colors")) {
        float pH = randFloat(rng, 0.f, 360.f);
        float pS = randFloat(rng, 0.6f, 1.f);
        float pV = randFloat(rng, 0.7f, 1.f);
        controls.color.primaryColor        = {hsvToRgb(pH, pS, pV), 1.f};
        controls.color.primaryColorTarget  = controls.color.primaryColor;
        controls.color.secondaryColor      = {hsvToRgb(std::fmod(pH + 180.f, 360.f), pS, pV), 1.f};
        controls.color.secondaryColorTarget= controls.color.secondaryColor;
        changed = controlsDirty = true;
    }
    changed |= ImGui::Checkbox("Auto Randomize", &controls.color.autoRandomizeColors);
    if (controls.color.autoRandomizeColors)
        changed |= ImGui::SliderFloat("Interval (s)", &controls.color.colorRandomizeInterval, 0.1f, 5.f, "%.1fs");

    ImGui::Separator();
    ImGui::Text("Audio-inspired inputs");
    if (controls.system.enableAudioReactive) {
        ImGui::BeginDisabled(true);
        ImGui::SliderFloat("Tempo (auto)", &controls.playback.tempo, 0.0f, 5.0f, "%.2fx");
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::TextDisabled("(energy driven)");
    } else {
        changed |= ImGui::SliderFloat("Tempo",  &controls.playback.tempo, 0.0f, 5.0f, "%.2fx");
    }
    changed |= ImGui::Checkbox("Auto tempo LFO", &controls.playback.enableTempoLfo);
    if (controls.playback.enableTempoLfo) {
        changed |= ImGui::SliderFloat("LFO speed (Hz)", &controls.playback.tempoLfoSpeed, 0.05f, 4.f, "%.2f Hz");
        changed |= ImGui::SliderFloat("LFO depth",      &controls.playback.tempoLfoDepth, 0.f, 2.f);
    }
    changed |= ImGui::SliderFloat("Energy",                   &controls.audio.energy,        0.f, 1.f);
    changed |= ImGui::SliderFloat("Bass",                     &controls.audio.bass,          0.f, 1.f);
    changed |= ImGui::SliderFloat("Mid",                      &controls.audio.mid,           0.f, 1.f);
    changed |= ImGui::SliderFloat("High",                     &controls.audio.high,          0.f, 1.f);
    changed |= ImGui::SliderFloat("High gain boost",          &controls.audio.highGain,      0.5f, 4.f, "%.2fx");
    changed |= ImGui::SliderFloat("Procedural audio drive",   &controls.audio.reactiveDrive, 0.5f, 3.f, "%.2fx");

    if (changed) { controlsDirty = true; if (callbacks.onControlsChanged) callbacks.onControlsChanged(); }
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Video
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawVideoContent(
    VisualControls& c, VideoRandomizerState& r1, VideoRandomizerState2& r2, VideoRandomizerState2& r3,
    VideoRegistry& registry, int& selAsset, int& selAsset2, int& selAsset3,
    float& transDur, float& transDur2, float& transDur3,
    bool& allowDimChange, bool& controlsDirty,
    const UIDiagnostics& diag, const UICallbacks& callbacks,
    const std::string& videoAssetsRoot)
{
    bool changed = false;

    // ─────────────────────────────────────────────────────────────
    // VIDEO ASSETS ROOT FOLDER
    // ─────────────────────────────────────────────────────────────
    ImGui::Text("Video Assets Root");
    ImGui::Separator();

    {
        char rootBuf[1024];
        std::strncpy(rootBuf, videoAssetsRoot.c_str(), sizeof(rootBuf) - 1);
        rootBuf[sizeof(rootBuf) - 1] = '\0';
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - 200);
        ImGui::InputText("##videoAssetsRoot", rootBuf, sizeof(rootBuf));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
#ifdef _WIN32
            std::string picked = pickFolderDialog(videoAssetsRoot);
            if (!picked.empty() && callbacks.onVideoAssetsRootChanged) {
                callbacks.onVideoAssetsRootChanged(picked);
            }
#endif
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply")) {
            if (callbacks.onVideoAssetsRootChanged) {
                callbacks.onVideoAssetsRootChanged(std::string(rootBuf));
            }
        }
        if (videoAssetsRoot.empty()) {
            ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "No folder set — click Browse to select your video folder");
        } else if (!std::filesystem::exists(videoAssetsRoot)) {
            ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "Folder does not exist: %s", videoAssetsRoot.c_str());
        }
    }

    ImGui::Spacing();

    // ─────────────────────────────────────────────────────────────
    // GLOBAL VIDEO TWEAKS
    // ─────────────────────────────────────────────────────────────
    ImGui::Text("Video Tweaks (Global)");
    ImGui::Separator();

    changed |= ImGui::Checkbox("Enable dual video", &c.playback.enableDualVideo);

    changed |= ImGui::SliderFloat("Grayscale", &c.playback.grayscaleAmount, 0.f, 1.f);
    changed |= ImGui::SliderFloat("Sharpen",    &c.playback.sharpenAmount,   0.f, 1.f);

    changed |= ImGui::Checkbox("Bicubic Upscale",  &c.playback.upscaleEnabled);
    changed |= ImGui::Checkbox("Auto Scale Video", &c.playback.autoScaleVideo);

    const char* aspectItems = "Original\0" "4:3\0" "16:9\0" "19:10\0";
    changed |= ImGui::Combo("Force output aspect", &c.playback.outputAspectRatio, aspectItems);

    if (ImGui::SliderFloat("Decode oversample", &c.playback.videoDecodeOversample, 1.f, 8.f, "%.1fx"))
    {
        c.playback.videoDecodeOversample =
            std::clamp(c.playback.videoDecodeOversample, 1.f, 8.f);
        changed = true;
    }

    {
        static const char* fpsLabels[] = {"Off","15 fps","24 fps","30 fps","60 fps"};
        int idx = std::clamp(c.playback.forcedFpsIndex, 0,
                             (int)FORCED_FPS_OPTIONS_UI.size() - 1);

        if (ImGui::Combo("Force FPS", &idx, fpsLabels, IM_ARRAYSIZE(fpsLabels)))
        {
            c.playback.forcedFpsIndex = idx;
            changed = true;
        }
    }

    changed |= ImGui::SliderFloat("Loop crossfade (s)", &c.playback.loopBlendSeconds, 0.f, 2.f, "%.2f s");
    changed |= ImGui::Checkbox("Allow dimension change", &allowDimChange);

    // ─────────────────────────────────────────────────────────────
    // VIDEO 1 & VIDEO 2 per-slot controls moved to Preview tab
    // ─────────────────────────────────────────────────────────────

    ImGui::Separator();
    TOGGLED_SECTION("Feedback blending", c.blending.enableBlending,
        changed |= ImGui::Combo("Feedback blend", &c.blending.blendModeFeedback, BLEND_ITEMS);
        changed |= ImGui::SliderFloat("Feedback mix", &c.blending.blendFeedbackMix, 0,2,"%.2f");
    )

    // ─────────────────────────────────────────────────────────────
    // FINALIZE
    // ─────────────────────────────────────────────────────────────
    if (changed)
    {
        controlsDirty = true;
        if (callbacks.onControlsChanged)
            callbacks.onControlsChanged();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Post FX
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::setPostEffectNames(const std::vector<std::string>& names) {
    postEffectNames = names;
}

int UISystem::cycleLayerMode(int currentMode, int delta) const {
    return ::cycleLayerMode(currentMode, delta);
}

std::string UISystem::cyclePostEffect(const std::string& current, int delta) const {
    if (postEffectNames.empty()) return current;

    auto it = std::find(postEffectNames.begin(), postEffectNames.end(), current);
    int idx = (it != postEffectNames.end()) ? static_cast<int>(it - postEffectNames.begin()) : -1;

    if (idx < 0 && !current.empty()) idx = 0;

    idx = (idx + delta) % static_cast<int>(postEffectNames.size());
    if (idx < 0) idx += static_cast<int>(postEffectNames.size());

    return postEffectNames[idx];
}

void UISystem::drawPostFxContent(VisualControls& c, bool& controlsDirty, std::mt19937& rng) {
    bool changed = false;

    auto setAll = [&](bool v) {
        c.post.enablePostCrtCurvature = c.post.enablePostScanMask   = c.post.enablePostVignette =
        c.post.enablePostFishEye      = c.post.enablePostBloom      = c.post.enablePostAberration =
        c.post.enablePostGrain        = c.post.enablePostBend       = c.post.enablePostGlitch    =
        c.post.enablePostColorBalance = v;
    };

    if (ImGui::Button("Randomize Post FX")) {
        randomizePostFxControls(c, rng);
        controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Post FX")) {
        setAll(false);
        c.post.crtCurvature = c.post.crtHorizontalCurvature = 0.15f;
        c.post.crtScanlineIntensity = c.post.crtMaskIntensity = 0.35f;
        c.post.crtVignette = 0.55f; c.post.crtFishEye = 0.f;
        c.post.bloomIntensity = 0.45f; c.post.bloomThreshold = 0.7f;
        c.post.aberrationAmount = 0.02f; c.post.grainStrength = 0.15f;
        c.fx.bendAmount = c.fx.glitchAmount = 0.f;
        c.color.colorBalance = glm::vec3(1.f);
        changed = controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Enable All"))  { setAll(true);  changed = controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) { setAll(false); changed = controlsDirty = true; }
    ImGui::Separator();

#define PFX(label, tog, ...) \
    { changed |= ImGui::Checkbox(label, &(tog)); ImGui::BeginDisabled(!(tog)); __VA_ARGS__ ImGui::EndDisabled(); }

    PFX("CRT Curvature", c.post.enablePostCrtCurvature,
        changed |= ImGui::SliderFloat("CRT Curvature V", &c.post.crtCurvature,           0.f, 0.6f, "%.2f");
        changed |= ImGui::SliderFloat("CRT Curvature H", &c.post.crtHorizontalCurvature, 0.f, 0.6f, "%.2f");)
    PFX("Scanlines / Mask", c.post.enablePostScanMask,
        changed |= ImGui::SliderFloat("CRT Scanlines", &c.post.crtScanlineIntensity, 0.f,1.f,"%.2f");
        changed |= ImGui::SliderFloat("CRT Mask",      &c.post.crtMaskIntensity,     0.f,1.f,"%.2f");)
    PFX("Vignette",  c.post.enablePostVignette,
        changed |= ImGui::SliderFloat("CRT Black Bars",&c.post.crtVignette, 0.f,1.f,"%.2f");)
    PFX("Fish-eye",  c.post.enablePostFishEye,
        changed |= ImGui::SliderFloat("CRT Fish-eye",  &c.post.crtFishEye, -1.f,1.f,"%.2f");)
    PFX("Bloom",     c.post.enablePostBloom,
        changed |= ImGui::SliderFloat("Bloom Intensity",&c.post.bloomIntensity, 0.f,2.f,"%.2f");
        changed |= ImGui::SliderFloat("Bloom Threshold",&c.post.bloomThreshold, 0.f,1.f,"%.2f");)
    PFX("Aberration##Toggle", c.post.enablePostAberration,
        changed |= ImGui::SliderFloat("Aberration",&c.post.aberrationAmount,-0.05f,0.05f,"%.3f");)
    PFX("Film Grain##Toggle", c.post.enablePostGrain,
        changed |= ImGui::SliderFloat("Film Grain",&c.post.grainStrength,0.f,0.5f,"%.2f");)
    PFX("Screen Bend",  c.post.enablePostBend,
        changed |= ImGui::SliderFloat("Bend Amount",&c.fx.bendAmount,0.f,0.5f,"%.2f");)
    PFX("Glitch wrapper",c.post.enablePostGlitch,
        changed |= ImGui::SliderFloat("Glitch Intensity",&c.fx.glitchAmount,0.f,1.f,"%.2f");)
#undef PFX

    ImGui::Separator();
    ImGui::Text("Post-Effect Slot");
    if (!postEffectNames.empty()) {
        std::vector<const char*> items;
        items.push_back("(None)");
        for (const auto& name : postEffectNames) items.push_back(name.c_str());
        int current = 0;
        for (size_t i = 0; i < postEffectNames.size(); ++i) {
            if (postEffectNames[i] == c.post.postEffectName) current = static_cast<int>(i + 1);
        }

        auto applyPostEffectSelection = [&](int newIndex) {
            if (newIndex < 0) newIndex = static_cast<int>(items.size()) - 1;
            if (newIndex >= static_cast<int>(items.size())) newIndex = 0;
            current = newIndex;
            c.post.postEffectName = (current == 0) ? "" : postEffectNames[current - 1];
            changed = true;
        };

        // Keyboard navigation: Up/Down cycles through the available post-effect slots.
        // This only runs while the Post FX tab is being drawn.
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            applyPostEffectSelection(current - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            applyPostEffectSelection(current + 1);
        }

        if (ImGui::Combo("Effect", &current, items.data(), static_cast<int>(items.size()))) {
            c.post.postEffectName = (current == 0) ? "" : postEffectNames[current - 1];
            changed = true;
        }
        if (!c.post.postEffectName.empty()) {
            changed |= ImGui::SliderFloat("Strength", &c.post.postEffectStrength, 0.f, 1.f, "%.2f");
            changed |= ImGui::SliderFloat("Intensity", &c.post.postEffectIntensity, 0.f, 1.f, "%.2f");
            changed |= ImGui::SliderInt("Mode", &c.post.postEffectMode, 0, 3);
            changed |= ImGui::ColorEdit3("RGB Adjust", &c.post.postEffectRgbAdjust.x);
        }
    } else {
        ImGui::TextDisabled("No post-effect shaders loaded");
    }

    if (changed) controlsDirty = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: VJAY Basics
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawVJayBasicsContent(VisualControls& c, bool& controlsDirty, std::mt19937& rng) {
    bool changed = false;
    static const char* LUT_ITEMS = "Off\0Filmic\0Neon\0Noir\0Heatmap\0Analog CRT\0";

    auto setAll = [&](bool v) {
        c.color.enableColorGrading = c.temporal.enableFeedback = c.fx.enableDistortion =
        c.fx.enableBlurMotion      = c.fx.enableSharpen        = c.fx.enableGlitch     =
        c.blending.enableBlending  = c.post.enableAnalog       =
        c.temporal.enableTemporal  = v;
    };
    auto reset = [&]() {
        setAll(false);
        c.color.gradeBrightness = 0;  c.color.gradeContrast = 1;  c.color.gradeSaturation = 1;
        c.color.gradeHueShift = 0;    c.color.gradeGamma = 1;     c.color.colorLUTIndex = 0;
        c.color.splitToneBalance = 0.5f;
        c.color.splitToneShadows = glm::vec3(0);  c.color.splitToneHighlights = glm::vec3(1);
        c.temporal.feedbackAmount = c.temporal.trailStrength = c.temporal.temporalAccumulation =
        c.temporal.feedbackDecay  = c.temporal.recursiveBlend = 0;
        c.fx.uvWarpStrength = c.fx.rippleStrength = 0;  c.fx.rippleFrequency = 1;
        c.fx.swirlStrength = c.fx.displacementAmount = 0;  c.fx.kaleidoSegments = 6;
        c.fx.tunnelDepth = c.fx.tunnelCurvature = 0;
        c.fx.gaussianBlur = c.fx.directionalBlur = c.fx.directionalBlurAngle = 0;
        c.fx.zoomBlur = c.fx.motionBlur = c.fx.temporalBlur = 0;
        c.fx.unsharpMask = c.fx.casAmount = c.fx.localContrast = 0;
        c.fx.glitchDatamosh = c.fx.glitchRGBSplit = c.fx.glitchScanlineBreak =
        c.fx.glitchJitter   = c.fx.glitchTearing  = c.fx.glitchPixelSort =
        c.fx.glitchBufferCorruption = 0;
        c.blending.blendModeProcedural = 0;  c.blending.blendModeVideo = 1;  c.blending.blendModeFeedback = 2;
        c.blending.blendProceduralMix  = 1;  c.blending.blendVideoMix  = 1;  c.blending.blendFeedbackMix  = 0.5f;
        c.post.analogScanlineFocus = 0.5f;   c.post.analogMaskBalance = 0.5f; c.post.analogNoise = 0.2f;
        c.post.analogBloom = 0.3f;           c.post.vhsDistortion = 0;        c.post.analogChromaticAberration = 0.02f;
        c.playback.temporalInterpolation = c.playback.temporalBlendStrength = 0;
        c.playback.slowMotionFactor = 1;  c.playback.frameAccumulation = 0;
    };

    if (ImGui::Button("Randomize VJAY basics")) { randomizeVJayBasicsControls(c, rng); changed = controlsDirty = true; }
    ImGui::SameLine(); if (ImGui::Button("Turn all ON"))  { setAll(true);  changed = controlsDirty = true; }
    ImGui::SameLine(); if (ImGui::Button("Turn all OFF")) { setAll(false); changed = controlsDirty = true; }
    ImGui::SameLine(); if (ImGui::Button("Reset"))        { reset();       changed = controlsDirty = true; }

    TOGGLED_SECTION("1. Color grading dinamico", c.color.enableColorGrading,
        changed |= ImGui::SliderFloat("Brightness",        &c.color.gradeBrightness,  -0.5f, 0.5f,   "%.2f");
        changed |= ImGui::SliderFloat("Contrast",          &c.color.gradeContrast,     0.1f,2.5f,  "%.2f");
        changed |= ImGui::SliderFloat("Saturation",        &c.color.gradeSaturation,   0.f, 2.5f,  "%.2f");
        changed |= ImGui::SliderFloat("Hue shift",         &c.color.gradeHueShift,  -180.f,180.f,  "%.1f\xc2\xb0");
        changed |= ImGui::SliderFloat("Gamma",             &c.color.gradeGamma,        0.4f,3.f,   "%.2f");
        changed |= ImGui::Combo("LUT",                     &c.color.colorLUTIndex,     LUT_ITEMS);
        changed |= ImGui::SliderFloat("Split tone balance",&c.color.splitToneBalance,  0.f, 1.f,   "%.2f");
        changed |= ImGui::ColorEdit3("Split tone shadows",     glm::value_ptr(c.color.splitToneShadows));
        changed |= ImGui::ColorEdit3("Split tone highlights",  glm::value_ptr(c.color.splitToneHighlights));
    )
    TOGGLED_SECTION("2. Feedback temporal", c.temporal.enableFeedback,
        changed |= ImGui::SliderFloat("Feedback",              &c.temporal.feedbackAmount,      0,1,"%.2f");
        changed |= ImGui::SliderFloat("Trails",                &c.temporal.trailStrength,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Temporal accumulation", &c.temporal.temporalAccumulation,0,1,"%.2f");
        changed |= ImGui::SliderFloat("Decay",                 &c.temporal.feedbackDecay,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Recursive blend",       &c.temporal.recursiveBlend,      0,1,"%.2f");
    )
    TOGGLED_SECTION("3. Distorsion espacial", c.fx.enableDistortion,
        changed |= ImGui::SliderFloat("UV warp",      &c.fx.uvWarpStrength,    0.f,  0.5f,"%.3f");
        changed |= ImGui::SliderFloat("Ripple",       &c.fx.rippleStrength,    0.f,  0.5f,"%.3f");
        changed |= ImGui::SliderFloat("Ripple freq",  &c.fx.rippleFrequency,   0.5f, 6.f, "%.1f");
        changed |= ImGui::SliderFloat("Swirl",        &c.fx.swirlStrength,    -0.5f, 0.5f,"%.3f");
        changed |= ImGui::SliderFloat("Displacement", &c.fx.displacementAmount,0.f,  0.5f,"%.3f");
        changed |= ImGui::SliderFloat("Kaleido segs", &c.fx.kaleidoSegments,   3.f, 12.f, "%.0f");
        changed |= ImGui::SliderFloat("Tunnel depth", &c.fx.tunnelDepth,       0.f,  0.5f,"%.3f");
        changed |= ImGui::SliderFloat("Tunnel curv",  &c.fx.tunnelCurvature,  -0.5f, 0.5f,"%.3f");
    )
    TOGGLED_SECTION("4. Blur & motion", c.fx.enableBlurMotion,
        changed |= ImGui::SliderFloat("Gaussian blur",     &c.fx.gaussianBlur,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("Directional blur",  &c.fx.directionalBlur,     0,1,"%.2f");
        changed |= ImGui::SliderFloat("Directional angle", &c.fx.directionalBlurAngle,0,360,"%.0f\xc2\xb0");
        changed |= ImGui::SliderFloat("Zoom blur",         &c.fx.zoomBlur,            0,1,"%.2f");
        changed |= ImGui::SliderFloat("Motion blur",       &c.fx.motionBlur,          0,1,"%.2f");
        changed |= ImGui::SliderFloat("Temporal blur",     &c.fx.temporalBlur,        0,1,"%.2f");
    )
    TOGGLED_SECTION("5. Sharpen / detalle", c.fx.enableSharpen,
        changed |= ImGui::SliderFloat("Unsharp mask",  &c.fx.unsharpMask,   0,1,"%.2f");
        changed |= ImGui::SliderFloat("CAS",           &c.fx.casAmount,     0,1,"%.2f");
        changed |= ImGui::SliderFloat("Local contrast",&c.fx.localContrast, 0,1,"%.2f");
    )
    TOGGLED_SECTION("6. Glitch / corruption", c.fx.enableGlitch,
        changed |= ImGui::SliderFloat("Datamosh",          &c.fx.glitchDatamosh,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("RGB split",         &c.fx.glitchRGBSplit,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("Scanline break",    &c.fx.glitchScanlineBreak,   0,1,"%.2f");
        changed |= ImGui::SliderFloat("Jitter",            &c.fx.glitchJitter,          0,1,"%.2f");
        changed |= ImGui::SliderFloat("Tearing",           &c.fx.glitchTearing,         0,1,"%.2f");
        changed |= ImGui::SliderFloat("Pixel sorting",     &c.fx.glitchPixelSort,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Buffer corruption", &c.fx.glitchBufferCorruption,0,1,"%.2f");
    )
    TOGGLED_SECTION("7. CRT / analog simulation", c.post.enableAnalog,
        changed |= ImGui::SliderFloat("Scanline focus",&c.post.analogScanlineFocus,       0.f,1.f,   "%.2f");
        changed |= ImGui::SliderFloat("Mask balance",  &c.post.analogMaskBalance,         0.f,1.f,   "%.2f");
        changed |= ImGui::SliderFloat("Analog noise",  &c.post.analogNoise,               0.f,1.f,   "%.2f");
        changed |= ImGui::SliderFloat("Analog bloom",  &c.post.analogBloom,               0.f,2.f,   "%.2f");
        changed |= ImGui::SliderFloat("VHS distortion",&c.post.vhsDistortion,             0.f,1.f,   "%.2f");
        changed |= ImGui::SliderFloat("Analog chroma", &c.post.analogChromaticAberration, 0.f,0.25f, "%.3f");
    )
    TOGGLED_SECTION("9. Temporal speed processing", c.temporal.enableTemporal,
        changed |= ImGui::SliderFloat("Frame interpolation",&c.playback.temporalInterpolation,0,1, "%.2f");
        changed |= ImGui::SliderFloat("Temporal blend",     &c.playback.temporalBlendStrength,0,1, "%.2f");
        changed |= ImGui::SliderFloat("Slow-motion",        &c.playback.slowMotionFactor, 0.1f,4,  "%.2fx");
        changed |= ImGui::SliderFloat("Frame accumulation", &c.playback.frameAccumulation,    0,1, "%.2f");
    )

    if (changed) controlsDirty = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: VJAY Extra
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawVJayExtraContent(VisualControls& c, bool& controlsDirty, std::mt19937& rng) {
    bool changed = false;
    auto setAll = [&](bool v) {
        c.fx.enablePixelate = c.fx.enableStrobe = c.fx.enableThreshold = c.fx.enableSlowZoom =
        c.fx.enableMirror   = c.fx.enableInvert = c.fx.enablePosterize = c.fx.enableInfrared =
        c.fx.enableZoomPulse= c.fx.enableRGBShift = c.system.enableFXAA = c.grid.enabled =
        c.fx.enableEdgeDetect = c.camera.enableMovement = v;
    };

    if (ImGui::Button("Randomize VJAY extra")) { randomizeVJayExtraControls(c, rng); changed = controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        c.fx.pixelateAmount = c.fx.strobeSpeed = c.fx.slowZoomAmount = 0;
        c.fx.thresholdLevel = 0.5f;
        c.fx.enablePixelate = c.fx.enableStrobe = c.fx.enableThreshold = c.fx.enableSlowZoom = false;
        c.system.enableFXAA = true;
        c.system.fxaaQualitySubpix           = 0.75f;
        c.system.fxaaQualityEdgeThreshold    = 0.125f;
        c.system.fxaaQualityEdgeThresholdMin = 0.0625f;
        changed = controlsDirty = true;
    }
    ImGui::SameLine(); if (ImGui::Button("Turn all ON"))  { setAll(true);  changed = controlsDirty = true; }
    ImGui::SameLine(); if (ImGui::Button("Turn all OFF")) { setAll(false); changed = controlsDirty = true; }
    ImGui::Separator();

// Macro local sencilla (undef al final del método)
#define EXT(label, tog, ...) \
    { changed |= ImGui::Checkbox(label, &(tog)); ImGui::BeginDisabled(!(tog)); __VA_ARGS__ ImGui::EndDisabled(); }

    EXT("Pixelate",  c.fx.enablePixelate,  changed |= ImGui::SliderFloat("Pixelate amount",&c.fx.pixelateAmount, 0.f,1.f,"%.2f");)
    EXT("Strobe",    c.fx.enableStrobe,    changed |= ImGui::SliderFloat("Strobe speed",   &c.fx.strobeSpeed,    0.f,20.f,"%.1f Hz");)
    EXT("Threshold", c.fx.enableThreshold, changed |= ImGui::SliderFloat("Threshold level",&c.fx.thresholdLevel, 0.f,1.f,"%.2f");)
    EXT("Slow zoom", c.fx.enableSlowZoom,  changed |= ImGui::SliderFloat("Slow zoom amount",&c.fx.slowZoomAmount,0.f,1.f,"%.2f");)
    EXT("Edge Detect", c.fx.enableEdgeDetect,
        changed |= ImGui::SliderFloat("Edge strength",  &c.fx.edgeStrength,  0.1f,5.f,"%.2f");
        changed |= ImGui::SliderFloat("Edge threshold", &c.fx.edgeThreshold, 0.f, 1.f,"%.2f");
        changed |= ImGui::SliderFloat("Edge blend",     &c.fx.edgeBlend,     0.f, 1.f,"%.2f");
        changed |= ImGui::ColorEdit3("Edge color",      glm::value_ptr(c.fx.edgeColor));)
    EXT("FXAA", c.system.enableFXAA,
        changed |= ImGui::SliderFloat("Quality Subpix",     &c.system.fxaaQualitySubpix,           0.f,1.f,"%.3f");
        changed |= ImGui::SliderFloat("Edge Threshold",     &c.system.fxaaQualityEdgeThreshold,    0.f,0.5f,"%.4f");
        changed |= ImGui::SliderFloat("Edge Threshold Min", &c.system.fxaaQualityEdgeThresholdMin, 0.f,0.2f,"%.4f");)
    EXT("Mirror",   c.fx.enableMirror,   changed |= ImGui::SliderFloat("Mirror amount",    &c.fx.mirrorAmount,    0.f,1.f,"%.2f");)
    EXT("Posterize",c.fx.enablePosterize,changed |= ImGui::SliderFloat("Posterize levels", &c.fx.posterizeLevels, 2.f,16.f,"%.0f");)
    EXT("Zoom pulse",c.fx.enableZoomPulse,changed |= ImGui::SliderFloat("Zoom pulse amount",&c.fx.zoomPulseAmount, 0.f,1.f,"%.2f");)
    EXT("RGB shift", c.fx.enableRGBShift, changed |= ImGui::SliderFloat("RGB shift amount", &c.fx.rgbShiftAmount,  0.f,0.1f,"%.3f");)

    ImGui::Separator();
    EXT("Grid overlay", c.grid.enabled,
        if (ImGui::Checkbox("##lock_grid_ext", &c.locks.lockGrid)) changed = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock grid during randomize");
        ImGui::SameLine(); ImGui::TextDisabled(c.locks.lockGrid ? "(locked)" : "(unlocked)");
        ImGui::SameLine(); ImGui::Text("Mode");
        changed |= ImGui::Combo("##grid_mode", &c.grid.mode, "Vertical\0Horizontal\0Matrix\0");
        if (c.grid.mode == 2) {
            changed |= ImGui::SliderInt("Rows",    &c.grid.rows,    1, 8);
            changed |= ImGui::SliderInt("Columns", &c.grid.columns, 1, 8);
        } else {
            changed |= ImGui::SliderInt("Grid count", &c.grid.count, 1, 8);
        }
        changed |= ImGui::Checkbox("Mirror cells",  &c.grid.mirrorCells);
        changed |= ImGui::Checkbox("Show grid lines",&c.grid.showLines);
        ImGui::BeginDisabled(!c.grid.showLines);
        changed |= ImGui::SliderFloat("Line width",     &c.grid.lineWidth,     0.0001f,0.05f,"%.4f");
        changed |= ImGui::SliderFloat("Line intensity", &c.grid.lineIntensity, 0.f,    1.f,  "%.2f");
        changed |= ImGui::ColorEdit3("Line color",      &c.grid.lineColor[0]);
        ImGui::EndDisabled();)

    ImGui::Separator();
    EXT("Camera movement", c.camera.enableMovement,
        changed |= ImGui::SliderFloat("Camera zoom",     &c.camera.zoom,     0.01f,5.f,       "%.2f");
        changed |= ImGui::SliderFloat("Camera pan X",    &c.camera.panX,     -1.f,1.f,       "%.2f");
        changed |= ImGui::SliderFloat("Camera pan Y",    &c.camera.panY,     -1.f,1.f,       "%.2f");
        changed |= ImGui::SliderFloat("Camera rotation", &c.camera.rotation, -3.14159f,3.14159f,"%.2f rad");)

    ImGui::Separator();
    EXT("RGB Overlay", c.color.enableRgbOverlay,
        changed |= ImGui::ColorEdit3("Overlay color", &c.color.rgbOverlay[0]);)

#undef EXT
    if (changed) controlsDirty = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: NLE Editor  (sin cambios lógicos, solo limpieza menor)
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawNLEEditorContent(
    const UICallbacks& callbacks,
    const std::string& video1Path,
    const std::string& video2Path,
    const std::string& video3Path)
{
    ImGui::Text("NLE Effects (for rendering/exporting only)");
    ImGui::TextDisabled("These effects are applied when rendering to a new file");
    ImGui::Separator();

    static int videoSource = static_cast<int>(g_project_state.nleVideoSource);
    ImGui::Text("Video Source:"); ImGui::SameLine();
    if (ImGui::RadioButton("Video 1", &videoSource, 0)) g_project_state.nleVideoSource = NLEVideoSource::VIDEO_1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Video 2", &videoSource, 1)) g_project_state.nleVideoSource = NLEVideoSource::VIDEO_2;
    ImGui::SameLine();
    if (ImGui::RadioButton("Video 3", &videoSource, 2)) g_project_state.nleVideoSource = NLEVideoSource::VIDEO_3;

    const std::string& path = (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) ? video1Path :
                              (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_2) ? video2Path : video3Path;
    g_project_state.active_file = path.empty() ? "(no video loaded)" : path;

    ImGui::Separator(); ImGui::Text("Quality Presets:");
    auto preset = [](int w, int h){ g_project_state.width = w; g_project_state.height = h; };
    if (ImGui::Button("Auto"))        { preset(0,0); g_project_state.fps = 0; }
    ImGui::SameLine(); if (ImGui::Button("240p"))      preset(426,240);
    ImGui::SameLine(); if (ImGui::Button("480p 4:3"))  preset(640,480);
    ImGui::SameLine(); if (ImGui::Button("720p"))      preset(1280,720);
    ImGui::SameLine(); if (ImGui::Button("1080p"))     preset(1920,1080);
    ImGui::SameLine(); if (ImGui::Button("1080p 4:3")) preset(1440,1080);

    ImGui::Separator();
    ImGui::SliderFloat("Speed",  &g_project_state.speed,  0.25f, 4.f,  "%.2fx");
    ImGui::SliderInt("FPS",      &g_project_state.fps,    0, 120);
    ImGui::SliderInt("Width",    &g_project_state.width,  0, 3840);
    ImGui::SliderInt("Height",   &g_project_state.height, 0, 2160);

    static char scaleBuf[64]  = "lanczos";
    static char outputBuf[256]= "output.mp4";
    // Sync buffers from state if they differ
    if (g_project_state.scale_flags  != scaleBuf)  strncpy(scaleBuf,  g_project_state.scale_flags.c_str(),  sizeof(scaleBuf)-1);
    if (g_project_state.output_file  != outputBuf) strncpy(outputBuf, g_project_state.output_file.c_str(), sizeof(outputBuf)-1);

    ImGui::InputText("Scale Flags", scaleBuf,  sizeof(scaleBuf));  g_project_state.scale_flags  = scaleBuf;
    ImGui::Checkbox("Enable Unsharp", &g_project_state.enable_unsharp);
    if (g_project_state.enable_unsharp) {
        ImGui::SliderFloat("Unsharp Amount", &g_project_state.unsharp_amount, 0.f, 2.f);
        ImGui::SliderFloat("Unsharp Radius", &g_project_state.unsharp_radius, 1.f, 10.f);
    }
    ImGui::Separator();
    ImGui::InputText("Output File", outputBuf, sizeof(outputBuf)); g_project_state.output_file = outputBuf;
    ImGui::Separator();

    bool canRender = (g_project_state.active_file != "(no video loaded)");
    ImGui::BeginDisabled(!canRender);
    if (ImGui::Button(canRender ? "Render/Export" : "Render/Export (No video loaded)")) {
        g_project_state.do_swap = true;
        g_project_state.increment_version();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Apply Changes") && callbacks.onApplyChanges) callbacks.onApplyChanges();

    ImGui::Separator();
    ImGui::Text("Current File: %s", g_project_state.active_file.c_str());
    ImGui::Text("Version: %lu",     g_project_state.get_version());
    ImGui::Text("Dirty: %s",        g_project_state.is_dirty() ? "Yes" : "No");
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Diagnostics
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawDiagnosticsContent(
    const UIDiagnostics& diag, VideoPlayer& player, VideoPlayer& player2, VideoPlayer& player3,
    VideoRegistry& registry, int& selAsset, int& /*selAsset2*/, int& /*selAsset3*/,
    VisualControls& c, const UICallbacks& callbacks)
{
    // ── System Overview ──
    if (ImGui::CollapsingHeader("System Overview", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "Render");
        ImGui::Text("  Swapchain:  %ux%u", diag.swapchainWidth, diag.swapchainHeight);
        ImGui::Text("  Frame:      %u (image %u)", diag.lastFrameFrameIndex, diag.lastFrameImageIndex);
        ImGui::Text("  Mode:       %d", diag.currentMode);
        ImGui::Text("  FPS:        %.1f", ImGui::GetIO().Framerate);
        ImGui::Unindent();
    }

    // ── Helper: ffprobe-style video info ──
    auto drawVideoProbe = [&c](const char* label, VideoPlayer& p, const UIDiagnostics& d, bool isVideo1) {
        if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) return;

        ImGui::Indent();

        if (!p.isReady()) {
            ImGui::TextColored({0.8f, 0.3f, 0.3f, 1.0f}, "  [OFFLINE]");
            ImGui::Unindent();
            return;
        }

        // File info
        ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "File");
        const auto& path = p.sourcePath();
        auto filename = path.substr(path.find_last_of('/') + 1);
        ImGui::Text("  Filename:    %s", filename.c_str());
        ImGui::Text("  Container:   %s", p.containerFormat());
        ImGui::Text("  Streams:     %d", p.numStreams());

        // Video stream info
        ImGui::Spacing();
        ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "Video Stream");
        ImGui::Text("  Codec:       %s", p.codecName());
        ImGui::Text("  Resolution:  %dx%d", p.width(), p.height());

        double fd = std::max(1e-6, p.frameDuration());
        double fps = 1.0 / fd;
        ImGui::Text("  FPS:         %.2f", fps);

        if (isVideo1) {
            double rate = effectivePlaybackRate(c, fps);
            double over = std::max(1.0, (double)c.playback.videoDecodeOversample);
            ImGui::Text("  Display FPS: %.2f", fps * rate);
            ImGui::Text("  Decode FPS:  %.2f", fps * std::max(rate, over));
            if (c.playback.forcedFpsIndex > 0)
                ImGui::Text("  Forced FPS:  %d", FORCED_FPS_OPTIONS_UI[c.playback.forcedFpsIndex]);
        }

        ImGui::Text("  Pixel fmt:   %s", p.pixelFormatName());

        int64_t br = p.bitrate();
        if (br > 0) {
            double mbps = br / 1000000.0;
            if (mbps >= 1.0)
                ImGui::Text("  Bitrate:     %.1f Mbps", mbps);
            else
                ImGui::Text("  Bitrate:     %lld kbps", br / 1000);
        } else {
            ImGui::Text("  Bitrate:     N/A");
        }

        // Duration
        double dur = p.durationSeconds();
        int mm = (int)(dur / 60.0);
        double ss = dur - mm * 60.0;
        ImGui::Text("  Duration:    %02d:%05.2f", mm, ss);

        // Playback position
        double pts = p.getCurrentFramePTS();
        int pm = (int)(pts / 60.0);
        double ps = pts - pm * 60.0;
        ImGui::Text("  Position:    %02d:%05.2f", pm, ps);

        // Playback rate
        ImGui::Text("  Play rate:   %.2fx", p.getPlaybackRate());

        ImGui::Unindent();
    };

    // ── Video Probes ──
    drawVideoProbe("Video 1 - Main Output", player, diag, true);
    drawVideoProbe("Video 2 - Overlay", player2, diag, false);
    drawVideoProbe("Video 3 - Extra", player3, diag, false);

    // ── Asset Manager ──
    if (ImGui::CollapsingHeader("Asset Manager")) {
        ImGui::Indent();
        const auto& assets = registry.getAssets();
        if (selAsset >= 0 && selAsset < (int)assets.size()) {
            const auto& m = assets[selAsset].metadata;
            ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "Selected Asset");
            ImGui::Text("  Filename:   %s", m.filename.c_str());
            ImGui::Text("  Resolution: %dx%d", m.width, m.height);
            ImGui::Text("  FPS:        %.2f", m.fps);

            int dm = (int)(m.duration / 60.0);
            double ds = m.duration - dm * 60.0;
            ImGui::Text("  Duration:   %02d:%05.2f", dm, ds);

            double mbps = m.bitrate / 1000000.0;
            if (mbps >= 1.0)
                ImGui::Text("  Bitrate:    %.1f Mbps", mbps);
            else
                ImGui::Text("  Bitrate:    %.0f kbps", m.bitrate / 1000.0);

            ImGui::Text("  Audio:      %s", m.hasAudio ? "Yes" : "No");
            ImGui::Text("  Pix fmt:    %s", av_get_pix_fmt_name(m.pixelFormat));

            ImGui::Spacing();
            static bool showRename = false;
            if (ImGui::Button("Rename")) showRename = !showRename;
            ImGui::SameLine();
            if (ImGui::Button("Delete") && callbacks.onDeleteAsset) callbacks.onDeleteAsset(selAsset);
            if (showRename) {
                static char renameBuf[256] = "";
                ImGui::InputText("New Filename", renameBuf, sizeof(renameBuf));
                ImGui::SameLine();
                if (ImGui::Button("Confirm") && renameBuf[0]) {
                    if (callbacks.onRenameAsset) callbacks.onRenameAsset(selAsset, renameBuf);
                    renameBuf[0] = '\0';
                    showRename   = false;
                }
            }
        } else {
            ImGui::TextDisabled("  No asset selected");
        }
        ImGui::Unindent();
    }

    // ── Animation Debug ──
    if (ImGui::CollapsingHeader("Animation Debug")) {
        ImGui::Indent();
        ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "Timing");
        ImGui::Text("  CPU elapsed:    %.3f s",    diag.animationElapsedSeconds);
        ImGui::Text("  UBO time:       %.3f",      diag.animationTime);
        ImGui::Text("  UBO delta:      %.6f",      diag.animationDelta);
        ImGui::Text("  Speed mult:     %.2fx",     diag.animationRelativeSpeed);
        ImGui::Text("  Seconds/unit:   %.3f",      diag.animationSecondsPerUnit);

        ImGui::Spacing();
        if (ImGui::SliderFloat("Target seconds", &c.playback.animationTargetSeconds, 0.1f, 5.f, "%.2fs")
            && callbacks.onControlsChanged) callbacks.onControlsChanged();
        float sug = 1.f / std::max(0.1f, c.playback.animationTargetSeconds);
        ImGui::Text("  Suggested speed: %.2fx", sug);
        if (ImGui::Button("Apply suggested speed")) c.playback.animationSpeed = sug;
        ImGui::SameLine();
        if (ImGui::Button("Reset time phase")) {
            c.playback.animationSpeed = c.playback.animationTargetSeconds = 1.f;
            if (callbacks.onControlsChanged) callbacks.onControlsChanged();
        }
        ImGui::Unindent();
    }

    // ── Color Palette ──
    if (ImGui::CollapsingHeader("Color Palette")) {
        ImGui::Indent();
        if (ImGui::Button("Reset Palette")) {
            c.color.primaryColor   = {0.9f, 0.4f, 0.1f, 1.f};
            c.color.secondaryColor = {0.1f, 0.5f, 0.8f, 1.f};
        }
        ImGui::Unindent();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Performance
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawPerformanceContent(const UIDiagnostics& diag)
{
    static const char* PASS_NAMES[] = {
        "Pass A (Base)",
        "Pass B (Spatial)",
        "Pass C (Detail)",
        "Pass D (Temporal)",
        "Pass E (Degradation)",
        "Pass F (Color)",
        "Pass G (Output)",
        "Swapchain Final"
    };

    ImGui::Text("GPU Timestamps (ms)");
    ImGui::Separator();

    for (int i = 0; i < 8; ++i) {
        float ms = diag.gpuPassTimes[i];
        if (ms > 0.0f) {
            ImGui::Text("  %s: %.3f ms", PASS_NAMES[i], ms);
        } else {
            ImGui::TextDisabled("  %s: --", PASS_NAMES[i]);
        }
    }

    ImGui::Separator();
    ImGui::Text("Total GPU: %.3f ms", diag.gpuTotalTime);

    ImGui::Separator();
    ImGui::Text("FPS");
    ImGui::Text("  App (main loop): %.1f", diag.appFps);
    ImGui::Text("  Preview window:  %.1f", diag.appFps);
    ImGui::Text("  Output window:   %.1f", diag.appFps);
    ImGui::Text("  ImGui render:    %.1f", ImGui::GetIO().Framerate);

    ImGui::Separator();
    ImGui::Text("CPU Breakdown (ms)");
    ImGui::Text("  Events:        %.3f", diag.cpuEventsMs);
    ImGui::Text("  UBO update:    %.3f", diag.cpuUboUpdateMs);
    ImGui::Text("  Video update:  %.3f", diag.cpuVideoUpdateMs);
    ImGui::Text("  UI render:     %.3f", diag.cpuUiRenderMs);
    ImGui::Text("  Record cmd:    %.3f", diag.cpuRecordCmdMs);
    ImGui::Text("  Submit/Present:%.3f", diag.cpuSubmitPresentMs);
    ImGui::Text("  Frame total:   %.3f", diag.cpuFrameTotalMs);

    ImGui::Separator();
    if (ImGui::Checkbox("Pause UI Renderer", &rendererPaused)) {
        // toggled
    }
    if (rendererPaused) {
        ImGui::SameLine();
        ImGui::TextDisabled("(press P to resume)");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Helpers compartidos: MIDI learn block  (evita duplicar en ambas funciones)
// ═════════════════════════════════════════════════════════════════════════════

static void drawMidiLearnBlock(MidiSystem& midi, const char* comboSuffix) {
    if (!midi.hasLearnedMessage()) return;
    MidiMessage msg = midi.getLastLearnedMessage();
    ImGui::Separator();
    ImGui::TextColored({0,1,0,1}, "Detected MIDI Message:");

    if (msg.type == MidiEventType::CONTROL_CHANGE) {
        ImGui::Text("Type: Control Change | CC %d | Value %d", msg.controller, msg.value);
        ImGui::Separator(); ImGui::Text("Assign to Parameter:");

        static int selParam = 0;
        selParam = std::clamp(selParam, 0, PARAMETER_COUNT - 1);
        const auto& ds = paramDisplayStrings();
        ImGui::Combo("Parameter", &selParam, [](void* d, int i){ return ((std::vector<std::string>*)d)->at(i).c_str(); },
                     (void*)&ds, (int)ds.size());

        static bool inv = false;
        ImGui::Checkbox("Invert", &inv);
        ImGui::Text("Range: %.2f – %.2f", PARAMETER_INFOS[selParam].minVal, PARAMETER_INFOS[selParam].maxVal);

        if (ImGui::Button("Assign Mapping")) {
            midi.addMapping(msg.controller, PARAMETER_INFOS[selParam].name,
                            PARAMETER_INFOS[selParam].minVal, PARAMETER_INFOS[selParam].maxVal, inv);
            midi.clearLearnedMessage();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) midi.clearLearnedMessage();

    } else if (msg.type == MidiEventType::NOTE_ON) {
        ImGui::Text("Type: Note On | Note %d | Vel %d", msg.note, msg.velocity);
        ImGui::Separator(); ImGui::Text("Assign to Trigger Action:");

        static int selAction = 0;
        char lbl[32]; snprintf(lbl, sizeof(lbl), "Action##MidiLearn%s", comboSuffix);
        ImGui::Combo(lbl, &selAction, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);

        if (ImGui::Button("Assign Trigger")) {
            midi.addTriggerMapping(msg.note, TRIGGER_ACTIONS[selAction]);
            midi.clearLearnedMessage();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) midi.clearLearnedMessage();
    } else {
        ImGui::Text("MIDI type not supported for learn mode.");
        if (ImGui::Button("Cancel")) midi.clearLearnedMessage();
    }
    drawTriggerAndRgbReferenceSection();
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: MIDI
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawMidiControlsContent(MidiSystem& midi) {
    bool en = midi.isEnabled();
    if (ImGui::Checkbox("Enable MIDI", &en)) midi.setEnabled(en);
    ImGui::Separator();

    unsigned portCount = midi.getPortCount();
    ImGui::Text("MIDI Ports: %u", portCount);
    if (portCount > 0) {
        auto ports = midi.getAvailablePorts();
        std::vector<const char*> names;
        for (auto& p : ports) names.push_back(p.c_str());
        static int sel = 0;
        if (ImGui::Combo("Port", &sel, names.data(), (int)portCount)) {
            midi.closePort();
            midi.openPort(sel);
        }
    } else { ImGui::Text("No MIDI devices detected"); }
    ImGui::Separator();

    ImGui::Text("MIDI Learn Wizard");
    bool lm = midi.isLearnMode();
    if (ImGui::Checkbox("Learn Mode", &lm)) midi.setLearnMode(lm);
    if (lm) ImGui::TextColored({1,1,0,1}, "Move a knob or press a key on your MIDI device...");

    drawMidiLearnBlock(midi, "Tab");
    ImGui::Separator();

    // Mappings table
    ImGui::Text("Current Mappings:");
    const auto& mappings = midi.getMappings();
    if (ImGui::BeginTable("MidiMappings", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("CC"); ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Min"); ImGui::TableSetupColumn("Max");
        ImGui::TableSetupColumn("Action"); ImGui::TableHeadersRow();
        std::vector<std::pair<int,MidiMapping>> copy(mappings.begin(), mappings.end());
        for (auto& [cc, m] : copy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", cc);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", m.parameterName.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", m.minValue);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", m.maxValue);
            ImGui::TableSetColumnIndex(4);
            std::string lbl = "Remove##" + std::to_string(cc);
            if (ImGui::Button(lbl.c_str())) midi.removeMapping(cc);
        }
        ImGui::EndTable();
    }
    ImGui::Separator();

    // Triggers table
    ImGui::Text("MIDI Trigger Buttons:");
    const auto& triggers = midi.getTriggerMappings();
    if (ImGui::BeginTable("MidiTriggers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Note"); ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove"); ImGui::TableHeadersRow();
        std::vector<std::pair<int,MidiTriggerMapping>> copy(triggers.begin(), triggers.end());
        for (auto& [note, m] : copy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", note);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", m.actionName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string lbl = "Remove##MidiTrig" + std::to_string(note);
            if (ImGui::Button(lbl.c_str())) midi.removeTriggerMapping(note);
        }
        ImGui::EndTable();
    } else if (triggers.empty()) { ImGui::TextDisabled("No MIDI triggers assigned yet"); }

    ImGui::Separator(); ImGui::Text("Add MIDI Trigger:");
    static int noteIn = 60;
    static int actionIdx = 0;
    ImGui::SliderInt("Note##Add", &noteIn, 0, 127);
    ImGui::Combo("Action##Add", &actionIdx, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);
    if (ImGui::Button("Add Trigger"))
        midi.addTriggerMapping(noteIn, TRIGGER_ACTIONS[actionIdx]);

    ImGui::Separator();
    ImGui::Text("Default CC Mappings: CC1=animationSpeed CC2=tempo CC3=energy CC4=bass");
    ImGui::Text("CC5=mid CC6=high CC7=colorBlend CC8=bloomIntensity CC12=feedbackAmount");
    ImGui::Text("Note Triggers: 36-48=mode switch 60=random video 62=bloom 64=glitch 67=feedback");
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: OSC
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawOscControlsContent(OscSystem& osc) {
    bool en = osc.isEnabled();
    if (ImGui::Checkbox("Enable OSC", &en)) osc.setEnabled(en);
    ImGui::Separator();

    std::string ip = OscSystem::getLocalIPAddress();
    ImGui::Text("OSC Port: %d", osc.getPort());
    ImGui::TextColored({0,1,0.5f,1}, "IP: %s", ip.c_str());
    ImGui::Text("Host: %s  Port: %d  Protocol: UDP", ip.c_str(), osc.getPort());
    ImGui::Separator();

    ImGui::Text("OSC Learn Wizard");
    bool lm = osc.isLearnMode();
    if (ImGui::Checkbox("Learn Mode", &lm)) osc.setLearnMode(lm);
    if (lm) ImGui::TextColored({1,1,0,1}, "Send an OSC message to learn its address...");

    if (osc.hasLearnedMessage()) {
        OscMessage msg = osc.getLastLearnedMessage();
        ImGui::Separator();
        ImGui::TextColored({0,1,0,1}, "Detected: %s", msg.address.c_str());
        if      (msg.type == OscMessageType::FLOAT)  ImGui::Text("Float: %.4f", msg.floatValue);
        else if (msg.type == OscMessageType::INT)    ImGui::Text("Int: %d",     msg.intValue);
        else if (msg.type == OscMessageType::STRING) ImGui::Text("String: %s",  msg.stringValue.c_str());
        ImGui::Separator(); ImGui::Text("Assign to Parameter:");

        static int selParam = 0;
        static int prevParam = -1;
        static float minV = 0.f, maxV = 1.f;
        selParam = std::clamp(selParam, 0, PARAMETER_COUNT - 1);
        const auto& ptrs = paramDisplayPtrs();
        if (ImGui::Combo("Parameter", &selParam, ptrs.data(), (int)ptrs.size()) || prevParam != selParam) {
            minV = PARAMETER_INFOS[selParam].minVal;
            maxV = PARAMETER_INFOS[selParam].maxVal;
            prevParam = selParam;
        }
        ImGui::DragFloat("Min", &minV, 0.01f);
        ImGui::DragFloat("Max", &maxV, 0.01f);
        static bool inv = false;
        ImGui::Checkbox("Invert", &inv);

        if (ImGui::Button("Assign Mapping")) {
            osc.addMapping(msg.address, PARAMETER_INFOS[selParam].name, minV, maxV, inv);
            osc.clearLearnedMessage();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) osc.clearLearnedMessage();
    }
    ImGui::Separator();

    // Mappings table
    ImGui::Text("Current Mappings:");
    if (ImGui::BeginTable("OscMappings", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Min"); ImGui::TableSetupColumn("Max"); ImGui::TableHeadersRow();
        for (auto& [addr, m] : osc.getMappings()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", addr.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", m.parameterName.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", m.minValue);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", m.maxValue);
        }
        ImGui::EndTable();
    }
    ImGui::Separator();

    // Triggers
    ImGui::Text("OSC Triggers:");
    const auto& trigs = osc.getTriggerMappings();
    if (ImGui::BeginTable("OscTriggers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove"); ImGui::TableHeadersRow();
        std::vector<std::pair<std::string,OscTriggerMapping>> copy(trigs.begin(), trigs.end());
        for (auto& [addr, m] : copy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", addr.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", m.actionName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string lbl = "Remove##" + addr;
            if (ImGui::Button(lbl.c_str())) osc.removeTriggerMapping(addr);
        }
        ImGui::EndTable();
    }
    ImGui::Separator(); ImGui::Text("Add Trigger Mapping:");
    static char trigAddr[256] = "/vjay/";
    static int selAction = 0;
    ImGui::InputText("OSC Address", trigAddr, sizeof(trigAddr));
    ImGui::Combo("Action", &selAction, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);
    if (ImGui::Button("Add Trigger"))
        osc.addTriggerMapping(trigAddr, TRIGGER_ACTIONS[selAction]);

    ImGui::Separator();
    ImGui::Text("Examples: /vjay/animationSpeed  /vjay/bloomIntensity");
    ImGui::Text("/vjay/randomize (trigger, no args)  /vjay/rgbOverlay R G B");
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Audio
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawAudioDebugContent(AudioSystem& audio, VisualControls& c) {
    ImGui::TextColored({0,1,0.5f,1}, "Audio Reactive Debug"); ImGui::Separator();

    // Manual inputs
    ImGui::Text("Audio-inspired inputs");
    if (c.system.enableAudioReactive) {
        ImGui::BeginDisabled(true);
        ImGui::SliderFloat("Tempo (auto)", &c.playback.tempo, 0.0f, 5.0f, "%.2fx");
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::TextDisabled("(driven by energy)");
    } else {
        ImGui::SliderFloat("Tempo",  &c.playback.tempo, 0.0f, 5.0f, "%.2fx");
    }
    ImGui::Checkbox("Auto tempo LFO", &c.playback.enableTempoLfo);
    if (c.playback.enableTempoLfo) {
        ImGui::SliderFloat("LFO speed (Hz)", &c.playback.tempoLfoSpeed, 0.05f, 4.f, "%.2f Hz");
        ImGui::SliderFloat("LFO depth",      &c.playback.tempoLfoDepth, 0.f, 2.f);
    }
    ImGui::SliderFloat("Energy", &c.audio.energy,        0.f,1.f);
    ImGui::SliderFloat("Bass",   &c.audio.bass,          0.f,1.f);
    ImGui::SliderFloat("Mid",    &c.audio.mid,           0.f,1.f);
    ImGui::SliderFloat("High",   &c.audio.high,          0.f,1.f);
    ImGui::Text("EQ / Gain");
    ImGui::SliderFloat("Input volume",  &c.audio.inputGain, 0.0f, 3.0f, "%.2fx");
    ImGui::SliderFloat("Bass gain",     &c.audio.bassGain,  0.0f, 4.0f, "%.2fx");
    ImGui::SliderFloat("Mid gain",      &c.audio.midGain,   0.0f, 4.0f, "%.2fx");
    ImGui::SliderFloat("High gain",     &c.audio.highGain,  0.0f, 4.0f, "%.2fx");
    ImGui::SliderFloat("Procedural audio drive", &c.audio.reactiveDrive, 0.5f,3.f,"%.2fx");
    ImGui::Separator();
    ImGui::TextColored({0,1,0.5f,1}, "Audio Reactivity (always ON)");
    ImGui::SliderFloat("Warp response",    &c.audio.warpResponse,    0,2,"%.2f");
    ImGui::SliderFloat("Feedback response",&c.audio.feedbackResponse,0,2,"%.2f");
    ImGui::SliderFloat("Blur response",    &c.audio.blurResponse,    0,2,"%.2f");
    ImGui::SliderFloat("Color response",   &c.audio.colorResponse,   0,2,"%.2f");
    ImGui::SliderFloat("Glitch response",  &c.audio.glitchResponse,  0,2,"%.2f");
    ImGui::SliderFloat("Beat sync",        &c.audio.beatSync,        0,4,"%.2f");
    ImGui::SliderFloat("LFO rate",         &c.audio.lfoRate,     0.05f,4,"%.2f Hz");
    ImGui::Separator();

    // Live read-only meters
    ImGui::Text("Monitoreo en vivo (solo lectura)");
    float e=c.runtime.audioReactive.energy, b=c.runtime.audioReactive.bass,
          m=c.runtime.audioReactive.mid,    h=c.runtime.audioReactive.high;
    ImGui::BeginDisabled(true);
    ImGui::SliderFloat("Energy meter",&e,0,1,"%.2f"); ImGui::SliderFloat("Bass meter",&b,0,1,"%.2f");
    ImGui::SliderFloat("Mid meter",   &m,0,1,"%.2f"); ImGui::SliderFloat("High meter",&h,0,1,"%.2f");
    ImGui::EndDisabled();
    ImGui::Separator();

    // Device selector
    ImGui::Text("Input Device:");
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        audio.refreshPulseAudioSources();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart Stream")) {
        audio.restartStream();
    }
    auto devNames = audio.getInputDeviceNames();
    std::vector<int>          validIdx;
    std::vector<const char*>  validNames;
    for (int i = 0; i < (int)devNames.size(); ++i)
        if (!devNames[i].empty()) { validIdx.push_back(i); validNames.push_back(devNames[i].c_str()); }
    int cur = 0;
    for (int i = 0; i < (int)validIdx.size(); ++i)
        if (validIdx[i] == audio.getInputDeviceIndex()) { cur = i; break; }
    if (ImGui::Combo("##Device", &cur, validNames.data(), (int)validNames.size()))
        audio.setInputDevice(validIdx[cur]);

    ImGui::Separator();
    ImGui::Text("Audio Stream: %s", audio.isRunning() ? "Running" : "Stopped");
    float rms = audio.getRMS();
    ImGui::Text("RMS Level: %.4f", rms); ImGui::ProgressBar(rms, {200,0});
    ImGui::Separator();

    ImGui::Text("Raw Band Levels:");
    ImGui::Text("SubBass %.4f | Kick %.4f | Bass %.4f | Mid %.4f | High %.4f",
                audio.getSubBass(), audio.getKick(), audio.getBass(), audio.getMid(), audio.getHigh());
    ImGui::Text("Smoothed:");
    ImGui::Text("SubBass %.4f | Kick %.4f | Bass %.4f | Mid %.4f | High %.4f",
                audio.getSmoothedSubBass(), audio.getSmoothedKick(),
                audio.getSmoothedBass(),   audio.getSmoothedMid(), audio.getSmoothedHigh());
    ImGui::TextColored({0,1,0.5f,1}, "Post-EQ (applied to visuals):");
    ImGui::Text("Bass %.4f | Mid %.4f | High %.4f",
                std::min(audio.getSmoothedBass()   * c.audio.bassGain, 1.0f),
                std::min(audio.getSmoothedMid()    * c.audio.midGain,  1.0f),
                std::min(audio.getSmoothedHigh()   * c.audio.highGain, 1.0f));
    ImGui::Separator();

    // FFT Spectrum with dB Y-axis, log-spaced bars, and EQ gains applied
    ImGui::Text("FFT Spectrum (20 Hz - 20 kHz) — dB scale (with EQ)");
    const auto& fft = audio.getFFTMagnitudes();
    static std::vector<float> spectrumBars;
    spectrumBars.resize(64);

    // Helper: gain for a given frequency based on EQ sliders
    auto freqGain = [&](float hz) -> float {
        float bassFreq = 500.0f;
        float midFreq  = 2000.0f;
        if (hz < bassFreq) {
            return c.audio.bassGain;
        } else if (hz < midFreq) {
            float t = (hz - bassFreq) / (midFreq - bassFreq);
            return c.audio.bassGain * (1.0f - t) + c.audio.midGain * t;
        } else {
            float t = std::min(1.0f, (hz - midFreq) / 4000.0f);
            return c.audio.midGain * (1.0f - t) + c.audio.highGain * t;
        }
    };

    for (int b = 0; b < 64; ++b) {
        float f0 = 20.0f * std::pow(20000.0f / 20.0f, (float)b / 64.0f);
        float f1 = 20.0f * std::pow(20000.0f / 20.0f, (float)(b+1) / 64.0f);
        float centerFreq = std::sqrt(f0 * f1); // geometric center for log scale
        float binW = 48000.0f / 2048.0f;
        int startBin = std::max(1, (int)(f0 / binW));
        int endBin   = std::min((int)fft.size() - 1, (int)(f1 / binW));
        if (endBin <= startBin) endBin = startBin + 1;
        float peak = 0.0f;
        for (int i = startBin; i <= endBin; ++i) {
            peak = std::max(peak, fft[i]);
        }
        spectrumBars[b] = peak * freqGain(centerFreq) * c.audio.inputGain;
    }

    // Fixed dB scale: -60 dB to 0 dB
    float dbMin = -60.0f;
    float dbMax = 0.0f;
    float dbRange = dbMax - dbMin;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;
    float labelW = 50.0f;
    float plotW = availW - labelW;
    float barW = plotW / 64.0f;
    float plotH = 200.0f;

    // Draw dB grid lines (horizontal reference lines)
    for (int db = (int)dbMin; db <= (int)dbMax; db += 10) {
        float norm = (float)(db - dbMin) / dbRange;
        float y = p.y + plotH - norm * plotH;
        dl->AddLine({p.x + labelW, y}, {p.x + labelW + plotW, y}, IM_COL32(80, 80, 80, 80));
        char dbLabel[16];
        snprintf(dbLabel, sizeof(dbLabel), "%d dB", db);
        dl->AddText({p.x, y - 6}, IM_COL32(180, 180, 180, 255), dbLabel);
    }

    // Draw bars in dB
    for (int i = 0; i < 64; ++i) {
        float mag = spectrumBars[i];
        float db = 20.0f * std::log10(mag + 1e-9f);
        float clampedDb = std::clamp(db, dbMin, dbMax);
        float norm = (clampedDb - dbMin) / dbRange;
        float h = norm * plotH;
        float hue = 0.33f + 0.5f * ((float)i / 64.0f);
        ImU32 col = ImColor::HSV(hue, 0.8f, 1.0f);
        dl->AddRectFilled({p.x + labelW + i*barW, p.y + plotH - h}, {p.x + labelW + (i+1)*barW - 1, p.y + plotH}, col);
    }

    // X-axis frequency labels
    ImGui::Dummy({availW, plotH + 5});
    ImGui::SetCursorScreenPos({p.x + labelW, p.y + plotH + 5});
    ImGui::Text("20Hz        100Hz        500Hz        2kHz        20kHz");
    ImGui::Separator();

    // Reactive state
    const auto& react = c.runtime.audioReactive;
    ImGui::Text("Realtime modulation: %s", react.enabled ? "ENABLED" : "disabled");
    if (react.enabled) {
        ImGui::Text("Energy %.3f | Bass %.3f | Mid %.3f | High %.3f",
                    react.energy, react.bass, react.mid, react.high);
        ImGui::Text("uvWarp %.3f | ripple %.3f | swirl %.3f | displacement %.3f | bend %.3f",
                    react.uvWarpStrength, react.rippleStrength, react.swirlStrength,
                    react.displacementAmount, react.bendAmount);
        ImGui::Text("feedback %.3f | trails %.3f | jitter %.3f | rgbSplit %.3f | grain %.3f",
                    react.feedbackAmount, react.trailStrength,
                    react.glitchJitter, react.glitchRGBSplit, react.grainStrength);
        ImGui::Text("zoomPulse %.3f | slowZoom %.3f | strobe %.3f | rgbShift %.3f",
                    react.zoomPulseAmount, react.slowZoomAmount,
                    react.strobeSpeed, react.rgbShiftAmount);
        ImGui::Text("cam zoom %.3f | panX %.3f | panY %.3f | rot %.3f",
                    react.cameraZoom, react.cameraPanX, react.cameraPanY, react.cameraRotation);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Tab: Parameters
// ═════════════════════════════════════════════════════════════════════════════

void UISystem::drawParameterIndexContent() {
    ImGui::TextColored({1,1,0.5f,1}, "Complete Parameter Reference");
    ImGui::Separator();
    ImGui::Text("Use these parameter names for MIDI CC and OSC mapping");

    auto list = [](std::initializer_list<const char*> items) {
        for (const char* s : items) ImGui::BulletText("%s", s);
    };

    if (!ImGui::BeginTabBar("ParamTabs")) return;

    if (ImGui::BeginTabItem("Core & Color")) {
        list({"animationSpeed","tempo","energy","bass","mid","high",
              "colorBlend","gradeBrightness","gradeContrast","gradeSaturation",
              "gradeHueShift","gradeGamma","colorLUTIndex","splitToneBalance",
              "primaryColor","secondaryColor","rgbOverlay","enableRgbOverlay",
              "autoRandomizeColors","colorRandomizeInterval"});
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Video & Mix")) {
        list({"videoMix","videoPlaybackRate","videoDecodeOversample",
              "loopBlendSeconds","forcedFpsIndex","enableDualVideo",
              "video2Mix","video2BlendMode","video2PlaybackRate",
              "video3Mix","video3BlendMode","video3PlaybackRate",
              "blendModeProcedural","blendModeVideo","blendModeFeedback",
              "blendProceduralMix","blendVideoMix","blendFeedbackMix"});
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Spatial & Camera")) {
        list({"uvWarpStrength","rippleStrength","rippleFrequency","swirlStrength",
              "displacementAmount","bendAmount","kaleidoSegments","tunnelDepth","tunnelCurvature",
              "cameraZoom","cameraPanX","cameraPanY","cameraRotation",
              "mirrorAmount","posterizeLevels","zoomPulseAmount","rgbShiftAmount"});
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Post FX & Blur")) {
        list({"bloomIntensity","bloomThreshold","aberrationAmount","grainStrength",
              "crtCurvature","crtScanlineIntensity","crtMaskIntensity","crtVignette","crtFishEye",
              "gaussianBlur","directionalBlur","directionalBlurAngle",
              "zoomBlur","motionBlur","temporalBlur",
              "unsharpMask","casAmount","localContrast",
              "pixelateAmount","strobeSpeed","thresholdLevel","slowZoomAmount",
              "edgeStrength","edgeThreshold","edgeBlend",
              "fxaaQualitySubpix","fxaaQualityEdgeThreshold","fxaaQualityEdgeThresholdMin"});
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Glitch & Temporal")) {
        list({"feedbackAmount","trailStrength","temporalAccumulation","feedbackDecay","recursiveBlend",
              "temporalInterpolation","temporalBlendStrength","slowMotionFactor","frameAccumulation",
              "glitchDatamosh","glitchRGBSplit","glitchScanlineBreak","glitchJitter",
              "glitchTearing","glitchPixelSort","glitchBufferCorruption",
              "analogScanlineFocus","analogMaskBalance","analogNoise","analogBloom",
              "vhsDistortion","analogChromaticAberration"});
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio")) {
        list({"audioWarpResponse","audioFeedbackResponse","audioBlurResponse",
              "audioColorResponse","audioGlitchResponse","audioBeatSync","audioLfoRate"});
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Triggers")) {
        ImGui::TextColored({0,1,0.5f,1}, "OSC (send without arguments)");
        for (int i = 0; i < TRIGGER_ACTION_COUNT; ++i) ImGui::BulletText("%s", TRIGGER_ACTIONS[i]);
        ImGui::Separator();
        ImGui::TextColored({0,1,0.5f,1}, "MIDI Notes");
        ImGui::Text("36-48 mode switch | 60 random video | 62 bloom | 64 glitch | 65 bend | 67 feedback");
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}