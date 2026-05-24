#include "UISystem.h"
#include "OscSystem.h"
#include "AudioSystem.h"

// ImGui
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

// GLM para value_ptr
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// Tus tipos existentes
#include "app/VisualControls.h"      // struct VisualControls
#include "app/MidiSystem.h"          // class MidiSystem
#include "video/VideoPlayer.h"         // class VideoPlayer
#include "video/VideoRegistry.h"       // class VideoRegistry
#include "app/ProjectState.h"        // g_project_state
#include "render/RenderJob.h"           // RenderWorker::Status

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

static const char* BLEND_ITEMS = "Add\0Screen\0Multiply\0Overlay\0Difference\0Soft Light\0";

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

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <random>

namespace fs = std::filesystem;

// Constantes copiadas del main (muevelas a un Config.h cuando quieras)
static const std::array<int, 5> FORCED_FPS_OPTIONS_UI = {0, 15, 24, 30, 60};

namespace {

constexpr int kLayerModeValues[] = {0, 1, 40};
constexpr int kLayerModeCount = sizeof(kLayerModeValues) / sizeof(kLayerModeValues[0]);
constexpr char kLayerComboItems[] = "Layer 0\0Layer 1\0Anaglyph 3D\0";

int layerIndexFromMode(int mode) {
    for (int i = 0; i < kLayerModeCount; ++i) {
        if (kLayerModeValues[i] == mode) {
            return i;
        }
    }
    return 0;
}

bool drawActiveLayerCombo(const char* label, int& activeMode) {
    int layerIndex = layerIndexFromMode(activeMode);
    bool selectionChanged = ImGui::Combo(label, &layerIndex, kLayerComboItems);
    if (selectionChanged) {
        activeMode = kLayerModeValues[layerIndex];
    }
    return selectionChanged;
}

float randFloat(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

struct ParameterInfo {
    const char* category;
    const char* name;
    float minVal;
    float maxVal;
};

static const ParameterInfo PARAMETER_INFOS[] = {
    // Procedural Controls
    {"Procedural", "animationSpeed", 0.0f, 3.0f},
    {"Procedural", "tempo", 0.0f, 2.0f},
    {"Procedural", "energy", 0.0f, 1.0f},
    {"Procedural", "bass", 0.0f, 1.0f},
    {"Procedural", "mid", 0.0f, 1.0f},
    {"Procedural", "high", 0.0f, 1.0f},
    {"Procedural", "colorBlend", 0.0f, 1.0f},
    {"Procedural", "uvWarpStrength", 0.0f, 1.0f},
    {"Procedural", "rippleStrength", 0.0f, 1.0f},
    {"Procedural", "rippleFrequency", 0.0f, 5.0f},
    {"Procedural", "swirlStrength", 0.0f, 1.0f},
    {"Procedural", "displacementAmount", 0.0f, 1.0f},
    {"Procedural", "kaleidoSegments", 1.0f, 12.0f},
    {"Procedural", "tunnelDepth", 0.0f, 1.0f},
    {"Procedural", "tunnelCurvature", 0.0f, 1.0f},
    // Post FX
    {"Post FX", "bloomIntensity", 0.0f, 1.0f},
    {"Post FX", "bloomThreshold", 0.0f, 1.0f},
    {"Post FX", "aberrationAmount", 0.0f, 0.1f},
    {"Post FX", "grainStrength", 0.0f, 1.0f},
    {"Post FX", "crtCurvature", 0.0f, 0.5f},
    {"Post FX", "crtScanlineIntensity", 0.0f, 1.0f},
    {"Post FX", "crtMaskIntensity", 0.0f, 1.0f},
    {"Post FX", "crtVignette", 0.0f, 1.0f},
    {"Post FX", "crtFishEye", 0.0f, 0.5f},
    {"Post FX", "gaussianBlur", 0.0f, 1.0f},
    {"Post FX", "directionalBlur", 0.0f, 1.0f},
    {"Post FX", "directionalBlurAngle", 0.0f, 360.0f},
    {"Post FX", "zoomBlur", 0.0f, 1.0f},
    {"Post FX", "motionBlur", 0.0f, 1.0f},
    {"Post FX", "temporalBlur", 0.0f, 1.0f},
    {"Post FX", "unsharpMask", 0.0f, 1.0f},
    {"Post FX", "casAmount", 0.0f, 1.0f},
    {"Post FX", "localContrast", 0.0f, 1.0f},
    {"Post FX", "pixelateAmount", 0.0f, 1.0f},
    {"Post FX", "strobeSpeed", 0.0f, 10.0f},
    {"Post FX", "thresholdLevel", 0.0f, 1.0f},
    {"Post FX", "enableThreshold", 0.0f, 1.0f},
    {"Post FX", "slowZoomAmount", 0.0f, 1.0f},
    {"Post FX", "edgeStrength", 0.0f, 2.0f},
    {"Post FX", "edgeThreshold", 0.0f, 1.0f},
    {"Post FX", "edgeBlend", 0.0f, 1.0f},
    {"Post FX", "fxaaQualitySubpix", 0.0f, 1.0f},
    {"Post FX", "fxaaQualityEdgeThreshold", 0.0f, 0.5f},
    {"Post FX", "fxaaQualityEdgeThresholdMin", 0.0f, 0.2f},
    // VJay Basics
    {"VJay Basics", "videoPlaybackRate", 0.1f, 3.0f},
    {"VJay Basics", "videoDecodeOversample", 1.0f, 4.0f},
    {"VJay Basics", "videoMix", 0.0f, 1.0f},
    {"VJay Basics", "video2Mix", 0.0f, 1.0f},
    {"VJay Basics", "video2PlaybackRate", 0.1f, 5.0f},
    {"VJay Basics", "grayscaleAmount", 0.0f, 1.0f},
    {"VJay Basics", "sharpenAmount", 0.0f, 1.0f},
    {"VJay Basics", "gradeBrightness", -1.0f, 1.0f},
    {"VJay Basics", "gradeContrast", 0.0f, 2.0f},
    {"VJay Basics", "gradeSaturation", 0.0f, 2.0f},
    {"VJay Basics", "gradeHueShift", 0.0f, 360.0f},
    {"VJay Basics", "gradeGamma", 0.1f, 3.0f},
    {"VJay Basics", "colorLUTIndex", 0.0f, 10.0f},
    {"VJay Basics", "splitToneBalance", 0.0f, 1.0f},
    {"VJay Basics", "blendProceduralMix", 0.0f, 1.0f},
    {"VJay Basics", "blendVideoMix", 0.0f, 1.0f},
    {"VJay Basics", "blendFeedbackMix", 0.0f, 1.0f},
    {"VJay Basics", "video2BlendMode", 0.0f, 4.0f},
    {"VJay Basics", "enableRgbOverlay", 0.0f, 1.0f},
    {"VJay Basics", "rgbOverlay", 0.0f, 2.0f},
    {"VJay Basics", "rgbOverlayR", 0.0f, 2.0f},
    {"VJay Basics", "rgbOverlayG", 0.0f, 2.0f},
    {"VJay Basics", "rgbOverlayB", 0.0f, 2.0f},
    // VJay Extra
    {"VJay Extra", "feedbackAmount", 0.0f, 1.0f},
    {"VJay Extra", "trailStrength", 0.0f, 1.0f},
    {"VJay Extra", "temporalAccumulation", 0.0f, 1.0f},
    {"VJay Extra", "feedbackDecay", 0.0f, 1.0f},
    {"VJay Extra", "recursiveBlend", 0.0f, 1.0f},
    {"VJay Extra", "glitchAmount", 0.0f, 1.0f},
    {"VJay Extra", "glitchDatamosh", 0.0f, 1.0f},
    {"VJay Extra", "glitchRGBSplit", 0.0f, 1.0f},
    {"VJay Extra", "glitchScanlineBreak", 0.0f, 1.0f},
    {"VJay Extra", "glitchJitter", 0.0f, 1.0f},
    {"VJay Extra", "glitchTearing", 0.0f, 1.0f},
    {"VJay Extra", "glitchPixelSort", 0.0f, 1.0f},
    {"VJay Extra", "glitchBufferCorruption", 0.0f, 1.0f},
    {"VJay Extra", "analogScanlineFocus", 0.0f, 1.0f},
    {"VJay Extra", "analogMaskBalance", 0.0f, 1.0f},
    {"VJay Extra", "analogNoise", 0.0f, 1.0f},
    {"VJay Extra", "analogBloom", 0.0f, 1.0f},
    {"VJay Extra", "vhsDistortion", 0.0f, 1.0f},
    {"VJay Extra", "analogChromaticAberration", 0.0f, 0.1f},
    {"VJay Extra", "mirrorAmount", 0.0f, 1.0f},
    {"VJay Extra", "posterizeLevels", 2.0f, 16.0f},
    {"VJay Extra", "zoomPulseAmount", 0.0f, 1.0f},
    {"VJay Extra", "rgbShiftAmount", 0.0f, 0.1f},
    {"VJay Extra", "audioWarpResponse", 0.0f, 1.0f},
    {"VJay Extra", "audioFeedbackResponse", 0.0f, 1.0f},
    {"VJay Extra", "audioBlurResponse", 0.0f, 1.0f},
    {"VJay Extra", "audioColorResponse", 0.0f, 1.0f},
    {"VJay Extra", "audioGlitchResponse", 0.0f, 1.0f},
    {"VJay Extra", "audioBeatSync", 0.0f, 2.0f},
    {"VJay Extra", "audioLfoRate", 0.0f, 2.0f},
    {"VJay Extra", "temporalInterpolation", 0.0f, 1.0f},
    {"VJay Extra", "temporalBlendStrength", 0.0f, 1.0f},
    {"VJay Extra", "slowMotionFactor", 0.1f, 2.0f},
    {"VJay Extra", "frameAccumulation", 0.0f, 1.0f}
};

constexpr int PARAMETER_COUNT = static_cast<int>(sizeof(PARAMETER_INFOS) / sizeof(ParameterInfo));

static const char* TRIGGER_ACTIONS[] = {
    "randomizeVideo",
    "randomizeVideo2",
    "jumpRandom",
    "folderChanged",
    "applyChanges"
};

constexpr int TRIGGER_ACTION_COUNT = static_cast<int>(sizeof(TRIGGER_ACTIONS) / sizeof(const char*));

void drawTriggerAndRgbReferenceSection() {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1.0f), "Trigger actions disponibles");
    for (int i = 0; i < TRIGGER_ACTION_COUNT; ++i) {
        ImGui::BulletText("%s", TRIGGER_ACTIONS[i]);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "RGB overlay mapping");
    ImGui::BulletText("enableRgbOverlay - toggle (0 = off, 1 = on)");
    ImGui::BulletText("rgbOverlay  - vec3 color (0-2 range)");
    ImGui::BulletText("rgbOverlayR/G/B - control channels individually");
    ImGui::BulletText("OSC multi-float: /vjay/rgbOverlay R G B (0-1 floats)");
}

const std::vector<std::string>& getParameterDisplayStrings() {
    static const std::vector<std::string> strings = [] {
        std::vector<std::string> temp;
        temp.reserve(PARAMETER_COUNT);
        for (const auto& info : PARAMETER_INFOS) {
            temp.emplace_back(std::string(info.category) + ": " + info.name);
        }
        return temp;
    }();
    return strings;
}

const std::vector<const char*>& getParameterDisplayPtrs() {
    static const std::vector<const char*> ptrs = [] {
        const auto& strings = getParameterDisplayStrings();
        std::vector<const char*> temp;
        temp.reserve(strings.size());
        for (const auto& str : strings) {
            temp.push_back(str.c_str());
        }
        return temp;
    }();
    return ptrs;
}

int randInt(std::mt19937& rng, int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

bool randBool(std::mt19937& rng) {
    return randInt(rng, 0, 1) == 1;
}

void randomizeVJayExtraControls(VisualControls& controls, std::mt19937& rng) {
    if (controls.fx.enablePixelate) {
        controls.fx.pixelateAmount = randFloat(rng, 0.0f, 1.0f);
    }

    if (controls.fx.enableStrobe) {
        controls.fx.strobeSpeed = randFloat(rng, 0.0f, 20.0f);
    }

    if (controls.fx.enableThreshold) {
        controls.fx.thresholdLevel = randFloat(rng, 0.0f, 1.0f);
    }

    if (controls.fx.enableSlowZoom) {
        controls.fx.slowZoomAmount = randFloat(rng, 0.0f, 1.0f);
    }

    if (controls.fx.enableEdgeDetect) {
        controls.fx.edgeStrength = randFloat(rng, 0.1f, 5.0f);
        controls.fx.edgeThreshold = randFloat(rng, 0.0f, 1.0f);
        controls.fx.edgeBlend = randFloat(rng, 0.0f, 1.0f);
        controls.fx.edgeColor = glm::vec3(randFloat(rng, 0.0f, 1.0f),
                                       randFloat(rng, 0.0f, 1.0f),
                                       randFloat(rng, 0.0f, 1.0f));
    }

    if (controls.system.enableFXAA) {
        controls.system.fxaaQualitySubpix = randFloat(rng, 0.0f, 1.0f);
        controls.system.fxaaQualityEdgeThreshold = randFloat(rng, 0.0f, 0.5f);
        controls.system.fxaaQualityEdgeThresholdMin = randFloat(rng, 0.0f, 0.2f);
    }

    if (controls.fx.enableMirror) {
        controls.fx.mirrorAmount = randFloat(rng, 0.0f, 1.0f);
    }

    if (controls.fx.enablePosterize) {
        controls.fx.posterizeLevels = randFloat(rng, 2.0f, 16.0f);
    }

    if (controls.fx.enableZoomPulse) {
        controls.fx.zoomPulseAmount = randFloat(rng, 0.0f, 1.0f);
    }

    if (controls.fx.enableRGBShift) {
        controls.fx.rgbShiftAmount = randFloat(rng, 0.0f, 0.1f);
    }

    if (controls.grid.enabled) {
        controls.grid.mode = randInt(rng, 0, 2);
        if (controls.grid.mode == 2) {
            controls.grid.rows = randInt(rng, 1, 8);
            controls.grid.columns = randInt(rng, 1, 8);
            controls.grid.count = std::max(2, controls.grid.columns);
        } else {
            controls.grid.count = randInt(rng, 2, 8);
        }
        controls.grid.mirrorCells = randBool(rng);
    }

    if (controls.camera.enableMovement) {
        controls.camera.zoom = randFloat(rng, 0.5f, 2.0f);
        controls.camera.panX = randFloat(rng, -1.0f, 1.0f);
        controls.camera.panY = randFloat(rng, -1.0f, 1.0f);
        controls.camera.rotation = randFloat(rng, -3.14159f, 3.14159f);
    }
}

void randomizeVJayBasicsControls(VisualControls& controls, std::mt19937& rng) {
    auto rr  = [&](float lo, float hi) { return randFloat(rng, lo, hi); };
    auto ri  = [&](int lo, int hi) { return randInt(rng, lo, hi); };
    auto u01 = [&]() { return randFloat(rng, 0.0f, 1.0f); };

    if (controls.color.enableColorGrading) {
        controls.color.gradeBrightness = rr(-0.2f, 0.2f);
        controls.color.gradeContrast = rr(0.8f, 1.4f);
        controls.color.gradeSaturation = rr(0.6f, 1.6f);
        controls.color.gradeHueShift = rr(-90.0f, 90.0f);
        controls.color.gradeGamma = rr(0.8f, 1.4f);
        controls.color.colorLUTIndex = ri(0, 5);
        controls.color.splitToneBalance = u01() * 0.5f;
        controls.color.splitToneShadows = glm::vec3(u01(), u01(), u01());
        controls.color.splitToneHighlights = glm::vec3(u01(), u01(), u01());
    }

    if (controls.temporal.enableFeedback) {
        controls.temporal.feedbackAmount = u01();
        controls.temporal.trailStrength = u01();
        controls.temporal.temporalAccumulation = u01();
        controls.temporal.feedbackDecay = u01();
        controls.temporal.recursiveBlend = u01();
    }

    if (controls.fx.enableDistortion) {
        controls.fx.uvWarpStrength = rr(0.0f, 0.5f);
        controls.fx.rippleStrength = rr(0.0f, 0.5f);
        controls.fx.rippleFrequency = rr(0.5f, 6.0f);
        controls.fx.swirlStrength = rr(-0.5f, 0.5f);
        controls.fx.displacementAmount = rr(0.0f, 0.5f);
        controls.fx.kaleidoSegments = rr(3.0f, 12.0f);
        controls.fx.tunnelDepth = rr(0.0f, 0.5f);
        controls.fx.tunnelCurvature = rr(-0.5f, 0.5f);
    }

    if (controls.fx.enableBlurMotion) {
        controls.fx.gaussianBlur = u01();
        controls.fx.directionalBlur = u01();
        controls.fx.directionalBlurAngle = rr(0.0f, 360.0f);
        controls.fx.zoomBlur = u01();
        controls.fx.motionBlur = u01();
        controls.fx.temporalBlur = u01();
    }

    if (controls.fx.enableSharpen) {
        controls.fx.unsharpMask = u01();
        controls.fx.casAmount = u01();
        controls.fx.localContrast = u01();
    }

    if (controls.fx.enableGlitch) {
        controls.fx.glitchDatamosh = u01();
        controls.fx.glitchRGBSplit = u01();
        controls.fx.glitchScanlineBreak = u01();
        controls.fx.glitchJitter = u01();
        controls.fx.glitchTearing = u01();
        controls.fx.glitchPixelSort = u01();
        controls.fx.glitchBufferCorruption = u01();
    }

    if (controls.blending.enableBlending) {
        controls.blending.blendModeProcedural = ri(0, 5);
        controls.blending.blendModeVideo = ri(0, 5);
        controls.blending.blendModeFeedback = ri(0, 5);
        controls.blending.blendProceduralMix = rr(0.0f, 2.0f);
        controls.blending.blendVideoMix = rr(0.0f, 2.0f);
        controls.blending.blendFeedbackMix = rr(0.0f, 2.0f);
    }

    if (controls.post.enableAnalog) {
        controls.post.analogScanlineFocus = u01();
        controls.post.analogMaskBalance = u01();
        controls.post.analogNoise = u01();
        controls.post.analogBloom = rr(0.0f, 2.0f);
        controls.post.vhsDistortion = u01();
        controls.post.analogChromaticAberration = rr(0.0f, 0.25f);
    }

    if (controls.system.enableAudioReactive) {
        controls.audio.warpResponse = rr(0.0f, 2.0f);
        controls.audio.feedbackResponse = rr(0.0f, 2.0f);
        controls.audio.blurResponse = rr(0.0f, 2.0f);
        controls.audio.colorResponse = rr(0.0f, 2.0f);
        controls.audio.glitchResponse = rr(0.0f, 2.0f);
        controls.audio.beatSync = rr(0.0f, 4.0f);
        controls.audio.lfoRate = rr(0.05f, 4.0f);
    }

    if (controls.temporal.enableTemporal) {
        controls.playback.temporalInterpolation = u01();
        controls.playback.temporalBlendStrength = u01();
        controls.playback.slowMotionFactor = rr(0.1f, 4.0f);
        controls.playback.frameAccumulation = u01();
    }
}
} // namespace

// ============================================================
// Lifecycle
// ============================================================

UISystem::~UISystem() {
    shutdown();
}

bool UISystem::initialize(SDL_Window* win, SDL_Renderer* ren) {
    if (initialized) return true;

    window   = win;
    renderer = ren;

    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "imgui.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding  = 4.0f;

    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        ImGui::DestroyContext(context);
        context = nullptr;
        return false;
    }
    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(context);
        context = nullptr;
        return false;
    }

    initialized = true;
    return true;
}

void UISystem::shutdown() {
    if (!initialized) return;

    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    const char* iniFile = io.IniFilename ? io.IniFilename : "imgui.ini";
    std::cout << "[UISystem] Saving ImGui settings to: " << iniFile << std::endl;
    ImGui::SaveIniSettingsToDisk(iniFile);
    std::cout << "[UISystem] ImGui settings saved." << std::endl;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    if (context) {
        ImGui::DestroyContext(context);
        context = nullptr;
    }
    initialized = false;
}

void UISystem::processEvent(const SDL_Event& event) {
    if (initialized) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
}

// ============================================================
// Frame principal
// ============================================================

void UISystem::render(
    VisualControls&       controls,
    VideoRandomizerState& randomizer,
    VideoRandomizerState2& randomizer2,
    VideoPlayer&          player,
    VideoPlayer&          player2,
    VideoRegistry&        registry,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2,
    float&                transitionDuration,
    float&                transitionDuration2,
    bool&                 allowDimensionChangeRecreation,
    bool&                 controlsDirty,
    std::mt19937&         rng,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks,
    MidiSystem&           midiSystem,
    OscSystem&            oscSystem,
    AudioSystem&          audioSystem,
    const std::string&    video1Path,
    const std::string&    video2Path
) {
    if (!initialized || !renderer) return;

    beginFrame();

    // Draw main navbar window with tabs
    drawMainNavbar(controls, randomizer, randomizer2, player, player2, registry,
                   selectedVideoAsset, selectedVideoAsset2, transitionDuration, transitionDuration2,
                   allowDimensionChangeRecreation, controlsDirty,
                   rng, diag, callbacks, midiSystem, oscSystem, audioSystem, video1Path, video2Path);

    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    endFrame();
}

void UISystem::beginFrame() {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void UISystem::endFrame() {
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
        SDL_SetRenderDrawColor(renderer, 12, 12, 12, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        return;
    }

    SDL_SetRenderDrawColor(renderer, 12, 12, 12, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(drawData, renderer);
    SDL_RenderPresent(renderer);
}

// ============================================================
// Ventana "Procedural Controls"
// ============================================================

void UISystem::drawProceduralControls(
    VisualControls&       controls,
    VideoRandomizerState& randomizer,
    VideoPlayer&          player,
    VideoRegistry&        registry,
    int&                  selectedAsset,
    int&                  selectedAsset2,
    float&                transitionDuration,
    bool&                 allowDimensionChange,
    bool&                 controlsDirty,
    std::mt19937&         rng,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks
) {
    bool changed = false;

    ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Procedural Controls")) { ImGui::End(); return; }

    ImGui::Text("Animation");
    changed |= ImGui::SliderFloat("Speed", &controls.playback.animationSpeed, 0.1f, 8.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Target change (s)", &controls.playback.animationTargetSeconds, 0.1f, 5.0f, "%.2fs");
    ImGui::SameLine();
    if (ImGui::Button("Snap 1s")) {
        controls.playback.animationTargetSeconds = 1.0f;
        controls.playback.animationSpeed = 1.0f;
        changed = true;
    }

    ImGui::Separator();
    ImGui::Text("Layers");
    changed |= drawActiveLayerCombo("Active Layer", controls.playback.activeMode);

    ImGui::Separator();
    ImGui::Text("Color Palette");
    changed |= ImGui::ColorEdit4("Primary",   glm::value_ptr(controls.color.primaryColor));
    changed |= ImGui::ColorEdit4("Secondary", glm::value_ptr(controls.color.secondaryColor));
    changed |= ImGui::SliderFloat("Blend", &controls.color.colorBlend, 0.0f, 1.0f);
    
    if (ImGui::Button("Randomize Colors")) {
        // Generate random colors with good gradient potential using HSV
        std::uniform_real_distribution<float> hueDist(0.0f, 360.0f);
        std::uniform_real_distribution<float> satDist(0.6f, 1.0f);
        std::uniform_real_distribution<float> valDist(0.7f, 1.0f);
        
        float primaryHue = hueDist(rng);
        float primarySat = satDist(rng);
        float primaryVal = valDist(rng);
        
        // Secondary color: complementary or analogous for good gradient
        float secondaryHue = fmod(primaryHue + 180.0f, 360.0f); // Complementary
        float secondarySat = primarySat;
        float secondaryVal = primaryVal;
        
        // Convert HSV to RGB
        auto hsvToRgb = [](float h, float s, float v) -> glm::vec3 {
            float c = v * s;
            float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
            float m = v - c;
            
            float r, g, b;
            if (h < 60.0f) { r = c; g = x; b = 0; }
            else if (h < 120.0f) { r = x; g = c; b = 0; }
            else if (h < 180.0f) { r = 0; g = c; b = x; }
            else if (h < 240.0f) { r = 0; g = x; b = c; }
            else if (h < 300.0f) { r = x; g = 0; b = c; }
            else { r = c; g = 0; b = x; }
            
            return glm::vec3(r + m, g + m, b + m);
        };
        
        controls.color.primaryColorTarget = glm::vec4(hsvToRgb(primaryHue, primarySat, primaryVal), 1.0f);
        controls.color.secondaryColorTarget = glm::vec4(hsvToRgb(secondaryHue, secondarySat, secondaryVal), 1.0f);
        controls.color.primaryColor = controls.color.primaryColorTarget;
        controls.color.secondaryColor = controls.color.secondaryColorTarget;
        changed = true;
        controlsDirty = true;
    }
    
    changed |= ImGui::Checkbox("Auto Randomize", &controls.color.autoRandomizeColors);
    if (controls.color.autoRandomizeColors) {
        changed |= ImGui::SliderFloat("Interval (s)", &controls.color.colorRandomizeInterval, 0.1f, 5.0f, "%.1fs");
    }

    ImGui::Separator();
    ImGui::Text("Audio-inspired inputs");
    changed |= ImGui::SliderFloat("Tempo",  &controls.playback.tempo,  0.25f, 4.0f);
    changed |= ImGui::Checkbox("Auto tempo LFO", &controls.playback.enableTempoLfo);
    if (controls.playback.enableTempoLfo) {
        changed |= ImGui::SliderFloat("LFO speed (Hz)", &controls.playback.tempoLfoSpeed, 0.05f, 4.0f, "%.2f Hz");
        changed |= ImGui::SliderFloat("LFO depth", &controls.playback.tempoLfoDepth, 0.0f, 2.0f);
    }
    changed |= ImGui::SliderFloat("Energy", &controls.audio.energy, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Bass",   &controls.audio.bass,   0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Mid",    &controls.audio.mid,    0.0f, 1.0f);
    changed |= ImGui::SliderFloat("High",   &controls.audio.high,   0.0f, 1.0f);
    changed |= ImGui::SliderFloat("High gain boost", &controls.audio.highGain, 0.5f, 4.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Procedural audio drive", &controls.audio.reactiveDrive, 0.5f, 3.0f, "%.2fx");

    ImGui::Separator();
    ImGui::Text("Video");
    changed |= ImGui::SliderFloat("Video Mix",   &controls.playback.videoMix,         0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Video speed", &controls.playback.videoPlaybackRate, 0.1f, 5.0f, "%.2fx");

    if (ImGui::SliderFloat("Decode oversample", &controls.playback.videoDecodeOversample, 1.0f, 8.0f, "%.1fx")) {
        controls.playback.videoDecodeOversample = std::clamp(controls.playback.videoDecodeOversample, 1.0f, 8.0f);
        changed = true;
    }

    changed |= ImGui::Checkbox("Auto Scale Video", &controls.playback.autoScaleVideo);

    static const char* forceFpsLabels[] = {"Off", "15 fps", "24 fps", "30 fps", "60 fps"};
    int forceIdx = std::clamp(controls.playback.forcedFpsIndex, 0,
                              static_cast<int>(FORCED_FPS_OPTIONS_UI.size()) - 1);
    if (forceIdx != controls.playback.forcedFpsIndex) { controls.playback.forcedFpsIndex = forceIdx; changed = true; }
    if (ImGui::Combo("Force FPS", &forceIdx, forceFpsLabels, IM_ARRAYSIZE(forceFpsLabels))) {
        controls.playback.forcedFpsIndex = forceIdx;
        changed = true;
    }

    changed |= ImGui::SliderFloat("Grayscale", &controls.playback.grayscaleAmount, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sharpen",   &controls.playback.sharpenAmount,   0.0f, 1.0f);
    changed |= ImGui::Checkbox("Bicubic Upscale", &controls.playback.upscaleEnabled);

    changed |= ImGui::SliderFloat("Loop crossfade (s)", &controls.playback.loopBlendSeconds, 0.0f, 2.0f, "%.2f s");

    // --- Video asset selector ---
    ImGui::TextWrapped("Video %s", diag.videoReady ? "online" : "unavailable");

    // Folder selector
    static std::vector<std::string> availableFolders;
    static bool foldersScanned = false;
    if (!foldersScanned) {
        availableFolders.clear();
        availableFolders.push_back("All Folders");
        try {
            fs::path mp4sPath("mp4s");
            if (fs::exists(mp4sPath) && fs::is_directory(mp4sPath)) {
                for (const auto& entry : fs::directory_iterator(mp4sPath)) {
                    if (entry.is_directory()) {
                        availableFolders.push_back(entry.path().filename().string());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[UISystem] Error scanning mp4s folder: " << e.what() << std::endl;
        }
        foldersScanned = true;
    }

    static std::vector<const char*> folderItems;
    folderItems.clear();
    for (auto& folder : availableFolders) {
        folderItems.push_back(folder.c_str());
    }

    int currentFolderIndex = 0;
    if (!controls.playback.selectedVideoFolder.empty()) {
        for (size_t i = 0; i < availableFolders.size(); ++i) {
            if (availableFolders[i] == controls.playback.selectedVideoFolder) {
                currentFolderIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (ImGui::Combo("Load Folder", &currentFolderIndex, folderItems.data(), static_cast<int>(folderItems.size()))) {
        std::string newFolder = (currentFolderIndex == 0) ? "" : availableFolders[currentFolderIndex];
        if (controls.playback.selectedVideoFolder != newFolder) {
            controls.playback.selectedVideoFolder = newFolder;
            if (callbacks.onFolderChanged) {
                callbacks.onFolderChanged();
            }
        }
    }

    // Show current loaded folder
    if (controls.playback.selectedVideoFolder.empty()) {
        ImGui::Text("Current loaded folder: All Folders");
    } else {
        ImGui::Text("Current loaded folder: %s", controls.playback.selectedVideoFolder.c_str());
    }

    // Video assets from current folder
    const auto& assets = registry.getFilteredAssets(controls.playback.selectedVideoFolder);
    if (assets.empty()) {
        ImGui::TextDisabled("No videos found in this folder");
    } else {
        if (selectedAsset < 0 || selectedAsset >= static_cast<int>(assets.size()))
            selectedAsset = 0;

        const std::string& currentLabel = assets[selectedAsset].metadata.filename;
        if (ImGui::BeginCombo("Video Asset", currentLabel.c_str())) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                bool isSelected = (i == selectedAsset);
                std::string label = assets[i].metadata.filename;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    if (selectedAsset != i) {
                        selectedAsset = i;
                        if (callbacks.onReloadVideo)
                            callbacks.onReloadVideo(assets[i].metadata.path);
                    }
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Videos in folder: %zu", assets.size());
    }

    // --- Randomizer ---
    const bool hasRandomChoices = assets.size() > 1;
    ImGui::BeginDisabled(randomizer.useVideoDuration);
    if (ImGui::SliderFloat("Random interval (s)", &randomizer.intervalSeconds, 1.0f, 300.0f, "%.0f s")) {
        randomizer.intervalSeconds = std::clamp(randomizer.intervalSeconds, 1.0f, 600.0f);
        randomizer.elapsedSeconds  = 0.0f;
        changed = true;
    }
    ImGui::EndDisabled();

    bool syncToggle = ImGui::Checkbox("Sync shuffle to clip duration", &randomizer.useVideoDuration);
    if (syncToggle) randomizer.elapsedSeconds = 0.0f;
    changed |= syncToggle;

    if (randomizer.useVideoDuration)
        ImGui::Text("Current clip: %.1f s", std::max(0.0f, randomizer.currentVideoDuration));

    changed |= ImGui::SliderFloat("Transition duration (s)", &transitionDuration, 0.1f, 2.0f, "%.2f s");
    changed |= ImGui::Checkbox("Allow dimension change recreation", &allowDimensionChange);

    ImGui::BeginDisabled(!hasRandomChoices);
    if (ImGui::Button("Randomize video online") && callbacks.onRandomizeVideo) {
        callbacks.onRandomizeVideo();
        changed = true;
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Auto randomize", &randomizer.autoRandomize);
    if (!hasRandomChoices) {
        ImGui::SameLine();
        ImGui::TextDisabled("Need at least 2 videos");
    } else if (randomizer.autoRandomize) {
        float target = (randomizer.useVideoDuration && randomizer.currentVideoDuration > 0.0f)
                           ? randomizer.currentVideoDuration
                           : randomizer.intervalSeconds;
        ImGui::Text("Next shuffle in %.1f s", std::max(0.0f, target - randomizer.elapsedSeconds));
    }

    ImGui::Indent();
    changed |= ImGui::Checkbox("Shuffle all videos", &randomizer.useShuffleMode);
    if (randomizer.useShuffleMode && !randomizer.shuffleQueue.empty()) {
        ImGui::Text("Playing %d/%d", randomizer.currentShuffleIndex + 1, static_cast<int>(randomizer.shuffleQueue.size()));
    }
    ImGui::Unindent();
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Checkbox("Show diagnostics window", &showDiagnostics);
    ImGui::Checkbox("Show ImGui demo",         &showDemoWindow);
    ImGui::Checkbox("Show NLE Editor",         &showNLEWindow);

    if (changed && callbacks.onControlsChanged) callbacks.onControlsChanged();
    ImGui::End();
}

// ============================================================
// Ventana "VJAY BASICS"
// ============================================================

void UISystem::drawVJayBasics(VisualControls& c, bool& controlsDirty, std::mt19937& rng) {
    bool changed = false;

    static const char* LUT_ITEMS   = "Off\0Filmic\0Neon\0Noir\0Heatmap\0Analog CRT\0";
    static const char* BLEND_ITEMS = "Add\0Screen\0Multiply\0Overlay\0Difference\0Soft Light\0";

    ImGui::SetNextWindowSize(ImVec2(430.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("VJAY BASICS")) { ImGui::End(); return; }

    auto setAllToggles = [&](bool v) {
        c.color.enableColorGrading  = v; c.temporal.enableFeedback    = v; c.fx.enableDistortion = v;
        c.fx.enableBlurMotion    = v; c.fx.enableSharpen     = v; c.fx.enableGlitch     = v;
        c.blending.enableBlending      = v; c.post.enableAnalog      = v; c.system.enableAudioReactive = v;
        c.temporal.enableTemporal      = v;
    };

    if (ImGui::Button("Randomize VJAY basics")) {
        randomizeVJayBasicsControls(c, rng);
        changed = true; controlsDirty = true;
    }

    if (ImGui::Button("Turn all ON"))  { setAllToggles(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Turn all OFF")) { setAllToggles(false); changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Reset VJAY basics")) {
        setAllToggles(false);
        c.color.gradeBrightness = 0; c.color.gradeContrast = 1; c.color.gradeSaturation = 1;
        c.color.gradeHueShift = 0; c.color.gradeGamma = 1; c.color.colorLUTIndex = 0;
        c.color.splitToneBalance = 0.5f;
        c.color.splitToneShadows = glm::vec3(0); c.color.splitToneHighlights = glm::vec3(1);
        c.temporal.feedbackAmount = 0; c.temporal.trailStrength = 0; c.temporal.temporalAccumulation = 0;
        c.temporal.feedbackDecay = 0; c.temporal.recursiveBlend = 0;
        c.fx.uvWarpStrength = 0; c.fx.rippleStrength = 0; c.fx.rippleFrequency = 1;
        c.fx.swirlStrength = 0; c.fx.displacementAmount = 0; c.fx.kaleidoSegments = 6;
        c.fx.tunnelDepth = 0; c.fx.tunnelCurvature = 0;
        c.fx.gaussianBlur = 0; c.fx.directionalBlur = 0; c.fx.directionalBlurAngle = 0;
        c.fx.zoomBlur = 0; c.fx.motionBlur = 0; c.fx.temporalBlur = 0;
        c.fx.unsharpMask = 0; c.fx.casAmount = 0; c.fx.localContrast = 0;
        c.fx.glitchDatamosh = 0; c.fx.glitchRGBSplit = 0; c.fx.glitchScanlineBreak = 0;
        c.fx.glitchJitter = 0; c.fx.glitchTearing = 0; c.fx.glitchPixelSort = 0; c.fx.glitchBufferCorruption = 0;
        c.blending.blendModeProcedural = 0; c.blending.blendModeVideo = 1; c.blending.blendModeFeedback = 2;
        c.blending.blendProceduralMix = 1; c.blending.blendVideoMix = 1; c.blending.blendFeedbackMix = 0.5f;
        c.post.analogScanlineFocus = 0.5f; c.post.analogMaskBalance = 0.5f; c.post.analogNoise = 0.2f;
        c.post.analogBloom = 0.3f; c.post.vhsDistortion = 0; c.post.analogChromaticAberration = 0.02f;
        c.audio.warpResponse = 0; c.audio.feedbackResponse = 0; c.audio.blurResponse = 0;
        c.audio.colorResponse = 0; c.audio.glitchResponse = 0; c.audio.beatSync = 0; c.audio.lfoRate = 0.5f;
        c.playback.temporalInterpolation = 0; c.playback.temporalBlendStrength = 0; c.playback.slowMotionFactor = 1; c.playback.frameAccumulation = 0;
        changed = true; controlsDirty = true;
    }

    // Macro para secciones con toggle on/off
#define VJAY_SECTION(num, title, toggleExpr, body) \
    { \
        ImGui::Text(num ". " title); ImGui::SameLine(); \
        auto& toggleRef__ = (toggleExpr); \
        changed |= ImGui::Checkbox("On##" title, &toggleRef__); \
        ImGui::Separator(); \
        ImGui::BeginDisabled(!toggleRef__); \
        body \
        ImGui::EndDisabled(); \
        ImGui::Spacing(); \
    }

    VJAY_SECTION("1","Color grading dinamico", c.color.enableColorGrading,
        changed |= ImGui::SliderFloat("Brightness",       &c.color.gradeBrightness,  -1.0f, 1.0f,   "%.2f");
        changed |= ImGui::SliderFloat("Contrast",         &c.color.gradeContrast,     0.1f, 2.5f,   "%.2f");
        changed |= ImGui::SliderFloat("Saturation",       &c.color.gradeSaturation,   0.0f, 2.5f,   "%.2f");
        changed |= ImGui::SliderFloat("Hue shift",        &c.color.gradeHueShift,  -180.0f, 180.0f, "%.1f\xc2\xb0");
        changed |= ImGui::SliderFloat("Gamma",            &c.color.gradeGamma,        0.4f, 3.0f,   "%.2f");
        changed |= ImGui::Combo("LUT",                    &c.color.colorLUTIndex,     LUT_ITEMS);
        changed |= ImGui::SliderFloat("Split tone balance",&c.color.splitToneBalance, 0.0f, 1.0f,   "%.2f");
        changed |= ImGui::ColorEdit3("Split tone shadows",    glm::value_ptr(c.color.splitToneShadows));
        changed |= ImGui::ColorEdit3("Split tone highlights", glm::value_ptr(c.color.splitToneHighlights));
    )
    VJAY_SECTION("2","Feedback temporal", c.temporal.enableFeedback,
        changed |= ImGui::SliderFloat("Feedback",              &c.temporal.feedbackAmount,      0,1,"%.2f");
        changed |= ImGui::SliderFloat("Trails",                &c.temporal.trailStrength,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Temporal accumulation", &c.temporal.temporalAccumulation,0,1,"%.2f");
        changed |= ImGui::SliderFloat("Decay",                 &c.temporal.feedbackDecay,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Recursive blend",       &c.temporal.recursiveBlend,      0,1,"%.2f");
    )
    VJAY_SECTION("3","Distorsion espacial", c.fx.enableDistortion,
        changed |= ImGui::SliderFloat("UV warp",      &c.fx.uvWarpStrength,    0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Ripple",       &c.fx.rippleStrength,    0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Ripple freq",  &c.fx.rippleFrequency,   0.5f,  6.0f, "%.1f");
        changed |= ImGui::SliderFloat("Swirl",        &c.fx.swirlStrength,    -0.5f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Displacement", &c.fx.displacementAmount,0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Kaleido segs", &c.fx.kaleidoSegments,   3.0f, 12.0f, "%.0f");
        changed |= ImGui::SliderFloat("Tunnel depth", &c.fx.tunnelDepth,       0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Tunnel curv",  &c.fx.tunnelCurvature,  -0.5f,  0.5f, "%.3f");
    )
    VJAY_SECTION("4","Blur & motion", c.fx.enableBlurMotion,
        changed |= ImGui::SliderFloat("Gaussian blur",     &c.fx.gaussianBlur,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("Directional blur",  &c.fx.directionalBlur,     0,1,"%.2f");
        changed |= ImGui::SliderFloat("Directional angle", &c.fx.directionalBlurAngle,0,360,"%.0f\xc2\xb0");
        changed |= ImGui::SliderFloat("Zoom blur",         &c.fx.zoomBlur,            0,1,"%.2f");
        changed |= ImGui::SliderFloat("Motion blur",       &c.fx.motionBlur,          0,1,"%.2f");
        changed |= ImGui::SliderFloat("Temporal blur",     &c.fx.temporalBlur,        0,1,"%.2f");
    )
    VJAY_SECTION("5","Sharpen / detalle", c.fx.enableSharpen,
        changed |= ImGui::SliderFloat("Unsharp mask",  &c.fx.unsharpMask,   0,1,"%.2f");
        changed |= ImGui::SliderFloat("CAS",           &c.fx.casAmount,     0,1,"%.2f");
        changed |= ImGui::SliderFloat("Local contrast",&c.fx.localContrast, 0,1,"%.2f");
    )
    VJAY_SECTION("6","Glitch / corruption", c.fx.enableGlitch,
        changed |= ImGui::SliderFloat("Datamosh",          &c.fx.glitchDatamosh,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("RGB split",         &c.fx.glitchRGBSplit,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Scanline break",    &c.fx.glitchScanlineBreak,  0,1,"%.2f");
        changed |= ImGui::SliderFloat("Jitter",            &c.fx.glitchJitter,         0,1,"%.2f");
        changed |= ImGui::SliderFloat("Tearing",           &c.fx.glitchTearing,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("Pixel sorting",     &c.fx.glitchPixelSort,      0,1,"%.2f");
        changed |= ImGui::SliderFloat("Buffer corruption", &c.fx.glitchBufferCorruption,0,1,"%.2f");
    )
    VJAY_SECTION("7","Compositing & blending", c.blending.enableBlending,
        changed |= ImGui::Combo("Video blend",     &c.blending.blendModeVideo,     BLEND_ITEMS);
        changed |= ImGui::Combo("Feedback blend",  &c.blending.blendModeFeedback,  BLEND_ITEMS);
        changed |= ImGui::SliderFloat("Video mix",     &c.blending.blendVideoMix,    0,2,"%.2f");
        changed |= ImGui::SliderFloat("Feedback mix",  &c.blending.blendFeedbackMix, 0,2,"%.2f");
    )
    VJAY_SECTION("8","CRT / analog simulation", c.post.enableAnalog,
        changed |= ImGui::SliderFloat("Scanline focus",&c.post.analogScanlineFocus,        0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Mask balance",  &c.post.analogMaskBalance,          0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Analog noise",  &c.post.analogNoise,                0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Analog bloom",  &c.post.analogBloom,                0,2,   "%.2f");
        changed |= ImGui::SliderFloat("VHS distortion",&c.post.vhsDistortion,              0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Analog chroma", &c.post.analogChromaticAberration,  0,0.25f,"%.3f");
    )
    VJAY_SECTION("9","Audio reactivity", c.system.enableAudioReactive,
        changed |= ImGui::SliderFloat("Warp response",    &c.audio.warpResponse,    0,2,"%.2f");
        changed |= ImGui::SliderFloat("Feedback response",&c.audio.feedbackResponse,0,2,"%.2f");
        changed |= ImGui::SliderFloat("Blur response",    &c.audio.blurResponse,    0,2,"%.2f");
        changed |= ImGui::SliderFloat("Color response",   &c.audio.colorResponse,   0,2,"%.2f");
        changed |= ImGui::SliderFloat("Glitch response",  &c.audio.glitchResponse,  0,2,"%.2f");
        changed |= ImGui::SliderFloat("Beat sync",        &c.audio.beatSync,        0,4,"%.2f");
        changed |= ImGui::SliderFloat("LFO rate",         &c.audio.lfoRate,     0.05f,4,"%.2f Hz");
    )
    VJAY_SECTION("10","Temporal speed processing", c.temporal.enableTemporal,
        changed |= ImGui::SliderFloat("Frame interpolation",&c.playback.temporalInterpolation, 0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Temporal blend",     &c.playback.temporalBlendStrength, 0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Slow-motion",        &c.playback.slowMotionFactor,  0.1f,4,   "%.2fx");
        changed |= ImGui::SliderFloat("Frame accumulation", &c.playback.frameAccumulation,     0,1,   "%.2f");
    )
#undef VJAY_SECTION

    ImGui::End();
    if (changed) controlsDirty = true;
}

// ============================================================
// Ventana "VJAY EXTRA"
// ============================================================

void UISystem::drawVJayExtra(VisualControls& c, bool& controlsDirty, std::mt19937& rng) {
    bool changed = false;

    ImGui::SetNextWindowSize(ImVec2(350.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("VJAY EXTRA")) { ImGui::End(); return; }

    auto setAllExtraToggles = [&](bool v) {
        c.fx.enablePixelate = v; c.fx.enableStrobe = v; c.fx.enableThreshold = v; c.fx.enableSlowZoom = v;
        c.fx.enableMirror = v; c.fx.enableInvert = v; c.fx.enablePosterize = v; c.fx.enableInfrared = v;
        c.fx.enableZoomPulse = v; c.fx.enableRGBShift = v; c.system.enableFXAA = v; c.grid.enabled = v;
        c.fx.enableEdgeDetect = v; c.camera.enableMovement = v;
    };

    if (ImGui::Button("Randomize VJAY extra")) {
        randomizeVJayExtraControls(c, rng);
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset VJAY extra")) {
        c.fx.pixelateAmount = 0; c.fx.strobeSpeed = 0; c.fx.thresholdLevel = 0.5f; c.fx.slowZoomAmount = 0;
        c.fx.enablePixelate = c.fx.enableStrobe = c.fx.enableThreshold = c.fx.enableSlowZoom = false;
        c.system.enableFXAA = true;
        c.system.fxaaQualitySubpix = 0.75f;
        c.system.fxaaQualityEdgeThreshold = 0.125f;
        c.system.fxaaQualityEdgeThresholdMin = 0.0625f;
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Turn all ON"))  { setAllExtraToggles(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Turn all OFF")) { setAllExtraToggles(false); changed = true; controlsDirty = true; }

#define EXTRA_SECTION(num, label, toggleExpr, slider) \
    { \
        ImGui::Spacing(); ImGui::Text(num ". " label); ImGui::SameLine(); \
        auto& extraToggle__ = (toggleExpr); \
        changed |= ImGui::Checkbox("On##" label, &extraToggle__); \
        ImGui::Separator(); \
        ImGui::BeginDisabled(!extraToggle__); \
        slider \
        ImGui::EndDisabled(); \
    }

    EXTRA_SECTION("1","Pixelate",  c.fx.enablePixelate,  changed |= ImGui::SliderFloat("Pixelate amount", &c.fx.pixelateAmount,  0,1,"%.2f");)
    EXTRA_SECTION("2","Strobe",    c.fx.enableStrobe,    changed |= ImGui::SliderFloat("Strobe speed",    &c.fx.strobeSpeed,    0,20,"%.1f Hz");)
    EXTRA_SECTION("3","Threshold", c.fx.enableThreshold, changed |= ImGui::SliderFloat("Threshold level", &c.fx.thresholdLevel,  0,1,"%.2f");)
    EXTRA_SECTION("4","Slow Zoom", c.fx.enableSlowZoom,  changed |= ImGui::SliderFloat("Slow zoom amount",&c.fx.slowZoomAmount,  0,1,"%.2f");)
    EXTRA_SECTION("5","Edge Detect", c.fx.enableEdgeDetect,
        changed |= ImGui::SliderFloat("Edge strength",  &c.fx.edgeStrength,  0.1f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("Edge threshold", &c.fx.edgeThreshold, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Edge blend",     &c.fx.edgeBlend,     0.0f, 1.0f, "%.2f");
        changed |= ImGui::ColorEdit3("Edge color", glm::value_ptr(c.fx.edgeColor));
    )
#undef EXTRA_SECTION

    // FXAA - Fast Approximate Anti-Aliasing for smooth HD edges
    ImGui::Spacing(); ImGui::Text("6. FXAA (Anti-Aliasing)"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##FXAA", &c.system.enableFXAA);
    ImGui::Separator();
    ImGui::BeginDisabled(!c.system.enableFXAA);
    changed |= ImGui::SliderFloat("Quality Subpix",      &c.system.fxaaQualitySubpix,       0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("Edge Threshold",      &c.system.fxaaQualityEdgeThreshold, 0.0f, 0.5f, "%.4f");
    changed |= ImGui::SliderFloat("Edge Threshold Min",  &c.system.fxaaQualityEdgeThresholdMin, 0.0f, 0.2f, "%.4f");
    ImGui::EndDisabled();

    // Grid / Mirroring - Show video in multiple positions
    ImGui::Spacing(); ImGui::Text("7. Grid / Mirroring"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Grid", &c.grid.enabled);
    ImGui::Separator();
    ImGui::BeginDisabled(!c.grid.enabled);
    static const char* gridModeLabels[] = {"Vertical (side by side)", "Horizontal (stacked)", "Matrix (grid 2D)"};
    changed |= ImGui::Combo("Mode", &c.grid.mode, gridModeLabels, 3);

    if (c.grid.mode == 2) {
        // Matrix mode: show rows and columns
        changed |= ImGui::SliderInt("Rows", &c.grid.rows, 1, 8);
        changed |= ImGui::SliderInt("Columns", &c.grid.columns, 1, 8);
    } else {
        // Vertical or Horizontal mode: show count
        changed |= ImGui::SliderInt("Count", &c.grid.count, 2, 8);
    }

    changed |= ImGui::Checkbox("Mirror cells", &c.grid.mirrorCells);
    changed |= ImGui::Checkbox("Show grid lines", &c.grid.showLines);
    ImGui::BeginDisabled(!c.grid.showLines);
    changed |= ImGui::SliderFloat("Line width", &c.grid.lineWidth, 0.0001f, 0.05f);
    changed |= ImGui::SliderFloat("Line intensity", &c.grid.lineIntensity, 0.0f, 1.0f);
    changed |= ImGui::ColorEdit3("Line color", &c.grid.lineColor[0]);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::End();
    if (changed) controlsDirty = true;
}

// ============================================================
// Ventana "NLE Editor"
// ============================================================

void UISystem::drawNLEEditor(const UICallbacks& callbacks, const std::string& video1Path, const std::string& video2Path) {
    ImGui::SetNextWindowSize(ImVec2(400.0f, 500.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("NLE Editor", &showNLEWindow)) { ImGui::End(); return; }

    ImGui::Text("NLE Effects (for rendering/exporting only)");
    ImGui::TextDisabled("These effects are applied when rendering to a new file");
    ImGui::Separator();

    ImGui::Text("Video Source:");
    ImGui::SameLine();
    static int videoSource = static_cast<int>(g_project_state.nleVideoSource);
    if (ImGui::RadioButton("Video 1", &videoSource, 0)) {
        g_project_state.nleVideoSource = NLEVideoSource::VIDEO_1;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Video 2", &videoSource, 1)) {
        g_project_state.nleVideoSource = NLEVideoSource::VIDEO_2;
    }

    // Update active_file based on selection (store full path for rendering)
    const std::string& currentPath = (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) ? video1Path : video2Path;
    if (!currentPath.empty()) {
        g_project_state.active_file = currentPath;
    } else {
        g_project_state.active_file = "(no video loaded)";
    }

    ImGui::Separator();
    ImGui::Text("Quality Presets:");

    auto preset = [](int w, int h) {
        g_project_state.width = w; g_project_state.height = h;
    };
    if (ImGui::Button("Auto"))    { preset(0,0); g_project_state.fps = 0; }
    ImGui::SameLine(); if (ImGui::Button("240p"))     preset(426,240);
    ImGui::SameLine(); if (ImGui::Button("480p 4:3")) preset(640,480);
    ImGui::SameLine(); if (ImGui::Button("720p"))     preset(1280,720);
    ImGui::SameLine(); if (ImGui::Button("1080p"))    preset(1920,1080);
    ImGui::SameLine(); if (ImGui::Button("1080p 4:3"))preset(1440,1080);

    ImGui::Separator();
    ImGui::SliderFloat("Speed",  &g_project_state.speed,  0.25f, 4.0f, "%.2fx");
    ImGui::SliderInt("FPS",      &g_project_state.fps,    0, 120);
    ImGui::SliderInt("Width",    &g_project_state.width,  0, 3840);
    ImGui::SliderInt("Height",   &g_project_state.height, 0, 2160);

    static char scale_flags_buf[64] = "lanczos";
    // Sync buffer with scale_flags if they differ
    if (g_project_state.scale_flags != std::string(scale_flags_buf)) {
        strncpy(scale_flags_buf, g_project_state.scale_flags.c_str(), sizeof(scale_flags_buf) - 1);
        scale_flags_buf[sizeof(scale_flags_buf) - 1] = '\0';
    }
    ImGui::InputText("Scale Flags", scale_flags_buf, sizeof(scale_flags_buf));
    g_project_state.scale_flags = scale_flags_buf;

    ImGui::Checkbox("Enable Unsharp", &g_project_state.enable_unsharp);
    if (g_project_state.enable_unsharp) {
        ImGui::SliderFloat("Unsharp Amount", &g_project_state.unsharp_amount, 0.0f, 2.0f);
        ImGui::SliderFloat("Unsharp Radius", &g_project_state.unsharp_radius, 1.0f, 10.0f);
    }

    ImGui::Separator();
    static char output_file_buf[256] = "output.mp4";
    // Sync buffer with active_file if they differ
    if (g_project_state.output_file != std::string(output_file_buf)) {
        strncpy(output_file_buf, g_project_state.output_file.c_str(), sizeof(output_file_buf) - 1);
        output_file_buf[sizeof(output_file_buf) - 1] = '\0';
    }
    ImGui::InputText("Output File", output_file_buf, sizeof(output_file_buf));
    g_project_state.output_file = output_file_buf;
    ImGui::Separator();

    bool canRender = (g_project_state.active_file != "(no video loaded)");
    if (!canRender) {
        ImGui::BeginDisabled(true);
        ImGui::Button("Render/Export (No video loaded)");
        ImGui::EndDisabled();
    } else if (ImGui::Button("Render/Export")) {
        g_project_state.do_swap = true;
        g_project_state.increment_version();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Changes") && callbacks.onApplyChanges) {
        callbacks.onApplyChanges();
    }

    ImGui::Separator();
    ImGui::Text("Current File: %s", g_project_state.active_file.c_str());
    ImGui::Text("Version: %lu",     g_project_state.get_version());
    ImGui::Text("Dirty: %s",        g_project_state.is_dirty() ? "Yes" : "No");

    ImGui::End();
}

// ============================================================
// Ventana "Diagnostics"
// ============================================================

void UISystem::drawDiagnostics(
    const UIDiagnostics& diag,
    VideoPlayer&         player,
    VideoPlayer&         player2,
    VideoRegistry&       registry,
    int&                 selectedAsset,
    int&                 selectedAsset2,
    VisualControls&      controls,
    const UICallbacks&   callbacks)
{
    ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Diagnostics", &showDiagnostics)) { ImGui::End(); return; }

    ImGui::Text("Frame %u | Image %u", diag.lastFrameFrameIndex, diag.lastFrameImageIndex);
    ImGui::Text("Swapchain: %ux%u",    diag.swapchainWidth, diag.swapchainHeight);
    ImGui::Text("Current mode: %d",    diag.currentMode);
    ImGui::Text("FPS: %.1f",           ImGui::GetIO().Framerate);

    if (diag.videoReady) {
        ImGui::Text("Video: %ux%u", diag.videoWidth, diag.videoHeight);
        if (player.isReady()) {
            double fd  = std::max(1e-6, player.frameDuration());
            double fps = 1.0 / fd;
            double oversample = std::max(1.0, static_cast<double>(controls.playback.videoDecodeOversample));
            double playRate = [&]() -> double {
                // Calculo inline de effectivePlaybackRate
                double base = std::clamp((double)controls.playback.videoPlaybackRate, 0.05, 8.0);
                int idx = std::clamp(controls.playback.forcedFpsIndex, 0,
                                     static_cast<int>(FORCED_FPS_OPTIONS_UI.size())-1);
                int forced = FORCED_FPS_OPTIONS_UI[idx];
                if (forced <= 0) return base;
                return std::clamp(forced / fps, 0.05, 8.0);
            }();
            ImGui::Text("Clip FPS: %.2f",     fps);
            ImGui::Text("Display FPS: %.2f",  fps * playRate);
            ImGui::Text("Decode FPS:  %.2f",  fps * std::max(playRate, oversample));
            if (controls.playback.forcedFpsIndex > 0)
                ImGui::Text("Forced FPS: %d", FORCED_FPS_OPTIONS_UI[controls.playback.forcedFpsIndex]);
        }
    } else {
        ImGui::Text("Video offline");
    }

    if (ImGui::Button("Reset Palette")) {
        controls.color.primaryColor   = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
        controls.color.secondaryColor = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
    }

    const auto& assets = registry.getAssets();
    if (selectedAsset >= 0 && selectedAsset < static_cast<int>(assets.size())) {
        const auto& meta = assets[selectedAsset].metadata;
        ImGui::Separator();
        ImGui::Text("Asset: %s",      meta.filename.c_str());
        ImGui::Text("Resolution: %dx%d", meta.width, meta.height);
        ImGui::Text("FPS: %.2f",      meta.fps);
        ImGui::Text("Duration: %.2f s", meta.duration);
        ImGui::Text("Bitrate: %.0f kbps", meta.bitrate / 1000.0);
        ImGui::Text("Audio: %s",      meta.hasAudio ? "yes" : "no");
        ImGui::Separator();

        static bool showRename = false;
        if (ImGui::Button("Rename")) showRename = !showRename;
        ImGui::SameLine();
        if (ImGui::Button("Delete") && callbacks.onDeleteAsset)
            callbacks.onDeleteAsset(selectedAsset);

        if (showRename) {
            static char renameBuf[256] = "";
            ImGui::InputText("New Filename", renameBuf, sizeof(renameBuf));
            ImGui::SameLine();
            if (ImGui::Button("Confirm") && strlen(renameBuf) > 0) {
                if (callbacks.onRenameAsset)
                    callbacks.onRenameAsset(selectedAsset, renameBuf);
                memset(renameBuf, 0, sizeof(renameBuf));
                showRename = false;
            }
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Animation Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("CPU elapsed: %.3f s", diag.animationElapsedSeconds);
        ImGui::Text("UBO time: %.3f", diag.animationTime);
        ImGui::Text("UBO delta: %.6f", diag.animationDelta);
        ImGui::Text("Speed multiplier: %.2fx", diag.animationRelativeSpeed);
        ImGui::Text("Seconds per unit: %.3f", diag.animationSecondsPerUnit);
        bool targetChanged = ImGui::SliderFloat("Target seconds", &controls.playback.animationTargetSeconds, 0.1f, 5.0f, "%.2fs");
        if (targetChanged && callbacks.onControlsChanged) callbacks.onControlsChanged();
        float suggestedSpeed = 1.0f / std::max(0.1f, controls.playback.animationTargetSeconds);
        ImGui::Text("Suggested speed: %.2fx", suggestedSpeed);
        if (ImGui::Button("Apply suggested speed")) {
            controls.playback.animationSpeed = suggestedSpeed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset time phase")) {
            controls.playback.animationSpeed = 1.0f;
            controls.playback.animationTargetSeconds = 1.0f;
            if (callbacks.onControlsChanged) callbacks.onControlsChanged();
        }
    }

    ImGui::End();
}

void UISystem::drawMidiControls(MidiSystem& midiSystem) {
    ImGui::Begin("MIDI Controls", &showMidiWindow);

    // Enable/Disable MIDI
    bool enabled = midiSystem.isEnabled();
    if (ImGui::Checkbox("Enable MIDI", &enabled)) {
        midiSystem.setEnabled(enabled);
    }

    ImGui::Separator();

    // Port selection
    unsigned int portCount = midiSystem.getPortCount();
    ImGui::Text("MIDI Ports: %u", portCount);

    static int selectedPort = 0;
    if (portCount > 0) {
        auto ports = midiSystem.getAvailablePorts();
        std::vector<const char*> portNames;
        for (const auto& port : ports) {
            portNames.push_back(port.c_str());
        }

        if (ImGui::Combo("Port", &selectedPort, portNames.data(), static_cast<int>(portCount))) {
            midiSystem.closePort();
            if (midiSystem.openPort(selectedPort)) {
                std::cout << "[UI] Opened MIDI port " << selectedPort << std::endl;
            }
        }
    } else {
        ImGui::Text("No MIDI devices detected");
    }

    ImGui::Separator();

    // MIDI Learn Wizard
    ImGui::Text("MIDI Learn Wizard");
    bool learnMode = midiSystem.isLearnMode();
    if (ImGui::Checkbox("Learn Mode", &learnMode)) {
        midiSystem.setLearnMode(learnMode);
    }

    if (midiSystem.isLearnMode()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Move a knob or press a key on your MIDI device...");
    }

    if (midiSystem.hasLearnedMessage()) {
        MidiMessage msg = midiSystem.getLastLearnedMessage();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Detected MIDI Message:");
        
        if (msg.type == MidiEventType::CONTROL_CHANGE) {
            ImGui::Text("Type: Control Change");
            ImGui::Text("CC Number: %d", msg.controller);
            ImGui::Text("Value: %d", msg.value);

            ImGui::Separator();
            ImGui::Text("Assign to Parameter:");

            static int selectedParamIndex = 0;
            selectedParamIndex = std::clamp(selectedParamIndex, 0, PARAMETER_COUNT - 1);

            const auto& displayStrings = getParameterDisplayStrings();
            ImGui::Combo("Parameter", &selectedParamIndex, [](void* data, int idx) -> const char* {
                const auto* strings = static_cast<const std::vector<std::string>*>(data);
                return (*strings)[idx].c_str();
            }, (void*)&displayStrings, static_cast<int>(displayStrings.size()));

            static bool invert = false;
            ImGui::Checkbox("Invert", &invert);

            ImGui::Text("Range: %.2f to %.2f", PARAMETER_INFOS[selectedParamIndex].minVal, PARAMETER_INFOS[selectedParamIndex].maxVal);

            if (ImGui::Button("Assign Mapping")) {
                std::string paramName = PARAMETER_INFOS[selectedParamIndex].name;
                midiSystem.addMapping(msg.controller, paramName,
                    PARAMETER_INFOS[selectedParamIndex].minVal,
                    PARAMETER_INFOS[selectedParamIndex].maxVal, invert);
                std::cout << "[UI] Assigned CC " << msg.controller << " to " << paramName << std::endl;
                midiSystem.clearLearnedMessage();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                midiSystem.clearLearnedMessage();
            }
        } else if (msg.type == MidiEventType::NOTE_ON) {
            ImGui::Text("Type: Note On");
            ImGui::Text("Note: %d", msg.note);
            ImGui::Text("Velocity: %d", msg.velocity);

            ImGui::Separator();
            ImGui::Text("Assign to Trigger Action:");

            static int selectedTriggerActionIdx = 0;
            ImGui::Combo("Action##MidiLearnWindow", &selectedTriggerActionIdx, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);

            if (ImGui::Button("Assign Trigger")) {
                const char* actionName = TRIGGER_ACTIONS[selectedTriggerActionIdx];
                midiSystem.addTriggerMapping(msg.note, actionName);
                std::cout << "[UI] Assigned MIDI note " << msg.note << " to " << actionName << std::endl;
                midiSystem.clearLearnedMessage();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                midiSystem.clearLearnedMessage();
            }
        } else {
            ImGui::Text("This MIDI message type is not supported for learn mode yet.");
            if (ImGui::Button("Cancel")) {
                midiSystem.clearLearnedMessage();
            }
        }

        drawTriggerAndRgbReferenceSection();
    }

    ImGui::Separator();

    // MIDI mappings display
    ImGui::Text("Current Mappings:");
    const auto& mappings = midiSystem.getMappings();
    if (ImGui::BeginTable("MidiMappings", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("CC");
        ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Max");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        // Copy to vector to avoid iterator invalidation when removing
        std::vector<std::pair<int, MidiMapping>> mappingsCopy(mappings.begin(), mappings.end());
        for (const auto& [cc, mapping] : mappingsCopy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", cc);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.parameterName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", mapping.minValue);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", mapping.maxValue);
            ImGui::TableSetColumnIndex(4);
            std::string label = "Remove##" + std::to_string(cc);
            if (ImGui::Button(label.c_str())) {
                midiSystem.removeMapping(cc);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("MIDI Trigger Buttons:");
    const auto& triggerMappings = midiSystem.getTriggerMappings();
    if (ImGui::BeginTable("MidiTriggerMappings", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Note");
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove");
        ImGui::TableHeadersRow();

        std::vector<std::pair<int, MidiTriggerMapping>> triggersCopy(triggerMappings.begin(), triggerMappings.end());
        for (const auto& [note, mapping] : triggersCopy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", note);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.actionName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string label = "Remove##MidiTrig" + std::to_string(note);
            if (ImGui::Button(label.c_str())) {
                midiSystem.removeTriggerMapping(note);
            }
        }
        ImGui::EndTable();
    } else if (triggerMappings.empty()) {
        ImGui::TextDisabled("No MIDI triggers assigned yet");
    }

    ImGui::Separator();
    ImGui::Text("Default CC Mappings (can be overridden):");
    ImGui::Text("CC 1: animationSpeed");
    ImGui::Text("CC 2: tempo");
    ImGui::Text("CC 3: energy");
    ImGui::Text("CC 4: bass");
    ImGui::Text("CC 5: mid");
    ImGui::Text("CC 6: high");
    ImGui::Text("CC 7: colorBlend");
    ImGui::Text("CC 8: bloomIntensity");
    ImGui::Text("CC 9: bloomThreshold");
    ImGui::Text("CC 10: aberrationAmount");
    ImGui::Text("CC 11: grainStrength");
    ImGui::Text("CC 12: feedbackAmount");
    ImGui::Text("CC 13: uvWarpStrength");
    ImGui::Text("CC 14: glitchAmount");
    ImGui::Text("CC 15: gradeBrightness");
    ImGui::Text("CC 16: gradeContrast");
    ImGui::Text("CC 17: gradeSaturation");
    ImGui::Text("CC 18: videoPlaybackRate");
    ImGui::Text("CC 19: pixelateAmount");
    ImGui::Text("CC 20: strobeSpeed");

    ImGui::Separator();
    ImGui::Text("Note Triggers:");
    ImGui::Text("Notes 36-48: Mode switching");
    ImGui::Text("Note 60: Random video change");
    ImGui::Text("Note 62: Toggle bloom");
    ImGui::Text("Note 64: Toggle glitch");
    ImGui::Text("Note 65: Toggle bend");
    ImGui::Text("Note 67: Toggle feedback");

    ImGui::End();
}

void UISystem::drawOscControls(OscSystem& oscSystem) {
    ImGui::Begin("OSC Controls", &showOscWindow);

    // Enable/Disable OSC
    bool enabled = oscSystem.isEnabled();
    if (ImGui::Checkbox("Enable OSC", &enabled)) {
        oscSystem.setEnabled(enabled);
    }

    ImGui::Separator();

    // Port display
    ImGui::Text("OSC Port: %d", oscSystem.getPort());
    ImGui::Text("Listening for OSC messages on UDP port");
    ImGui::Separator();
    std::string localIP = OscSystem::getLocalIPAddress();
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "IP Address: %s", localIP.c_str());
    ImGui::Text("Configure your OSC client with:");
    ImGui::Text("Host: %s", localIP.c_str());
    ImGui::Text("Port: %d", oscSystem.getPort());
    ImGui::Text("Protocol: UDP");

    ImGui::Separator();

    // OSC Learn Wizard
    ImGui::Text("OSC Learn Wizard");
    bool learnMode = oscSystem.isLearnMode();
    if (ImGui::Checkbox("Learn Mode", &learnMode)) {
        oscSystem.setLearnMode(learnMode);
    }

    if (oscSystem.isLearnMode()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Send an OSC message to learn its address...");
    }

    if (oscSystem.hasLearnedMessage()) {
        OscMessage msg = oscSystem.getLastLearnedMessage();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Detected OSC Message:");
        ImGui::Text("Address: %s", msg.address.c_str());

        if (msg.type == OscMessageType::FLOAT) {
            ImGui::Text("Type: Float");
            ImGui::Text("Value: %.4f", msg.floatValue);
        } else if (msg.type == OscMessageType::INT) {
            ImGui::Text("Type: Int");
            ImGui::Text("Value: %d", msg.intValue);
        } else if (msg.type == OscMessageType::STRING) {
            ImGui::Text("Type: String");
            ImGui::Text("Value: %s", msg.stringValue.c_str());
        }

        ImGui::Separator();
        ImGui::Text("Assign to Parameter:");

        static int selectedParamIndex = 0;
        selectedParamIndex = std::clamp(selectedParamIndex, 0, PARAMETER_COUNT - 1);

        const auto& displayPtrs = getParameterDisplayPtrs();
        ImGui::Combo("Parameter", &selectedParamIndex, displayPtrs.data(), static_cast<int>(displayPtrs.size()));

        static float minVal = 0.0f;
        static float maxVal = 1.0f;
        minVal = PARAMETER_INFOS[selectedParamIndex].minVal;
        maxVal = PARAMETER_INFOS[selectedParamIndex].maxVal;
        ImGui::DragFloat("Min", &minVal, 0.01f);
        ImGui::DragFloat("Max", &maxVal, 0.01f);

        static bool invert = false;
        ImGui::Checkbox("Invert", &invert);

        if (ImGui::Button("Assign Mapping")) {
            oscSystem.addMapping(msg.address, PARAMETER_INFOS[selectedParamIndex].name, minVal, maxVal, invert);
            std::cout << "[UI] Assigned OSC address " << msg.address << " to " << PARAMETER_INFOS[selectedParamIndex].name << std::endl;
            oscSystem.clearLearnedMessage();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            oscSystem.clearLearnedMessage();
        }
    }

    ImGui::Separator();

    // OSC mappings display
    ImGui::Text("Current Mappings:");
    const auto& mappings = oscSystem.getMappings();
    if (ImGui::BeginTable("OscMappings", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Max");
        ImGui::TableHeadersRow();

        for (const auto& [addr, mapping] : mappings) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", addr.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.parameterName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", mapping.minValue);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", mapping.maxValue);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("OSC Address Examples:");
    ImGui::Text("/vjay/animationSpeed");
    ImGui::Text("/vjay/bloomIntensity");
    ImGui::Text("/vjay/feedbackAmount");
    ImGui::Text("/vjay/rgbOverlay r g b (send 3 floats 0.0-1.0)");
    ImGui::Text("Example: /vjay/rgbOverlay 1.0 0.2 0.5");
    ImGui::Text("Remember to enable enableRgbOverlay toggle");
    ImGui::Text("Send float values 0.0-1.0 for normalized control");

    ImGui::Separator();
    ImGui::Text("OSC Triggers (Buttons):");
    const auto& triggerMappings = oscSystem.getTriggerMappings();
    if (ImGui::BeginTable("OscTriggers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove");
        ImGui::TableHeadersRow();

        std::vector<std::pair<std::string, OscTriggerMapping>> triggersCopy(triggerMappings.begin(), triggerMappings.end());
        for (const auto& [addr, mapping] : triggersCopy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", addr.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.actionName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string label = "Remove##" + addr;
            if (ImGui::Button(label.c_str())) {
                oscSystem.removeTriggerMapping(addr);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Add Trigger Mapping:");
    static char triggerAddress[256] = "/vjay/";
    ImGui::InputText("OSC Address", triggerAddress, IM_ARRAYSIZE(triggerAddress));

    static int selectedAction = 0;
    ImGui::Combo("Action", &selectedAction, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);

    if (ImGui::Button("Add Trigger")) {
        const char* actionName = TRIGGER_ACTIONS[selectedAction];
        oscSystem.addTriggerMapping(triggerAddress, actionName);
        std::cout << "[UI] Added OSC trigger " << triggerAddress << " -> " << actionName << std::endl;
    }

    ImGui::Separator();
    ImGui::Text("Trigger Examples:");
    ImGui::Text("/vjay/randomize (no arguments)");
    ImGui::Text("/vjay/randomize2 (no arguments)");
    ImGui::Text("/vjay/jump (no arguments)");
    ImGui::Text("Send messages without arguments for triggers");

    ImGui::End();
}

void UISystem::drawAudioDebug(AudioSystem& audioSystem) {
    ImGui::Begin("Audio Debug", &showAudioWindow);
    
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Audio Reactive Debug");
    ImGui::Separator();
    
    // Device Selection
    ImGui::Text("Input Device:");
    std::vector<std::string> deviceNames = audioSystem.getInputDeviceNames();
    int currentDeviceIndex = audioSystem.getInputDeviceIndex();
    
    // Create a filtered list of only devices with input channels
    std::vector<int> validDeviceIndices;
    std::vector<const char*> validDeviceNames;
    for (size_t i = 0; i < deviceNames.size(); i++) {
        if (!deviceNames[i].empty()) {
            validDeviceIndices.push_back(i);
            validDeviceNames.push_back(deviceNames[i].c_str());
        }
    }
    
    // Find current device index in the filtered list
    int currentFilteredIndex = 0;
    for (size_t i = 0; i < validDeviceIndices.size(); i++) {
        if (validDeviceIndices[i] == currentDeviceIndex) {
            currentFilteredIndex = i;
            break;
        }
    }
    
    if (ImGui::Combo("##Device", &currentFilteredIndex, validDeviceNames.data(), validDeviceNames.size())) {
        int newDeviceIndex = validDeviceIndices[currentFilteredIndex];
        audioSystem.setInputDevice(newDeviceIndex);
    }
    
    ImGui::Separator();
    
    // Status
    ImGui::Text("Audio Stream: %s", audioSystem.isRunning() ? "Running" : "Stopped");
    
    ImGui::Separator();
    
    // RMS Level
    float rms = audioSystem.getRMS();
    ImGui::Text("RMS Level: %.4f", rms);
    ImGui::ProgressBar(rms, ImVec2(200, 0));
    
    ImGui::Separator();
    
    // Raw Band Levels
    ImGui::Text("Raw Band Levels (Techno Optimized):");
    ImGui::Text("SubBass: %.4f", audioSystem.getSubBass());
    ImGui::Text("Kick:    %.4f", audioSystem.getKick());
    ImGui::Text("Bass:    %.4f", audioSystem.getBass());
    ImGui::Text("Mid:     %.4f", audioSystem.getMid());
    ImGui::Text("High:    %.4f", audioSystem.getHigh());
    
    ImGui::Separator();
    
    // Smoothed Band Levels
    ImGui::Text("Smoothed Band Levels:");
    ImGui::Text("SubBass: %.4f", audioSystem.getSmoothedSubBass());
    ImGui::Text("Kick:    %.4f", audioSystem.getSmoothedKick());
    ImGui::Text("Bass:    %.4f", audioSystem.getSmoothedBass());
    ImGui::Text("Mid:     %.4f", audioSystem.getSmoothedMid());
    ImGui::Text("High:    %.4f", audioSystem.getSmoothedHigh());
    
    ImGui::Separator();
    
    // FFT Visualization
    ImGui::Text("FFT Spectrum (256 bins):");
    const std::vector<float>& fftMagnitudes = audioSystem.getFFTMagnitudes();
    
    // Draw FFT bars
    float maxMagnitude = 0.0f;
    for (float mag : fftMagnitudes) {
        if (mag > maxMagnitude) maxMagnitude = mag;
    }
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float barWidth = 200.0f / fftMagnitudes.size();
    float maxHeight = 100.0f;
    
    for (size_t i = 0; i < fftMagnitudes.size(); i++) {
        float normalizedHeight = maxMagnitude > 0 ? (fftMagnitudes[i] / maxMagnitude) * maxHeight : 0;
        ImVec2 barMin(p.x + i * barWidth, p.y + maxHeight - normalizedHeight);
        ImVec2 barMax(p.x + (i + 1) * barWidth - 1, p.y + maxHeight);
        
        // Color gradient from bass (blue) to high (red)
        float t = static_cast<float>(i) / fftMagnitudes.size();
        ImColor color(t, 0.5f * (1.0f - t), 1.0f - t);
        drawList->AddRectFilled(barMin, barMax, color);
    }
    
    ImGui::Dummy(ImVec2(200, maxHeight + 5));
    
    ImGui::End();
}

void UISystem::drawParameterIndex() {
    ImGui::Begin("Parameter Index", &showParameterIndex);

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Complete Parameter Reference");
    ImGui::Separator();
    ImGui::Text("Use these parameter names for MIDI CC and OSC mapping");

    if (ImGui::BeginTabBar("ParameterTabs")) {
        if (ImGui::BeginTabItem("Procedural")) {
            ImGui::Text("animationSpeed - 0.0 to 3.0");
            ImGui::Text("tempo - 0.0 to 2.0");
            ImGui::Text("energy - 0.0 to 1.0");
            ImGui::Text("bass - 0.0 to 1.0");
            ImGui::Text("mid - 0.0 to 1.0");
            ImGui::Text("high - 0.0 to 1.0");
            ImGui::Text("colorBlend - 0.0 to 1.0");
            ImGui::Text("uvWarpStrength - 0.0 to 1.0");
            ImGui::Text("rippleStrength - 0.0 to 1.0");
            ImGui::Text("rippleFrequency - 0.0 to 5.0");
            ImGui::Text("swirlStrength - 0.0 to 1.0");
            ImGui::Text("displacementAmount - 0.0 to 1.0");
            ImGui::Text("kaleidoSegments - 1.0 to 12.0");
            ImGui::Text("tunnelDepth - 0.0 to 1.0");
            ImGui::Text("tunnelCurvature - 0.0 to 1.0");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Post FX")) {
            ImGui::Text("bloomIntensity - 0.0 to 1.0");
            ImGui::Text("bloomThreshold - 0.0 to 1.0");
            ImGui::Text("aberrationAmount - 0.0 to 0.1");
            ImGui::Text("grainStrength - 0.0 to 1.0");
            ImGui::Text("crtCurvature - 0.0 to 0.5");
            ImGui::Text("crtScanlineIntensity - 0.0 to 1.0");
            ImGui::Text("crtMaskIntensity - 0.0 to 1.0");
            ImGui::Text("crtVignette - 0.0 to 1.0");
            ImGui::Text("crtFishEye - 0.0 to 0.5");
            ImGui::Text("gaussianBlur - 0.0 to 1.0");
            ImGui::Text("directionalBlur - 0.0 to 1.0");
            ImGui::Text("directionalBlurAngle - 0.0 to 360.0");
            ImGui::Text("zoomBlur - 0.0 to 1.0");
            ImGui::Text("motionBlur - 0.0 to 1.0");
            ImGui::Text("temporalBlur - 0.0 to 1.0");
            ImGui::Text("unsharpMask - 0.0 to 1.0");
            ImGui::Text("casAmount - 0.0 to 1.0");
            ImGui::Text("localContrast - 0.0 to 1.0");
            ImGui::Text("pixelateAmount - 0.0 to 1.0");
            ImGui::Text("strobeSpeed - 0.0 to 20.0");
            ImGui::Text("thresholdLevel - 0.0 to 1.0");
            ImGui::Text("slowZoomAmount - 0.0 to 1.0");
            ImGui::Text("edgeStrength - 0.1 to 5.0");
            ImGui::Text("edgeThreshold - 0.0 to 1.0");
            ImGui::Text("edgeBlend - 0.0 to 1.0");
            ImGui::Text("fxaaQualitySubpix - 0.0 to 1.0");
            ImGui::Text("fxaaQualityEdgeThreshold - 0.0 to 1.0");
            ImGui::Text("fxaaQualityEdgeThresholdMin - 0.0 to 1.0");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("VJay Basics")) {
            ImGui::Text("videoPlaybackRate - 0.0 to 2.0");
            ImGui::Text("videoDecodeOversample - 1.0 to 4.0");
            ImGui::Text("videoMix - 0.0 to 1.0");
            ImGui::Text("grayscaleAmount - 0.0 to 1.0");
            ImGui::Text("sharpenAmount - 0.0 to 1.0");
            ImGui::Text("gradeBrightness - 0.0 to 2.0");
            ImGui::Text("gradeContrast - 0.0 to 2.0");
            ImGui::Text("gradeSaturation - 0.0 to 2.0");
            ImGui::Text("gradeHueShift - 0.0 to 360.0");
            ImGui::Text("gradeGamma - 0.0 to 2.0");
            ImGui::Text("colorLUTIndex - 0.0 to 10.0");
            ImGui::Text("splitToneBalance - 0.0 to 1.0");
            ImGui::Text("blendProceduralMix - 0.0 to 1.0");
            ImGui::Text("blendVideoMix - 0.0 to 1.0");
            ImGui::Text("blendFeedbackMix - 0.0 to 1.0");
            ImGui::Text("video2BlendMode - 0 a 4 (entero)");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("VJay Extra")) {
            ImGui::Text("feedbackAmount - 0.0 to 1.0");
            ImGui::Text("trailStrength - 0.0 to 1.0");
            ImGui::Text("temporalAccumulation - 0.0 to 1.0");
            ImGui::Text("feedbackDecay - 0.0 to 1.0");
            ImGui::Text("recursiveBlend - 0.0 to 1.0");
            ImGui::Text("glitchAmount - 0.0 to 1.0");
            ImGui::Text("glitchDatamosh - 0.0 to 1.0");
            ImGui::Text("glitchRGBSplit - 0.0 to 1.0");
            ImGui::Text("glitchScanlineBreak - 0.0 to 1.0");
            ImGui::Text("glitchJitter - 0.0 to 1.0");
            ImGui::Text("glitchTearing - 0.0 to 1.0");
            ImGui::Text("glitchPixelSort - 0.0 to 1.0");
            ImGui::Text("glitchBufferCorruption - 0.0 to 1.0");
            ImGui::Text("analogScanlineFocus - 0.0 to 1.0");
            ImGui::Text("analogMaskBalance - 0.0 to 1.0");
            ImGui::Text("analogNoise - 0.0 to 1.0");
            ImGui::Text("analogBloom - 0.0 to 1.0");
            ImGui::Text("vhsDistortion - 0.0 to 1.0");
            ImGui::Text("analogChromaticAberration - 0.0 to 1.0");
            ImGui::Text("mirrorAmount - 0.0 to 1.0");
            ImGui::Text("posterizeLevels - 0.0 to 16.0");
            ImGui::Text("zoomPulseAmount - 0.0 to 1.0");
            ImGui::Text("rgbShiftAmount - 0.0 to 1.0");
            ImGui::Text("audioWarpResponse - 0.0 to 1.0");
            ImGui::Text("audioFeedbackResponse - 0.0 to 1.0");
            ImGui::Text("audioBlurResponse - 0.0 to 1.0");
            ImGui::Text("audioColorResponse - 0.0 to 1.0");
            ImGui::Text("audioGlitchResponse - 0.0 to 1.0");
            ImGui::Text("audioBeatSync - 0.0 to 1.0");
            ImGui::Text("audioLfoRate - 0.0 to 10.0");
            ImGui::Text("temporalInterpolation - 0.0 to 1.0");
            ImGui::Text("temporalBlendStrength - 0.0 to 1.0");
            ImGui::Text("slowMotionFactor - 0.0 to 1.0");
            ImGui::Text("frameAccumulation - 0.0 to 1.0");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Triggers & RGB")) {
            ImGui::Text("Trigger actions disponibles (OSC / MIDI notes):");
            for (int i = 0; i < TRIGGER_ACTION_COUNT; ++i) {
                ImGui::BulletText("%s", TRIGGER_ACTIONS[i]);
            }

            ImGui::Separator();
            ImGui::Text("Par\xC3\xA1metros RGB overlay:");
            ImGui::BulletText("enableRgbOverlay - 0.0 a 1.0");
            ImGui::BulletText("rgbOverlay (vec3) - 0.0 a 2.0");
            ImGui::BulletText("rgbOverlayR / rgbOverlayG / rgbOverlayB");
            ImGui::BulletText("OSC multi-float: /vjay/rgbOverlay R G B (floats 0-1)");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Trigger Actions")) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "OSC Triggers (send message without arguments)");
            ImGui::Text("randomizeVideo - Randomize current video");
            ImGui::Text("randomizeVideo2 - Randomize second video source (Video 2)");
            ImGui::Text("jumpRandom - Random jump within current video");
            ImGui::Text("folderChanged - Reload video folder");
            ImGui::Text("applyChanges - Apply render changes");
            
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "MIDI Note Triggers");
            ImGui::Text("Notes 36-48 - Mode switching");
            ImGui::Text("Note 60 - Random video change");
            ImGui::Text("Note 62 - Toggle bloom");
            ImGui::Text("Note 64 - Toggle glitch");
            ImGui::Text("Note 65 - Toggle bend");
            ImGui::Text("Note 67 - Toggle feedback");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ============================================================
// Main Navbar with Tabs
// ============================================================

void UISystem::drawMainNavbar(
    VisualControls&       controls,
    VideoRandomizerState& randomizer,
    VideoRandomizerState2& randomizer2,
    VideoPlayer&          player,
    VideoPlayer&          player2,
    VideoRegistry&        registry,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2,
    float&                transitionDuration,
    float&                transitionDuration2,
    bool&                 allowDimensionChangeRecreation,
    bool&                 controlsDirty,
    std::mt19937&         rng,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks,
    MidiSystem&           midiSystem,
    OscSystem&            oscSystem,
    AudioSystem&          audioSystem,
    const std::string&    video1Path,
    const std::string&    video2Path
) {
    // Main window with navbar tabs - fullscreen responsive
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | 
                                   ImGuiWindowFlags_NoTitleBar | 
                                   ImGuiWindowFlags_NoResize | 
                                   ImGuiWindowFlags_NoMove | 
                                   ImGuiWindowFlags_NoCollapse | 
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    if (!ImGui::Begin("VulkanCpp Controls", nullptr, windowFlags)) {
        ImGui::End();
        return;
    }
    
    // Create tab bar as navbar
    if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_None)) {
        // Tab 1: Procedural
        if (ImGui::BeginTabItem("Procedural")) {
            drawProceduralControlsContent(controls, randomizer, player, registry,
                                         selectedVideoAsset, selectedVideoAsset2, transitionDuration,
                                         allowDimensionChangeRecreation, controlsDirty,
                                         rng, diag, callbacks);
            ImGui::EndTabItem();
        }

        // Tab 2: Video
        if (ImGui::BeginTabItem("Video")) {
            drawVideoContent(controls, randomizer, randomizer2, registry,
                            selectedVideoAsset, selectedVideoAsset2, transitionDuration, transitionDuration2,
                            allowDimensionChangeRecreation, controlsDirty,
                            diag, callbacks);
            ImGui::EndTabItem();
        }

        // Tab 3: Post FX
        if (ImGui::BeginTabItem("Post FX")) {
            drawPostFxContent(controls, controlsDirty, rng);
            ImGui::EndTabItem();
        }

        // Tab 4: VJAY Basics
        if (ImGui::BeginTabItem("VJAY Basics")) {
            drawVJayBasicsContent(controls, controlsDirty, rng);
            ImGui::EndTabItem();
        }
        
        // Tab 5: VJAY Extra
        if (ImGui::BeginTabItem("VJAY Extra")) {
            drawVJayExtraContent(controls, controlsDirty, rng);
            ImGui::EndTabItem();
        }
        
        // Tab 6: NLE Editor
        if (ImGui::BeginTabItem("NLE Editor")) {
            drawNLEEditorContent(callbacks, video1Path, video2Path);
            ImGui::EndTabItem();
        }
        
        // Tab 7: Diagnostics
        if (ImGui::BeginTabItem("Diagnostics")) {
            drawDiagnosticsContent(diag, player, player2, registry, selectedVideoAsset, selectedVideoAsset2, controls, callbacks);
            ImGui::EndTabItem();
        }
        
        // Tab 8: MIDI
        if (ImGui::BeginTabItem("MIDI")) {
            drawMidiControlsContent(midiSystem);
            ImGui::EndTabItem();
        }
        
        // Tab 9: OSC
        if (ImGui::BeginTabItem("OSC")) {
            drawOscControlsContent(oscSystem);
            ImGui::EndTabItem();
        }
        
        // Tab 10: Audio
        if (ImGui::BeginTabItem("Audio")) {
            drawAudioDebugContent(audioSystem, controls);
            ImGui::EndTabItem();
        }
        
        // Tab 11: Parameters
        if (ImGui::BeginTabItem("Parameters")) {
            drawParameterIndexContent();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

// ============================================================
// Content functions for tabs (call existing window functions)
// ============================================================

void UISystem::drawProceduralControlsContent(
    VisualControls&       controls,
    VideoRandomizerState& randomizer,
    VideoPlayer&          player,
    VideoRegistry&        registry,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2,
    float&                transitionDuration,
    bool&                 allowDimensionChangeRecreation,
    bool&                 controlsDirty,
    std::mt19937&         rng,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks
) {
    bool changed = false;

    ImGui::Text("Animation");
    changed |= ImGui::SliderFloat("Speed", &controls.playback.animationSpeed, 0.1f, 8.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Target change (s)", &controls.playback.animationTargetSeconds, 0.1f, 5.0f, "%.2fs");
    ImGui::SameLine();
    if (ImGui::Button("Snap 1s")) {
        controls.playback.animationTargetSeconds = 1.0f;
        controls.playback.animationSpeed = 1.0f;
        changed = true;
    }

    ImGui::Separator();
    ImGui::Text("Layers");
    changed |= drawActiveLayerCombo("Active Layer", controls.playback.activeMode);

    ImGui::Separator();
    ImGui::Text("Color Palette");
    changed |= ImGui::ColorEdit4("Primary",   glm::value_ptr(controls.color.primaryColor));
    changed |= ImGui::ColorEdit4("Secondary", glm::value_ptr(controls.color.secondaryColor));
    changed |= ImGui::SliderFloat("Blend", &controls.color.colorBlend, 0.0f, 1.0f);
    
    if (ImGui::Button("Randomize Colors")) {
        std::uniform_real_distribution<float> hueDist(0.0f, 360.0f);
        std::uniform_real_distribution<float> satDist(0.6f, 1.0f);
        std::uniform_real_distribution<float> valDist(0.7f, 1.0f);
        
        float primaryHue = hueDist(rng);
        float primarySat = satDist(rng);
        float primaryVal = valDist(rng);
        
        float secondaryHue = fmod(primaryHue + 180.0f, 360.0f);
        float secondarySat = primarySat;
        float secondaryVal = primaryVal;
        
        auto hsvToRgb = [](float h, float s, float v) -> glm::vec3 {
            float c = v * s;
            float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
            float m = v - c;
            
            float r, g, b;
            if (h < 60.0f) { r = c; g = x; b = 0; }
            else if (h < 120.0f) { r = x; g = c; b = 0; }
            else if (h < 180.0f) { r = 0; g = c; b = x; }
            else if (h < 240.0f) { r = 0; g = x; b = c; }
            else if (h < 300.0f) { r = x; g = 0; b = c; }
            else { r = c; g = 0; b = x; }
            
            return glm::vec3(r + m, g + m, b + m);
        };
        
        controls.color.primaryColorTarget = glm::vec4(hsvToRgb(primaryHue, primarySat, primaryVal), 1.0f);
        controls.color.secondaryColorTarget = glm::vec4(hsvToRgb(secondaryHue, secondarySat, secondaryVal), 1.0f);
        controls.color.primaryColor = controls.color.primaryColorTarget;
        controls.color.secondaryColor = controls.color.secondaryColorTarget;
        changed = true;
        controlsDirty = true;
    }
    
    changed |= ImGui::Checkbox("Auto Randomize", &controls.color.autoRandomizeColors);
    if (controls.color.autoRandomizeColors) {
        changed |= ImGui::SliderFloat("Interval (s)", &controls.color.colorRandomizeInterval, 0.1f, 5.0f, "%.1fs");
    }

    if (changed && callbacks.onControlsChanged) callbacks.onControlsChanged();
}

void UISystem::drawVideoContent(
    VisualControls&       controls,
    VideoRandomizerState& randomizer,
    VideoRandomizerState2& randomizer2,
    VideoRegistry&        registry,
    int&                  selectedVideoAsset,
    int&                  selectedVideoAsset2,
    float&                transitionDuration,
    float&                transitionDuration2,
    bool&                 allowDimensionChangeRecreation,
    bool&                 controlsDirty,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks
) {
    bool changed = false;

    ImGui::Separator();
    ImGui::Text("Video Tweaks (Global)");
    ImGui::Separator();
    changed |= ImGui::Checkbox("Enable dual video", &controls.playback.enableDualVideo);
    changed |= ImGui::SliderFloat("Grayscale", &controls.playback.grayscaleAmount, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sharpen",   &controls.playback.sharpenAmount,   0.0f, 1.0f);
    changed |= ImGui::Checkbox("Bicubic Upscale", &controls.playback.upscaleEnabled);
    changed |= ImGui::Checkbox("Auto Scale Video", &controls.playback.autoScaleVideo);
    
    if (ImGui::SliderFloat("Decode oversample", &controls.playback.videoDecodeOversample, 1.0f, 8.0f, "%.1fx")) {
        controls.playback.videoDecodeOversample = std::clamp(controls.playback.videoDecodeOversample, 1.0f, 8.0f);
        changed = true;
    }

    static const char* forceFpsLabels[] = {"Off", "15 fps", "24 fps", "30 fps", "60 fps"};
    int forceIdx = std::clamp(controls.playback.forcedFpsIndex, 0,
                              static_cast<int>(FORCED_FPS_OPTIONS_UI.size()) - 1);
    if (forceIdx != controls.playback.forcedFpsIndex) { controls.playback.forcedFpsIndex = forceIdx; changed = true; }
    if (ImGui::Combo("Force FPS", &forceIdx, forceFpsLabels, IM_ARRAYSIZE(forceFpsLabels))) {
        controls.playback.forcedFpsIndex = forceIdx;
        changed = true;
    }

    changed |= ImGui::SliderFloat("Loop crossfade (s)", &controls.playback.loopBlendSeconds, 0.0f, 2.0f, "%.2f s");
    changed |= ImGui::Checkbox("Allow dimension change recreation", &allowDimensionChangeRecreation);

    ImGui::Separator();
    ImGui::Text("Video 1 Randomization");
    changed |= ImGui::Checkbox("Random Start Video 1", &controls.playback.randomVideoStart);
    if (controls.playback.randomVideoStart) {
        ImGui::SameLine();
        if (ImGui::Button("Jump Random") && callbacks.onJumpRandom) {
            callbacks.onJumpRandom();
        }
    }

    ImGui::Indent();
    changed |= ImGui::Checkbox("Auto jump interval", &controls.playback.enableRandomJumpInterval);
    if (controls.playback.enableRandomJumpInterval) {
        changed |= ImGui::SliderFloat("Interval (s)", &controls.playback.randomJumpInterval, 1.0f, 60.0f, "%.1f s");
    }
    ImGui::Unindent();

    ImGui::Separator();
    ImGui::Text("Video 1");
    ImGui::Separator();
    ImGui::Text("Playback");
    changed |= ImGui::SliderFloat("Video Mix",   &controls.playback.videoMix,         0.0f, 1.0f);
    ImGui::Indent();
    ImGui::TextDisabled("Blend vs. procedural");
    changed |= ImGui::Combo("##VideoProceduralBlend", &controls.blending.blendModeProcedural, BLEND_ITEMS);
    changed |= ImGui::SliderFloat("Procedural Mix##Video", &controls.blending.blendProceduralMix, 0.0f, 2.0f, "%.2f");
    ImGui::Unindent();
    changed |= ImGui::SliderFloat("Video speed", &controls.playback.videoPlaybackRate, 0.1f, 5.0f, "%.2fx");

    ImGui::Separator();
    ImGui::Text("Selection");
    ImGui::TextWrapped("Video %s", diag.videoReady ? "online" : "unavailable");

    static std::vector<std::string> availableFolders;
    static bool foldersScanned = false;
    if (!foldersScanned) {
        availableFolders.clear();
        availableFolders.push_back("All Folders");
        try {
            fs::path mp4sPath("mp4s");
            if (fs::exists(mp4sPath) && fs::is_directory(mp4sPath)) {
                for (const auto& entry : fs::directory_iterator(mp4sPath)) {
                    if (entry.is_directory()) {
                        availableFolders.push_back(entry.path().filename().string());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[UISystem] Error scanning mp4s folder: " << e.what() << std::endl;
        }
        foldersScanned = true;
    }

    static std::vector<const char*> folderItems;
    folderItems.clear();
    for (auto& folder : availableFolders) {
        folderItems.push_back(folder.c_str());
    }

    int currentFolderIndex = 0;
    if (!controls.playback.selectedVideoFolder.empty()) {
        for (size_t i = 0; i < availableFolders.size(); ++i) {
            if (availableFolders[i] == controls.playback.selectedVideoFolder) {
                currentFolderIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (ImGui::Combo("Load Folder##V1", &currentFolderIndex, folderItems.data(), static_cast<int>(folderItems.size()))) {
        std::string newFolder = (currentFolderIndex == 0) ? "" : availableFolders[currentFolderIndex];
        if (controls.playback.selectedVideoFolder != newFolder) {
            controls.playback.selectedVideoFolder = newFolder;
            if (callbacks.onFolderChanged) {
                callbacks.onFolderChanged();
            }
        }
    }

    if (controls.playback.selectedVideoFolder.empty()) {
        ImGui::Text("Current loaded folder: All Folders");
    } else {
        ImGui::Text("Current loaded folder: %s", controls.playback.selectedVideoFolder.c_str());
    }

    const auto& assets = registry.getFilteredAssets(controls.playback.selectedVideoFolder);
    if (assets.empty()) {
        ImGui::TextDisabled("No videos found in this folder");
    } else {
        if (selectedVideoAsset < 0 || selectedVideoAsset >= static_cast<int>(assets.size()))
            selectedVideoAsset = 0;

        const std::string& currentLabel = assets[selectedVideoAsset].metadata.filename;
        if (ImGui::BeginCombo("Video Asset##V1", currentLabel.c_str())) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                bool isSelected = (i == selectedVideoAsset);
                std::string label = assets[i].metadata.filename;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    if (selectedVideoAsset != i) {
                        selectedVideoAsset = i;
                        if (callbacks.onReloadVideo)
                            callbacks.onReloadVideo(assets[i].metadata.path);
                    }
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Videos in folder: %zu", assets.size());
    }

    ImGui::Separator();
    ImGui::Text("Video Randomizer (Auto-shuffle)");
    const bool hasRandomChoices = assets.size() > 1;
    ImGui::BeginDisabled(randomizer.useVideoDuration);
    if (ImGui::SliderFloat("Random interval (s)", &randomizer.intervalSeconds, 1.0f, 300.0f, "%.0f s")) {
        randomizer.intervalSeconds = std::clamp(randomizer.intervalSeconds, 1.0f, 600.0f);
        randomizer.elapsedSeconds  = 0.0f;
        changed = true;
    }
    ImGui::EndDisabled();

    bool syncToggle = ImGui::Checkbox("Sync shuffle to clip duration", &randomizer.useVideoDuration);
    if (syncToggle) randomizer.elapsedSeconds = 0.0f;
    changed |= syncToggle;

    if (randomizer.useVideoDuration)
        ImGui::Text("Current clip: %.1f s", std::max(0.0f, randomizer.currentVideoDuration));

    changed |= ImGui::SliderFloat("Transition duration (s)", &transitionDuration, 0.1f, 2.0f, "%.2f s");

    ImGui::BeginDisabled(!hasRandomChoices);
    if (ImGui::Button("Randomize video online") && callbacks.onRandomizeVideo) {
        callbacks.onRandomizeVideo();
        changed = true;
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Auto randomize", &randomizer.autoRandomize);
    if (!hasRandomChoices) {
        ImGui::SameLine();
        ImGui::TextDisabled("Need at least 2 videos");
    } else if (randomizer.autoRandomize) {
        float target = (randomizer.useVideoDuration && randomizer.currentVideoDuration > 0.0f)
                           ? randomizer.currentVideoDuration
                           : randomizer.intervalSeconds;
        ImGui::Text("Next shuffle in %.1f s", std::max(0.0f, target - randomizer.elapsedSeconds));
    }

    ImGui::Indent();
    changed |= ImGui::Checkbox("Shuffle all videos", &randomizer.useShuffleMode);
    if (randomizer.useShuffleMode && !randomizer.shuffleQueue.empty()) {
        ImGui::Text("Playing %d/%d", randomizer.currentShuffleIndex + 1, static_cast<int>(randomizer.shuffleQueue.size()));
    }
    ImGui::Unindent();
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Video 2");
    ImGui::Separator();
    ImGui::Text("Playback");
    ImGui::BeginDisabled(!controls.playback.enableDualVideo);
    changed |= ImGui::SliderFloat("Video 2 Mix", &controls.playback.video2Mix, 0.0f, 1.0f);
    static const char* blendLabels = "Mix\0Add\0Multiply\0Screen\0Difference\0";
    changed |= ImGui::Combo("Blend Mode", &controls.playback.video2BlendMode, blendLabels, 5);
    changed |= ImGui::SliderFloat("Video 2 speed", &controls.playback.video2PlaybackRate, 0.1f, 5.0f, "%.2fx");
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Randomization");
    changed |= ImGui::Checkbox("Random Start Video 2", &controls.playback.randomVideo2Start);
    if (controls.playback.randomVideo2Start) {
        ImGui::SameLine();
        if (ImGui::Button("Jump Random##V2") && callbacks.onJumpRandom) {
            callbacks.onJumpRandom();
        }
    }

    ImGui::Indent();
    changed |= ImGui::Checkbox("Auto jump interval", &controls.playback.enableRandomJumpInterval2);
    if (controls.playback.enableRandomJumpInterval2) {
        changed |= ImGui::SliderFloat("Interval (s)", &controls.playback.randomJumpInterval2, 1.0f, 60.0f, "%.1f s");
    }
    ImGui::Unindent();

    ImGui::Separator();
    ImGui::Text("Selection");

    int currentFolder2Index = 0;
    if (!controls.playback.selectedVideo2Folder.empty()) {
        for (size_t i = 0; i < availableFolders.size(); ++i) {
            if (availableFolders[i] == controls.playback.selectedVideo2Folder) {
                currentFolder2Index = static_cast<int>(i);
                break;
            }
        }
    }

    if (ImGui::Combo("Load Folder##V2", &currentFolder2Index, folderItems.data(), static_cast<int>(folderItems.size()))) {
        std::string newFolder = (currentFolder2Index == 0) ? "" : availableFolders[currentFolder2Index];
        if (controls.playback.selectedVideo2Folder != newFolder) {
            controls.playback.selectedVideo2Folder = newFolder;
            if (callbacks.onFolderChanged2) {
                callbacks.onFolderChanged2();
            }
        }
    }

    if (controls.playback.selectedVideo2Folder.empty()) {
        ImGui::Text("Current loaded folder: All Folders");
    } else {
        ImGui::Text("Current loaded folder: %s", controls.playback.selectedVideo2Folder.c_str());
    }

    const auto& assets2 = registry.getFilteredAssets(controls.playback.selectedVideo2Folder);
    if (assets2.empty()) {
        ImGui::TextDisabled("No videos found in this folder");
    } else {
        if (selectedVideoAsset2 < 0 || selectedVideoAsset2 >= static_cast<int>(assets2.size()))
            selectedVideoAsset2 = 0;

        const std::string& currentLabel2 = assets2[selectedVideoAsset2].metadata.filename;
        if (ImGui::BeginCombo("Video Asset##V2", currentLabel2.c_str())) {
            for (int i = 0; i < static_cast<int>(assets2.size()); ++i) {
                bool isSelected = (i == selectedVideoAsset2);
                std::string label = assets2[i].metadata.filename;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    if (selectedVideoAsset2 != i) {
                        selectedVideoAsset2 = i;
                        if (callbacks.onReloadVideo2)
                            callbacks.onReloadVideo2(assets2[i].metadata.path);
                    }
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Videos in folder: %zu", assets2.size());
        if (assets2.size() > 1 && ImGui::Button("Randomize video 2 online##Selection") && callbacks.onRandomizeVideo2) {
            callbacks.onRandomizeVideo2();
            changed = true;
        }
    }

    ImGui::Separator();
    ImGui::Text("Video 2 Randomizer (Auto-shuffle)");
    const bool hasRandomChoices2 = assets2.size() > 1;
    ImGui::BeginDisabled(randomizer2.useVideoDuration);
    if (ImGui::SliderFloat("Random interval (s)##V2", &randomizer2.intervalSeconds, 1.0f, 300.0f, "%.0f s")) {
        randomizer2.intervalSeconds = std::clamp(randomizer2.intervalSeconds, 1.0f, 600.0f);
        randomizer2.elapsedSeconds = 0.0f;
        changed = true;
    }
    ImGui::EndDisabled();

    bool syncToggle2 = ImGui::Checkbox("Sync shuffle to clip duration##V2", &randomizer2.useVideoDuration);
    if (syncToggle2) randomizer2.elapsedSeconds = 0.0f;
    changed |= syncToggle2;

    if (randomizer2.useVideoDuration)
        ImGui::Text("Current clip: %.1f s", std::max(0.0f, randomizer2.currentVideoDuration));

    changed |= ImGui::SliderFloat("Transition duration (s)##V2", &transitionDuration2, 0.1f, 2.0f, "%.2f s");

    ImGui::BeginDisabled(!hasRandomChoices2);
    if (ImGui::Button("Randomize video 2 online##Randomizer") && callbacks.onRandomizeVideo2) {
        callbacks.onRandomizeVideo2();
        changed = true;
    }
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Auto randomize##V2", &randomizer2.autoRandomize);
    if (!hasRandomChoices2) {
        ImGui::SameLine();
        ImGui::TextDisabled("Need at least 2 videos");
    } else if (randomizer2.autoRandomize) {
        float target = (randomizer2.useVideoDuration && randomizer2.currentVideoDuration > 0.0f)
                           ? randomizer2.currentVideoDuration
                           : randomizer2.intervalSeconds;
        ImGui::Text("Next shuffle in %.1f s", std::max(0.0f, target - randomizer2.elapsedSeconds));
    }

    ImGui::Indent();
    changed |= ImGui::Checkbox("Shuffle all videos##V2", &randomizer2.useShuffleMode);
    if (randomizer2.useShuffleMode && !randomizer2.shuffleQueue.empty()) {
        ImGui::Text("Playing %d/%d", randomizer2.currentShuffleIndex + 1,
                    static_cast<int>(randomizer2.shuffleQueue.size()));
    }
    ImGui::Unindent();
    ImGui::EndDisabled();

    if (changed && callbacks.onControlsChanged) callbacks.onControlsChanged();
}

void UISystem::drawPostFxContent(
    VisualControls& controls,
    bool&           controlsDirty,
    std::mt19937&   rng
) {
    bool changed = false;

    ImGui::Separator();
    ImGui::Text("Post FX");

    auto setPostFxEnabled = [&](bool enabled) {
        controls.post.enablePostCrtCurvature  = enabled;
        controls.post.enablePostScanMask      = enabled;
        controls.post.enablePostVignette      = enabled;
        controls.post.enablePostFishEye       = enabled;
        controls.post.enablePostBloom         = enabled;
        controls.post.enablePostAberration    = enabled;
        controls.post.enablePostGrain         = enabled;
        controls.post.enablePostBend          = enabled;
        controls.post.enablePostGlitch        = enabled;
        controls.post.enablePostColorBalance  = enabled;
    };

    if (ImGui::Button("Randomize Post FX")) {
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        auto rr = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        if (controls.post.enablePostCrtCurvature)  { controls.post.crtCurvature = rr(0,0.6f); controls.post.crtHorizontalCurvature = rr(0,0.6f); }
        if (controls.post.enablePostScanMask)       { controls.post.crtScanlineIntensity = u01(rng); controls.post.crtMaskIntensity = u01(rng); }
        if (controls.post.enablePostVignette)       { controls.post.crtVignette = u01(rng); }
        if (controls.post.enablePostFishEye)        { controls.post.crtFishEye = rr(-3,3); }
        if (controls.post.enablePostBloom)          { controls.post.bloomIntensity = rr(0,2); controls.post.bloomThreshold = u01(rng); }
        if (controls.post.enablePostAberration)     { controls.post.aberrationAmount = rr(-0.05f,0.05f); }
        if (controls.post.enablePostGrain)          { controls.post.grainStrength = rr(0,0.5f); }
        if (controls.post.enablePostBend)           { controls.fx.bendAmount = u01(rng); }
        if (controls.post.enablePostGlitch)         { controls.fx.glitchAmount = u01(rng); }
        if (controls.post.enablePostColorBalance)   { controls.color.colorBalance = glm::vec3(rr(0,2), rr(0,2), rr(0,2)); }
        controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Post FX")) {
        setPostFxEnabled(false);
        controls.post.crtCurvature           = 0.15f;
        controls.post.crtHorizontalCurvature = 0.15f;
        controls.post.crtScanlineIntensity   = 0.35f;
        controls.post.crtMaskIntensity       = 0.35f;
        controls.post.crtVignette            = 0.55f;
        controls.post.crtFishEye             = 0.0f;
        controls.post.bloomIntensity         = 0.45f;
        controls.post.bloomThreshold         = 0.7f;
        controls.post.aberrationAmount       = 0.02f;
        controls.post.grainStrength          = 0.15f;
        controls.fx.bendAmount             = 0.0f;
        controls.fx.glitchAmount           = 0.0f;
        controls.color.colorBalance           = glm::vec3(1.0f);
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Enable All"))  { setPostFxEnabled(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) { setPostFxEnabled(false); changed = true; controlsDirty = true; }

    ImGui::Separator();

    // Macros para no repetir el patron checkbox+slider
#define POST_FX_SECTION(label, toggleExpr, ...) \
    { \
        auto& postToggle__ = (toggleExpr); \
        changed |= ImGui::Checkbox(label, &postToggle__); \
        ImGui::BeginDisabled(!postToggle__); \
        __VA_ARGS__ \
        ImGui::EndDisabled(); \
    }

    POST_FX_SECTION("CRT Curvature", controls.post.enablePostCrtCurvature,
        changed |= ImGui::SliderFloat("CRT Curvature V", &controls.post.crtCurvature, 0.0f, 0.6f, "%.2f");
        changed |= ImGui::SliderFloat("CRT Curvature H", &controls.post.crtHorizontalCurvature, 0.0f, 0.6f, "%.2f");
    )
    POST_FX_SECTION("Scanlines / Mask", controls.post.enablePostScanMask,
        changed |= ImGui::SliderFloat("CRT Scanlines", &controls.post.crtScanlineIntensity, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("CRT Mask",      &controls.post.crtMaskIntensity,     0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Vignette", controls.post.enablePostVignette,
        changed |= ImGui::SliderFloat("CRT Black Bars", &controls.post.crtVignette, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Fish-eye", controls.post.enablePostFishEye,
        changed |= ImGui::SliderFloat("CRT Fish-eye", &controls.post.crtFishEye, -3.0f, 3.0f, "%.2f");
    )
    POST_FX_SECTION("Bloom", controls.post.enablePostBloom,
        changed |= ImGui::SliderFloat("Bloom Intensity", &controls.post.bloomIntensity, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Threshold", &controls.post.bloomThreshold, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Aberration##Toggle", controls.post.enablePostAberration,
        changed |= ImGui::SliderFloat("Aberration", &controls.post.aberrationAmount, -0.05f, 0.05f, "%.3f");
    )
    POST_FX_SECTION("Film Grain##Toggle", controls.post.enablePostGrain,
        changed |= ImGui::SliderFloat("Film Grain", &controls.post.grainStrength, 0.0f, 0.5f, "%.2f");
    )
    POST_FX_SECTION("Screen Bend", controls.post.enablePostBend,
        changed |= ImGui::SliderFloat("Bend Amount", &controls.fx.bendAmount, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Glitch wrapper", controls.post.enablePostGlitch,
        changed |= ImGui::SliderFloat("Glitch Intensity", &controls.fx.glitchAmount, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("RGB Mix##Toggle", controls.post.enablePostColorBalance,
        changed |= ImGui::SliderFloat3("RGB Mix", glm::value_ptr(controls.color.colorBalance), 0.0f, 2.0f);
    )
#undef POST_FX_SECTION

    if (changed) {
        controlsDirty = true;
    }
}

void UISystem::drawVJayBasicsContent(
    VisualControls& controls,
    bool&           controlsDirty,
    std::mt19937&   rng
) {
    bool changed = false;
    static const char* LUT_ITEMS   = "Off\0Filmic\0Neon\0Noir\0Heatmap\0Analog CRT\0";
    static const char* BLEND_ITEMS = "Add\0Screen\0Multiply\0Overlay\0Difference\0Soft Light\0";

    auto setAllToggles = [&](bool v) {
        controls.color.enableColorGrading  = v; controls.temporal.enableFeedback    = v; controls.fx.enableDistortion = v;
        controls.fx.enableBlurMotion    = v; controls.fx.enableSharpen     = v; controls.fx.enableGlitch     = v;
        controls.blending.enableBlending      = v; controls.post.enableAnalog      = v; controls.system.enableAudioReactive = v;
        controls.temporal.enableTemporal      = v;
    };

    if (ImGui::Button("Randomize VJAY basics")) {
        randomizeVJayBasicsControls(controls, rng);
        changed = true; controlsDirty = true;
    }

    if (ImGui::Button("Turn all ON"))  { setAllToggles(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Turn all OFF")) { setAllToggles(false); changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Reset VJAY basics")) {
        setAllToggles(false);
        controls.color.gradeBrightness = 0; controls.color.gradeContrast = 1; controls.color.gradeSaturation = 1;
        controls.color.gradeHueShift = 0; controls.color.gradeGamma = 1; controls.color.colorLUTIndex = 0;
        controls.color.splitToneBalance = 0.5f;
        controls.color.splitToneShadows = glm::vec3(0); controls.color.splitToneHighlights = glm::vec3(1);
        controls.temporal.feedbackAmount = 0; controls.temporal.trailStrength = 0; controls.temporal.temporalAccumulation = 0;
        controls.temporal.feedbackDecay = 0; controls.temporal.recursiveBlend = 0;
        controls.fx.uvWarpStrength = 0; controls.fx.rippleStrength = 0; controls.fx.rippleFrequency = 1;
        controls.fx.swirlStrength = 0; controls.fx.displacementAmount = 0; controls.fx.kaleidoSegments = 6;
        controls.fx.tunnelDepth = 0; controls.fx.tunnelCurvature = 0;
        controls.fx.gaussianBlur = 0; controls.fx.directionalBlur = 0; controls.fx.directionalBlurAngle = 0;
        controls.fx.zoomBlur = 0; controls.fx.motionBlur = 0; controls.fx.temporalBlur = 0;
        controls.fx.unsharpMask = 0; controls.fx.casAmount = 0; controls.fx.localContrast = 0;
        controls.fx.glitchDatamosh = 0; controls.fx.glitchRGBSplit = 0; controls.fx.glitchScanlineBreak = 0;
        controls.fx.glitchJitter = 0; controls.fx.glitchTearing = 0; controls.fx.glitchPixelSort = 0; controls.fx.glitchBufferCorruption = 0;
        controls.blending.blendModeProcedural = 0; controls.blending.blendModeVideo = 1; controls.blending.blendModeFeedback = 2;
        controls.blending.blendProceduralMix = 1; controls.blending.blendVideoMix = 1; controls.blending.blendFeedbackMix = 0.5f;
        controls.post.analogScanlineFocus = 0.5f; controls.post.analogMaskBalance = 0.5f; controls.post.analogNoise = 0.2f;
        controls.post.analogBloom = 0.3f; controls.post.vhsDistortion = 0; controls.post.analogChromaticAberration = 0.02f;
        controls.audio.warpResponse = 0; controls.audio.feedbackResponse = 0; controls.audio.blurResponse = 0;
        controls.audio.colorResponse = 0; controls.audio.glitchResponse = 0; controls.audio.beatSync = 0; controls.audio.lfoRate = 0.5f;
        controls.playback.temporalInterpolation = 0; controls.playback.temporalBlendStrength = 0; controls.playback.slowMotionFactor = 1; controls.playback.frameAccumulation = 0;
        changed = true; controlsDirty = true;
    }

    ImGui::Text("1. Color grading dinamico"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Color grading", &controls.color.enableColorGrading);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.color.enableColorGrading);
    changed |= ImGui::SliderFloat("Brightness",       &controls.color.gradeBrightness,  -1.0f, 1.0f,   "%.2f");
    changed |= ImGui::SliderFloat("Contrast",         &controls.color.gradeContrast,     0.1f, 2.5f,   "%.2f");
    changed |= ImGui::SliderFloat("Saturation",       &controls.color.gradeSaturation,   0.0f, 2.5f,   "%.2f");
    changed |= ImGui::SliderFloat("Hue shift",        &controls.color.gradeHueShift,  -180.0f, 180.0f, "%.1f°");
    changed |= ImGui::SliderFloat("Gamma",            &controls.color.gradeGamma,        0.4f, 3.0f,   "%.2f");
    changed |= ImGui::Combo("LUT",                    &controls.color.colorLUTIndex,     LUT_ITEMS);
    changed |= ImGui::SliderFloat("Split tone balance",&controls.color.splitToneBalance, 0.0f, 1.0f,   "%.2f");
    changed |= ImGui::ColorEdit3("Split tone shadows",    glm::value_ptr(controls.color.splitToneShadows));
    changed |= ImGui::ColorEdit3("Split tone highlights", glm::value_ptr(controls.color.splitToneHighlights));
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("2. Feedback temporal"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Feedback", &controls.temporal.enableFeedback);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.temporal.enableFeedback);
    changed |= ImGui::SliderFloat("Feedback",              &controls.temporal.feedbackAmount,      0,1,"%.2f");
    changed |= ImGui::SliderFloat("Trails",                &controls.temporal.trailStrength,       0,1,"%.2f");
    changed |= ImGui::SliderFloat("Temporal accumulation", &controls.temporal.temporalAccumulation,0,1,"%.2f");
    changed |= ImGui::SliderFloat("Decay",                 &controls.temporal.feedbackDecay,       0,1,"%.2f");
    changed |= ImGui::SliderFloat("Recursive blend",       &controls.temporal.recursiveBlend,      0,1,"%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("3. Distorsion espacial"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Distortion", &controls.fx.enableDistortion);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.fx.enableDistortion);
    changed |= ImGui::SliderFloat("UV warp",      &controls.fx.uvWarpStrength,    0.0f,  0.5f, "%.3f");
    changed |= ImGui::SliderFloat("Ripple",       &controls.fx.rippleStrength,    0.0f,  0.5f, "%.3f");
    changed |= ImGui::SliderFloat("Ripple freq",  &controls.fx.rippleFrequency,   0.5f,  6.0f, "%.1f");
    changed |= ImGui::SliderFloat("Swirl",        &controls.fx.swirlStrength,    -0.5f,  0.5f, "%.3f");
    changed |= ImGui::SliderFloat("Displacement", &controls.fx.displacementAmount,0.0f,  0.5f, "%.3f");
    changed |= ImGui::SliderFloat("Kaleido segs", &controls.fx.kaleidoSegments,   3.0f, 12.0f, "%.0f");
    changed |= ImGui::SliderFloat("Tunnel depth", &controls.fx.tunnelDepth,       0.0f,  0.5f, "%.3f");
    changed |= ImGui::SliderFloat("Tunnel curv",  &controls.fx.tunnelCurvature,  -0.5f,  0.5f, "%.3f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("4. Blur & motion"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Blur", &controls.fx.enableBlurMotion);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.fx.enableBlurMotion);
    changed |= ImGui::SliderFloat("Gaussian blur",     &controls.fx.gaussianBlur,        0,1,"%.2f");
    changed |= ImGui::SliderFloat("Directional blur",  &controls.fx.directionalBlur,     0,1,"%.2f");
    changed |= ImGui::SliderFloat("Directional angle", &controls.fx.directionalBlurAngle,0,360,"%.0f°");
    changed |= ImGui::SliderFloat("Zoom blur",         &controls.fx.zoomBlur,            0,1,"%.2f");
    changed |= ImGui::SliderFloat("Motion blur",       &controls.fx.motionBlur,          0,1,"%.2f");
    changed |= ImGui::SliderFloat("Temporal blur",     &controls.fx.temporalBlur,        0,1,"%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("5. Sharpen / detalle"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Sharpen", &controls.fx.enableSharpen);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.fx.enableSharpen);
    changed |= ImGui::SliderFloat("Unsharp mask",  &controls.fx.unsharpMask,   0,1,"%.2f");
    changed |= ImGui::SliderFloat("CAS",           &controls.fx.casAmount,     0,1,"%.2f");
    changed |= ImGui::SliderFloat("Local contrast",&controls.fx.localContrast, 0,1,"%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("6. Glitch / corruption"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Glitch", &controls.fx.enableGlitch);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.fx.enableGlitch);
    changed |= ImGui::SliderFloat("Datamosh",          &controls.fx.glitchDatamosh,       0,1,"%.2f");
    changed |= ImGui::SliderFloat("RGB split",         &controls.fx.glitchRGBSplit,       0,1,"%.2f");
    changed |= ImGui::SliderFloat("Scanline break",    &controls.fx.glitchScanlineBreak,  0,1,"%.2f");
    changed |= ImGui::SliderFloat("Jitter",            &controls.fx.glitchJitter,         0,1,"%.2f");
    changed |= ImGui::SliderFloat("Tearing",           &controls.fx.glitchTearing,        0,1,"%.2f");
    changed |= ImGui::SliderFloat("Pixel sorting",     &controls.fx.glitchPixelSort,      0,1,"%.2f");
    changed |= ImGui::SliderFloat("Buffer corruption", &controls.fx.glitchBufferCorruption,0,1,"%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("7. Compositing & blending"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Blending", &controls.blending.enableBlending);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.blending.enableBlending);
    changed |= ImGui::Combo("Video blend",     &controls.blending.blendModeVideo,     BLEND_ITEMS);
    changed |= ImGui::Combo("Feedback blend",  &controls.blending.blendModeFeedback,  BLEND_ITEMS);
    changed |= ImGui::SliderFloat("Video mix",     &controls.blending.blendVideoMix,    0,2,"%.2f");
    changed |= ImGui::SliderFloat("Feedback mix",  &controls.blending.blendFeedbackMix, 0,2,"%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("8. CRT / analog simulation"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Analog", &controls.post.enableAnalog);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.post.enableAnalog);
    changed |= ImGui::SliderFloat("Scanline focus",&controls.post.analogScanlineFocus,        0,1,   "%.2f");
    changed |= ImGui::SliderFloat("Mask balance",  &controls.post.analogMaskBalance,          0,1,   "%.2f");
    changed |= ImGui::SliderFloat("Analog noise",  &controls.post.analogNoise,                0,1,   "%.2f");
    changed |= ImGui::SliderFloat("Analog bloom",  &controls.post.analogBloom,                0,2,   "%.2f");
    changed |= ImGui::SliderFloat("VHS distortion",&controls.post.vhsDistortion,              0,1,   "%.2f");
    changed |= ImGui::SliderFloat("Analog chroma", &controls.post.analogChromaticAberration,  0,0.25f,"%.3f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("9. Audio reactivity"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Audio", &controls.system.enableAudioReactive);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.system.enableAudioReactive);
    changed |= ImGui::SliderFloat("Warp response",    &controls.audio.warpResponse,    0,2,"%.2f");
    changed |= ImGui::SliderFloat("Feedback response",&controls.audio.feedbackResponse,0,2,"%.2f");
    changed |= ImGui::SliderFloat("Blur response",    &controls.audio.blurResponse,    0,2,"%.2f");
    changed |= ImGui::SliderFloat("Color response",   &controls.audio.colorResponse,   0,2,"%.2f");
    changed |= ImGui::SliderFloat("Glitch response",  &controls.audio.glitchResponse,  0,2,"%.2f");
    changed |= ImGui::SliderFloat("Beat sync",        &controls.audio.beatSync,        0,4,"%.2f");
    changed |= ImGui::SliderFloat("LFO rate",         &controls.audio.lfoRate,     0.05f,4,"%.2f Hz");
    ImGui::EndDisabled();
    ImGui::Spacing();

    ImGui::Text("10. Temporal speed processing"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Temporal", &controls.temporal.enableTemporal);
    ImGui::Separator();
    ImGui::BeginDisabled(!controls.temporal.enableTemporal);
    changed |= ImGui::SliderFloat("Frame interpolation",&controls.playback.temporalInterpolation, 0,1,   "%.2f");
    changed |= ImGui::SliderFloat("Temporal blend",     &controls.playback.temporalBlendStrength, 0,1,   "%.2f");
    changed |= ImGui::SliderFloat("Slow-motion",        &controls.playback.slowMotionFactor,  0.1f,4,   "%.2fx");
    changed |= ImGui::SliderFloat("Frame accumulation", &controls.playback.frameAccumulation,     0,1,   "%.2f");
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (changed) controlsDirty = true;
}

void UISystem::drawVJayExtraContent(
    VisualControls& controls,
    bool&           controlsDirty,
    std::mt19937&   rng
) {
    bool changed = false;
    auto setAllExtraToggles = [&](bool v) {
        controls.fx.enablePixelate = v; controls.fx.enableStrobe = v; controls.fx.enableThreshold = v; controls.fx.enableSlowZoom = v;
        controls.fx.enableMirror = v; controls.fx.enableInvert = v; controls.fx.enablePosterize = v; controls.fx.enableInfrared = v;
        controls.fx.enableZoomPulse = v; controls.fx.enableRGBShift = v; controls.system.enableFXAA = v; controls.grid.enabled = v;
        controls.fx.enableEdgeDetect = v; controls.camera.enableMovement = v;
    };

    if (ImGui::Button("Randomize VJAY extra")) {
        randomizeVJayExtraControls(controls, rng);
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset VJAY extra")) {
        controls.fx.pixelateAmount = 0; controls.fx.strobeSpeed = 0; controls.fx.thresholdLevel = 0.5f; controls.fx.slowZoomAmount = 0;
        controls.fx.enablePixelate = controls.fx.enableStrobe = controls.fx.enableThreshold = controls.fx.enableSlowZoom = false;
        controls.system.enableFXAA = true;
        controls.system.fxaaQualitySubpix = 0.75f;
        controls.system.fxaaQualityEdgeThreshold = 0.125f;
        controls.system.fxaaQualityEdgeThresholdMin = 0.0625f;
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Turn all ON"))  { setAllExtraToggles(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Turn all OFF")) { setAllExtraToggles(false); changed = true; controlsDirty = true; }

    ImGui::Separator();

    changed |= ImGui::Checkbox("Pixelate", &controls.fx.enablePixelate);
    ImGui::BeginDisabled(!controls.fx.enablePixelate);
    changed |= ImGui::SliderFloat("Pixelate amount", &controls.fx.pixelateAmount, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Strobe", &controls.fx.enableStrobe);
    ImGui::BeginDisabled(!controls.fx.enableStrobe);
    changed |= ImGui::SliderFloat("Strobe speed", &controls.fx.strobeSpeed, 0.0f, 20.0f, "%.1f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Threshold", &controls.fx.enableThreshold);
    ImGui::BeginDisabled(!controls.fx.enableThreshold);
    changed |= ImGui::SliderFloat("Threshold level", &controls.fx.thresholdLevel, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Slow zoom", &controls.fx.enableSlowZoom);
    ImGui::BeginDisabled(!controls.fx.enableSlowZoom);
    changed |= ImGui::SliderFloat("Slow zoom amount", &controls.fx.slowZoomAmount, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::Separator();

    changed |= ImGui::Checkbox("FXAA", &controls.system.enableFXAA);
    ImGui::BeginDisabled(!controls.system.enableFXAA);
    changed |= ImGui::SliderFloat("FXAA Quality Subpix", &controls.system.fxaaQualitySubpix, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("FXAA Edge Threshold", &controls.system.fxaaQualityEdgeThreshold, 0.0f, 0.5f, "%.3f");
    changed |= ImGui::SliderFloat("FXAA Edge Threshold Min", &controls.system.fxaaQualityEdgeThresholdMin, 0.0f, 0.2f, "%.3f");
    ImGui::EndDisabled();

    ImGui::Separator();

    changed |= ImGui::Checkbox("Mirror", &controls.fx.enableMirror);
    ImGui::BeginDisabled(!controls.fx.enableMirror);
    changed |= ImGui::SliderFloat("Mirror amount", &controls.fx.mirrorAmount, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Posterize", &controls.fx.enablePosterize);
    ImGui::BeginDisabled(!controls.fx.enablePosterize);
    changed |= ImGui::SliderFloat("Posterize levels", &controls.fx.posterizeLevels, 2.0f, 16.0f, "%.0f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("Zoom pulse", &controls.fx.enableZoomPulse);
    ImGui::BeginDisabled(!controls.fx.enableZoomPulse);
    changed |= ImGui::SliderFloat("Zoom pulse amount", &controls.fx.zoomPulseAmount, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    changed |= ImGui::Checkbox("RGB shift", &controls.fx.enableRGBShift);
    ImGui::BeginDisabled(!controls.fx.enableRGBShift);
    changed |= ImGui::SliderFloat("RGB shift amount", &controls.fx.rgbShiftAmount, 0.0f, 0.1f, "%.3f");
    ImGui::EndDisabled();

    ImGui::Separator();

    changed |= ImGui::Checkbox("Grid overlay", &controls.grid.enabled);
    ImGui::BeginDisabled(!controls.grid.enabled);
    changed |= ImGui::Combo("Grid mode", &controls.grid.mode, "Vertical\0Horizontal\0Matrix\0");
    if (controls.grid.mode == 2) {
        changed |= ImGui::SliderInt("Rows", &controls.grid.rows, 1, 8);
        changed |= ImGui::SliderInt("Columns", &controls.grid.columns, 1, 8);
    } else {
        changed |= ImGui::SliderInt("Grid count", &controls.grid.count, 1, 8);
    }
    changed |= ImGui::Checkbox("Mirror cells", &controls.grid.mirrorCells);
    if (controls.grid.mirrorCells) {
        ImGui::TextDisabled("Alternate cells mirrored for seamless edges");
    }
    changed |= ImGui::Checkbox("Show grid lines", &controls.grid.showLines);
    ImGui::BeginDisabled(!controls.grid.showLines);
    changed |= ImGui::SliderFloat("Line width", &controls.grid.lineWidth, 0.0001f, 0.05f, "%.4f");
    changed |= ImGui::SliderFloat("Line intensity", &controls.grid.lineIntensity, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::ColorEdit3("Line color", &controls.grid.lineColor[0]);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::Separator();

    // Camera Movement
    changed |= ImGui::Checkbox("Camera movement", &controls.camera.enableMovement);
    ImGui::BeginDisabled(!controls.camera.enableMovement);
    changed |= ImGui::SliderFloat("Camera zoom", &controls.camera.zoom, 0.5f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Camera pan X", &controls.camera.panX, -1.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Camera pan Y", &controls.camera.panY, -1.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Camera rotation", &controls.camera.rotation, -3.14159f, 3.14159f, "%.2f rad");
    ImGui::EndDisabled();

    ImGui::Separator();

    // Final RGB overlay
    changed |= ImGui::Checkbox("RGB Overlay", &controls.color.enableRgbOverlay);
    ImGui::BeginDisabled(!controls.color.enableRgbOverlay);
    changed |= ImGui::ColorEdit3("Overlay color", &controls.color.rgbOverlay[0]);
    ImGui::EndDisabled();

    if (changed) controlsDirty = true;
}

void UISystem::drawNLEEditorContent(const UICallbacks& callbacks, const std::string& video1Path, const std::string& video2Path) {
    ImGui::Text("NLE Effects (for rendering/exporting only)");
    ImGui::TextDisabled("These effects are applied when rendering to a new file");
    ImGui::Separator();

    ImGui::Text("Video Source:");
    ImGui::SameLine();
    static int videoSource = static_cast<int>(g_project_state.nleVideoSource);
    if (ImGui::RadioButton("Video 1", &videoSource, 0)) {
        g_project_state.nleVideoSource = NLEVideoSource::VIDEO_1;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Video 2", &videoSource, 1)) {
        g_project_state.nleVideoSource = NLEVideoSource::VIDEO_2;
    }

    // Update active_file based on selection (store full path for rendering)
    const std::string& currentPath = (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) ? video1Path : video2Path;
    if (!currentPath.empty()) {
        g_project_state.active_file = currentPath;
    } else {
        g_project_state.active_file = "(no video loaded)";
    }

    ImGui::Separator();
    ImGui::Text("Quality Presets:");

    auto preset = [](int w, int h) {
        g_project_state.width = w; g_project_state.height = h;
    };
    if (ImGui::Button("Auto"))    { preset(0,0); g_project_state.fps = 0; }
    ImGui::SameLine(); if (ImGui::Button("240p"))     preset(426,240);
    ImGui::SameLine(); if (ImGui::Button("480p 4:3")) preset(640,480);
    ImGui::SameLine(); if (ImGui::Button("720p"))     preset(1280,720);
    ImGui::SameLine(); if (ImGui::Button("1080p"))    preset(1920,1080);
    ImGui::SameLine(); if (ImGui::Button("1080p 4:3"))preset(1440,1080);

    ImGui::Separator();
    ImGui::SliderFloat("Speed",  &g_project_state.speed,  0.25f, 4.0f, "%.2fx");
    ImGui::SliderInt("FPS",      &g_project_state.fps,    0, 120);
    ImGui::SliderInt("Width",    &g_project_state.width,  0, 3840);
    ImGui::SliderInt("Height",   &g_project_state.height, 0, 2160);

    static char scale_flags_buf[64] = "lanczos";
    if (g_project_state.scale_flags != std::string(scale_flags_buf)) {
        strncpy(scale_flags_buf, g_project_state.scale_flags.c_str(), sizeof(scale_flags_buf) - 1);
        scale_flags_buf[sizeof(scale_flags_buf) - 1] = '\0';
    }
    ImGui::InputText("Scale Flags", scale_flags_buf, sizeof(scale_flags_buf));
    g_project_state.scale_flags = scale_flags_buf;

    ImGui::Checkbox("Enable Unsharp", &g_project_state.enable_unsharp);
    if (g_project_state.enable_unsharp) {
        ImGui::SliderFloat("Unsharp Amount", &g_project_state.unsharp_amount, 0.0f, 2.0f);
        ImGui::SliderFloat("Unsharp Radius", &g_project_state.unsharp_radius, 1.0f, 10.0f);
    }

    ImGui::Separator();
    static char output_file_buf[256] = "output.mp4";
    if (g_project_state.output_file != std::string(output_file_buf)) {
        strncpy(output_file_buf, g_project_state.output_file.c_str(), sizeof(output_file_buf) - 1);
        output_file_buf[sizeof(output_file_buf) - 1] = '\0';
    }
    ImGui::InputText("Output File", output_file_buf, sizeof(output_file_buf));
    g_project_state.output_file = output_file_buf;
    ImGui::Separator();

    bool canRender = (g_project_state.active_file != "(no video loaded)");
    if (!canRender) {
        ImGui::BeginDisabled(true);
        ImGui::Button("Render/Export (No video loaded)");
        ImGui::EndDisabled();
    } else if (ImGui::Button("Render/Export")) {
        g_project_state.do_swap = true;
        g_project_state.increment_version();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Changes") && callbacks.onApplyChanges) {
        callbacks.onApplyChanges();
    }

    ImGui::Separator();
    ImGui::Text("Current File: %s", g_project_state.active_file.c_str());
    ImGui::Text("Version: %lu",     g_project_state.get_version());
    ImGui::Text("Dirty: %s",        g_project_state.is_dirty() ? "Yes" : "No");
}

void UISystem::drawDiagnosticsContent(
    const UIDiagnostics& diag,
    VideoPlayer&         player,
    VideoPlayer&         player2,
    VideoRegistry&       registry,
    int&                 selectedAsset,
    int&                 selectedAsset2,
    VisualControls&      controls,
    const UICallbacks&   callbacks
) {
    ImGui::Text("Frame %u | Image %u", diag.lastFrameFrameIndex, diag.lastFrameImageIndex);
    ImGui::Text("Swapchain: %ux%u",    diag.swapchainWidth, diag.swapchainHeight);
    ImGui::Text("Current mode: %d",    diag.currentMode);
    ImGui::Text("FPS: %.1f",           ImGui::GetIO().Framerate);

    ImGui::Separator();
    ImGui::Text("Video 1:");
    if (diag.videoReady) {
        ImGui::Text("Video: %ux%u", diag.videoWidth, diag.videoHeight);
        if (player.isReady()) {
            double fd  = std::max(1e-6, player.frameDuration());
            double fps = 1.0 / fd;
            double oversample = std::max(1.0, static_cast<double>(controls.playback.videoDecodeOversample));
            double playRate = [&]() -> double {
                double base = std::clamp((double)controls.playback.videoPlaybackRate, 0.05, 8.0);
                int idx = std::clamp(controls.playback.forcedFpsIndex, 0,
                                     static_cast<int>(FORCED_FPS_OPTIONS_UI.size())-1);
                int forced = FORCED_FPS_OPTIONS_UI[idx];
                if (forced <= 0) return base;
                return std::clamp(forced / fps, 0.05, 8.0);
            }();
            ImGui::Text("Clip FPS: %.2f",     fps);
            ImGui::Text("Display FPS: %.2f",  fps * playRate);
            ImGui::Text("Decode FPS:  %.2f",  fps * std::max(playRate, oversample));
            if (controls.playback.forcedFpsIndex > 0)
                ImGui::Text("Forced FPS: %d", FORCED_FPS_OPTIONS_UI[controls.playback.forcedFpsIndex]);
        }
    } else {
        ImGui::Text("Video offline");
    }

    ImGui::Separator();
    ImGui::Text("Video 2:");
    if (player2.isReady()) {
        ImGui::Text("Video: %ux%u", player2.width(), player2.height());
        double fd  = std::max(1e-6, player2.frameDuration());
        double fps = 1.0 / fd;
        ImGui::Text("Clip FPS: %.2f", fps);
    } else {
        ImGui::Text("Video offline");
    }

    if (ImGui::Button("Reset Palette")) {
        controls.color.primaryColor   = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
        controls.color.secondaryColor = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
    }

    const auto& assets = registry.getAssets();
    if (selectedAsset >= 0 && selectedAsset < static_cast<int>(assets.size())) {
        const auto& meta = assets[selectedAsset].metadata;
        ImGui::Separator();
        ImGui::Text("Asset: %s",      meta.filename.c_str());
        ImGui::Text("Resolution: %dx%d", meta.width, meta.height);
        ImGui::Text("FPS: %.2f",      meta.fps);
        ImGui::Text("Duration: %.2f s", meta.duration);
        ImGui::Text("Bitrate: %.0f kbps", meta.bitrate / 1000.0);
        ImGui::Text("Audio: %s",      meta.hasAudio ? "yes" : "no");
        ImGui::Separator();

        static bool showRename = false;
        if (ImGui::Button("Rename")) showRename = !showRename;
        ImGui::SameLine();
        if (ImGui::Button("Delete") && callbacks.onDeleteAsset)
            callbacks.onDeleteAsset(selectedAsset);

        if (showRename) {
            static char renameBuf[256] = "";
            ImGui::InputText("New Filename", renameBuf, sizeof(renameBuf));
            ImGui::SameLine();
            if (ImGui::Button("Confirm") && strlen(renameBuf) > 0) {
                if (callbacks.onRenameAsset)
                    callbacks.onRenameAsset(selectedAsset, renameBuf);
                memset(renameBuf, 0, sizeof(renameBuf));
                showRename = false;
            }
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Animation Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("CPU elapsed: %.3f s", diag.animationElapsedSeconds);
        ImGui::Text("UBO time: %.3f", diag.animationTime);
        ImGui::Text("UBO delta: %.6f", diag.animationDelta);
        ImGui::Text("Speed multiplier: %.2fx", diag.animationRelativeSpeed);
        ImGui::Text("Seconds per unit: %.3f", diag.animationSecondsPerUnit);
        bool targetChanged = ImGui::SliderFloat("Target seconds", &controls.playback.animationTargetSeconds, 0.1f, 5.0f, "%.2fs");
        if (targetChanged && callbacks.onControlsChanged) callbacks.onControlsChanged();
        float suggestedSpeed = 1.0f / std::max(0.1f, controls.playback.animationTargetSeconds);
        ImGui::Text("Suggested speed: %.2fx", suggestedSpeed);
        if (ImGui::Button("Apply suggested speed")) {
            controls.playback.animationSpeed = suggestedSpeed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset time phase")) {
            controls.playback.animationSpeed = 1.0f;
            controls.playback.animationTargetSeconds = 1.0f;
            if (callbacks.onControlsChanged) callbacks.onControlsChanged();
        }
    }
}

void UISystem::drawMidiControlsContent(MidiSystem& midiSystem) {
    bool enabled = midiSystem.isEnabled();
    if (ImGui::Checkbox("Enable MIDI", &enabled)) {
        midiSystem.setEnabled(enabled);
    }

    ImGui::Separator();

    unsigned int portCount = midiSystem.getPortCount();
    ImGui::Text("MIDI Ports: %u", portCount);

    static int selectedPort = 0;
    if (portCount > 0) {
        auto ports = midiSystem.getAvailablePorts();
        std::vector<const char*> portNames;
        for (const auto& port : ports) {
            portNames.push_back(port.c_str());
        }

        if (ImGui::Combo("Port", &selectedPort, portNames.data(), static_cast<int>(portCount))) {
            midiSystem.closePort();
            if (midiSystem.openPort(selectedPort)) {
                std::cout << "[UI] Opened MIDI port " << selectedPort << std::endl;
            }
        }
    } else {
        ImGui::Text("No MIDI devices detected");
    }

    ImGui::Separator();

    ImGui::Text("MIDI Learn Wizard");
    bool learnMode = midiSystem.isLearnMode();
    if (ImGui::Checkbox("Learn Mode", &learnMode)) {
        midiSystem.setLearnMode(learnMode);
    }

    if (midiSystem.isLearnMode()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Move a knob or press a key on your MIDI device...");
    }

    if (midiSystem.hasLearnedMessage()) {
        MidiMessage msg = midiSystem.getLastLearnedMessage();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Detected MIDI Message:");
        
        if (msg.type == MidiEventType::CONTROL_CHANGE) {
            ImGui::Text("Type: Control Change");
            ImGui::Text("CC Number: %d", msg.controller);
            ImGui::Text("Value: %d", msg.value);

            ImGui::Separator();
            ImGui::Text("Assign to Parameter:");

            static int selectedParamIndex = 0;
            selectedParamIndex = std::clamp(selectedParamIndex, 0, PARAMETER_COUNT - 1);

            const auto& displayStrings = getParameterDisplayStrings();
            ImGui::Combo("Parameter", &selectedParamIndex, [](void* data, int idx) -> const char* {
                const auto* strings = static_cast<const std::vector<std::string>*>(data);
                return (*strings)[idx].c_str();
            }, (void*)&displayStrings, static_cast<int>(displayStrings.size()));

            static bool invert = false;
            ImGui::Checkbox("Invert", &invert);

            ImGui::Text("Range: %.2f to %.2f", PARAMETER_INFOS[selectedParamIndex].minVal, PARAMETER_INFOS[selectedParamIndex].maxVal);

            if (ImGui::Button("Assign Mapping")) {
                std::string paramName = PARAMETER_INFOS[selectedParamIndex].name;
                midiSystem.addMapping(msg.controller, paramName,
                    PARAMETER_INFOS[selectedParamIndex].minVal,
                    PARAMETER_INFOS[selectedParamIndex].maxVal, invert);
                std::cout << "[UI] Assigned CC " << msg.controller << " to " << paramName << std::endl;
                midiSystem.clearLearnedMessage();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                midiSystem.clearLearnedMessage();
            }
        } else if (msg.type == MidiEventType::NOTE_ON) {
            ImGui::Text("Type: Note On");
            ImGui::Text("Note: %d", msg.note);
            ImGui::Text("Velocity: %d", msg.velocity);

            ImGui::Separator();
            ImGui::Text("Assign to Trigger Action:");

            static int selectedTriggerActionIdx = 0;
            ImGui::Combo("Action##MidiLearnTab", &selectedTriggerActionIdx, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);

            if (ImGui::Button("Assign Trigger")) {
                const char* actionName = TRIGGER_ACTIONS[selectedTriggerActionIdx];
                midiSystem.addTriggerMapping(msg.note, actionName);
                std::cout << "[UI] Assigned MIDI note " << msg.note << " to " << actionName << std::endl;
                midiSystem.clearLearnedMessage();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                midiSystem.clearLearnedMessage();
            }
        } else {
            ImGui::Text("This MIDI message type is not supported for learn mode yet.");
            if (ImGui::Button("Cancel")) {
                midiSystem.clearLearnedMessage();
            }
        }

        drawTriggerAndRgbReferenceSection();
    }

    ImGui::Separator();

    ImGui::Text("Current Mappings:");
    const auto& mappings = midiSystem.getMappings();
    if (ImGui::BeginTable("MidiMappings", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("CC");
        ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Max");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        std::vector<std::pair<int, MidiMapping>> mappingsCopy(mappings.begin(), mappings.end());
        for (const auto& [cc, mapping] : mappingsCopy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", cc);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.parameterName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", mapping.minValue);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", mapping.maxValue);
            ImGui::TableSetColumnIndex(4);
            std::string label = "Remove##" + std::to_string(cc);
            if (ImGui::Button(label.c_str())) {
                midiSystem.removeMapping(cc);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("MIDI Trigger Buttons:");
    const auto& triggerMappings = midiSystem.getTriggerMappings();
    if (ImGui::BeginTable("MidiTriggerMappingsTab", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Note");
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove");
        ImGui::TableHeadersRow();

        std::vector<std::pair<int, MidiTriggerMapping>> triggersCopy(triggerMappings.begin(), triggerMappings.end());
        for (const auto& [note, mapping] : triggersCopy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", note);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.actionName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string label = "Remove##MidiTrigTab" + std::to_string(note);
            if (ImGui::Button(label.c_str())) {
                midiSystem.removeTriggerMapping(note);
            }
        }
        ImGui::EndTable();
    } else if (triggerMappings.empty()) {
        ImGui::TextDisabled("No MIDI triggers assigned yet");
    }

    ImGui::Separator();
    ImGui::Text("Add MIDI Trigger:");
    static int triggerNoteInput = 60;
    triggerNoteInput = std::clamp(triggerNoteInput, 0, 127);
    ImGui::SliderInt("Note##MidiTriggerAdd", &triggerNoteInput, 0, 127);

    static int triggerActionIdx = 0;
    ImGui::Combo("Action##MidiTriggerAdd", &triggerActionIdx, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);

    if (ImGui::Button("Add MIDI Trigger")) {
        const char* actionName = TRIGGER_ACTIONS[triggerActionIdx];
        midiSystem.addTriggerMapping(triggerNoteInput, actionName);
        std::cout << "[UI] Added MIDI trigger note " << triggerNoteInput << " -> " << actionName << std::endl;
    }

    ImGui::Separator();
    ImGui::Text("Default CC Mappings:");
    ImGui::Text("CC 1: animationSpeed");
    ImGui::Text("CC 2: tempo");
    ImGui::Text("CC 3: energy");
    ImGui::Text("CC 4: bass");
    ImGui::Text("CC 5: mid");
    ImGui::Text("CC 6: high");
    ImGui::Text("CC 7: colorBlend");
    ImGui::Text("CC 8: bloomIntensity");
    ImGui::Text("CC 12: feedbackAmount");
    ImGui::Text("CC 13: uvWarpStrength");
    ImGui::Text("CC 14: glitchAmount");

    ImGui::Separator();
    ImGui::Text("Note Triggers:");
    ImGui::Text("Notes 36-48: Mode switching");
    ImGui::Text("Note 60: Random video change");
    ImGui::Text("Note 62: Toggle bloom");
    ImGui::Text("Note 64: Toggle glitch");
    ImGui::Text("Note 65: Toggle bend");
    ImGui::Text("Note 67: Toggle feedback");
}

void UISystem::drawOscControlsContent(OscSystem& oscSystem) {
    bool enabled = oscSystem.isEnabled();
    if (ImGui::Checkbox("Enable OSC", &enabled)) {
        oscSystem.setEnabled(enabled);
    }

    ImGui::Separator();

    ImGui::Text("OSC Port: %d", oscSystem.getPort());
    ImGui::Text("Listening for OSC messages on UDP port");
    ImGui::Separator();
    std::string localIP = OscSystem::getLocalIPAddress();
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "IP Address: %s", localIP.c_str());
    ImGui::Text("Configure your OSC client with:");
    ImGui::Text("Host: %s", localIP.c_str());
    ImGui::Text("Port: %d", oscSystem.getPort());
    ImGui::Text("Protocol: UDP");

    ImGui::Separator();

    ImGui::Text("OSC Learn Wizard");
    bool learnMode = oscSystem.isLearnMode();
    if (ImGui::Checkbox("Learn Mode", &learnMode)) {
        oscSystem.setLearnMode(learnMode);
    }

    if (oscSystem.isLearnMode()) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Send an OSC message to learn its address...");
    }

    if (oscSystem.hasLearnedMessage()) {
        OscMessage msg = oscSystem.getLastLearnedMessage();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Detected OSC Message:");
        ImGui::Text("Address: %s", msg.address.c_str());

        if (msg.type == OscMessageType::FLOAT) {
            ImGui::Text("Type: Float");
            ImGui::Text("Value: %.4f", msg.floatValue);
        } else if (msg.type == OscMessageType::INT) {
            ImGui::Text("Type: Int");
            ImGui::Text("Value: %d", msg.intValue);
        } else if (msg.type == OscMessageType::STRING) {
            ImGui::Text("Type: String");
            ImGui::Text("Value: %s", msg.stringValue.c_str());
        }

        ImGui::Separator();
        ImGui::Text("Assign to Parameter:");

        static int selectedParamIndex = 0;
        selectedParamIndex = std::clamp(selectedParamIndex, 0, PARAMETER_COUNT - 1);

        const auto& displayPtrs = getParameterDisplayPtrs();
        static int prevParamIndex = -1;
        if (ImGui::Combo("Parameter", &selectedParamIndex, displayPtrs.data(), static_cast<int>(displayPtrs.size()))) {
            prevParamIndex = -1;
        }

        static float minVal = 0.0f;
        static float maxVal = 1.0f;
        if (prevParamIndex != selectedParamIndex) {
            minVal = PARAMETER_INFOS[selectedParamIndex].minVal;
            maxVal = PARAMETER_INFOS[selectedParamIndex].maxVal;
            prevParamIndex = selectedParamIndex;
        }
        ImGui::DragFloat("Min", &minVal, 0.01f);
        ImGui::DragFloat("Max", &maxVal, 0.01f);

        static bool invert = false;
        ImGui::Checkbox("Invert", &invert);

        if (ImGui::Button("Assign Mapping")) {
            const char* paramName = PARAMETER_INFOS[selectedParamIndex].name;
            oscSystem.addMapping(msg.address, paramName, minVal, maxVal, invert);
            std::cout << "[UI] Assigned OSC address " << msg.address << " to " << paramName << std::endl;
            oscSystem.clearLearnedMessage();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            oscSystem.clearLearnedMessage();
        }
    }

    ImGui::Separator();

    ImGui::Text("Current Mappings:");
    const auto& mappings = oscSystem.getMappings();
    if (ImGui::BeginTable("OscMappings", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Parameter");
        ImGui::TableSetupColumn("Min");
        ImGui::TableSetupColumn("Max");
        ImGui::TableHeadersRow();

        for (const auto& [addr, mapping] : mappings) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", addr.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.parameterName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", mapping.minValue);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", mapping.maxValue);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("OSC Address Examples:");
    ImGui::Text("/vjay/animationSpeed");
    ImGui::Text("/vjay/bloomIntensity");
    ImGui::Text("/vjay/feedbackAmount");
    ImGui::Text("Send float values 0.0-1.0 for normalized control");

    ImGui::Separator();
    ImGui::Text("OSC Triggers (Buttons):");
    const auto& triggerMappings = oscSystem.getTriggerMappings();
    if (ImGui::BeginTable("OscTriggers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove");
        ImGui::TableHeadersRow();

        std::vector<std::pair<std::string, OscTriggerMapping>> triggersCopy(triggerMappings.begin(), triggerMappings.end());
        for (const auto& [addr, mapping] : triggersCopy) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", addr.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", mapping.actionName.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string label = "Remove##" + addr;
            if (ImGui::Button(label.c_str())) {
                oscSystem.removeTriggerMapping(addr);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Add Trigger Mapping:");
    static char triggerAddress[256] = "/vjay/";
    ImGui::InputText("OSC Address", triggerAddress, IM_ARRAYSIZE(triggerAddress));

    static int selectedAction = 0;
    ImGui::Combo("Action", &selectedAction, TRIGGER_ACTIONS, TRIGGER_ACTION_COUNT);

    if (ImGui::Button("Add Trigger")) {
        const char* actionName = TRIGGER_ACTIONS[selectedAction];
        oscSystem.addTriggerMapping(triggerAddress, actionName);
        std::cout << "[UI] Added OSC trigger " << triggerAddress << " -> " << actionName << std::endl;
    }

    ImGui::Separator();
    ImGui::Text("Trigger Examples:");
    ImGui::Text("/vjay/randomize (no arguments)");
    ImGui::Text("/vjay/randomize2 (no arguments)");
    ImGui::Text("/vjay/jump (no arguments)");
    ImGui::Text("Send messages without arguments for triggers");
}

void UISystem::drawAudioDebugContent(AudioSystem& audioSystem, VisualControls& controls) {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Audio Reactive Debug");
    ImGui::Separator();
    
    ImGui::Text("Audio-inspired inputs");
    ImGui::SliderFloat("Tempo",  &controls.playback.tempo,  0.25f, 4.0f);
    ImGui::Checkbox("Auto tempo LFO", &controls.playback.enableTempoLfo);
    if (controls.playback.enableTempoLfo) {
        ImGui::SliderFloat("LFO speed (Hz)", &controls.playback.tempoLfoSpeed, 0.05f, 4.0f, "%.2f Hz");
        ImGui::SliderFloat("LFO depth", &controls.playback.tempoLfoDepth, 0.0f, 2.0f);
    }
    ImGui::SliderFloat("Energy", &controls.audio.energy, 0.0f, 1.0f);
    ImGui::SliderFloat("Bass",   &controls.audio.bass,   0.0f, 1.0f);
    ImGui::SliderFloat("Mid",    &controls.audio.mid,    0.0f, 1.0f);
    ImGui::SliderFloat("High",   &controls.audio.high,   0.0f, 1.0f);
    ImGui::SliderFloat("High gain boost", &controls.audio.highGain, 0.5f, 4.0f, "%.2fx");
    ImGui::SliderFloat("Procedural audio drive", &controls.audio.reactiveDrive, 0.5f, 3.0f, "%.2fx");
    
    ImGui::Separator();

    ImGui::Text("Monitoreo en vivo (solo lectura)");
    float liveEnergy = controls.runtime.audioReactive.energy;
    float liveBass   = controls.runtime.audioReactive.bass;
    float liveMid    = controls.runtime.audioReactive.mid;
    float liveHigh   = controls.runtime.audioReactive.high;
    ImGui::BeginDisabled(true);
    ImGui::SliderFloat("Energy meter", &liveEnergy, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Bass meter",   &liveBass,   0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Mid meter",    &liveMid,    0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("High meter",   &liveHigh,   0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();
    ImGui::Separator();
    
    // Device Selection
    ImGui::Text("Input Device:");
    std::vector<std::string> deviceNames = audioSystem.getInputDeviceNames();
    int currentDeviceIndex = audioSystem.getInputDeviceIndex();
    
    // Create a filtered list of only devices with input channels
    std::vector<int> validDeviceIndices;
    std::vector<const char*> validDeviceNames;
    for (size_t i = 0; i < deviceNames.size(); i++) {
        if (!deviceNames[i].empty()) {
            validDeviceIndices.push_back(i);
            validDeviceNames.push_back(deviceNames[i].c_str());
        }
    }
    
    // Find current device index in the filtered list
    int currentFilteredIndex = 0;
    for (size_t i = 0; i < validDeviceIndices.size(); i++) {
        if (validDeviceIndices[i] == currentDeviceIndex) {
            currentFilteredIndex = i;
            break;
        }
    }
    
    if (ImGui::Combo("##Device", &currentFilteredIndex, validDeviceNames.data(), validDeviceNames.size())) {
        int newDeviceIndex = validDeviceIndices[currentFilteredIndex];
        audioSystem.setInputDevice(newDeviceIndex);
    }
    
    ImGui::Separator();
    
    // Status
    ImGui::Text("Audio Stream: %s", audioSystem.isRunning() ? "Running" : "Stopped");
    
    ImGui::Separator();
    
    // RMS Level
    float rms = audioSystem.getRMS();
    ImGui::Text("RMS Level: %.4f", rms);
    ImGui::ProgressBar(rms, ImVec2(200, 0));
    
    ImGui::Separator();
    
    // Raw Band Levels
    ImGui::Text("Raw Band Levels (Techno Optimized):");
    ImGui::Text("SubBass: %.4f", audioSystem.getSubBass());
    ImGui::Text("Kick:    %.4f", audioSystem.getKick());
    ImGui::Text("Bass:    %.4f", audioSystem.getBass());
    ImGui::Text("Mid:     %.4f", audioSystem.getMid());
    ImGui::Text("High:    %.4f", audioSystem.getHigh());
    
    ImGui::Separator();
    
    // Smoothed Band Levels
    ImGui::Text("Smoothed Band Levels:");
    ImGui::Text("SubBass: %.4f", audioSystem.getSmoothedSubBass());
    ImGui::Text("Kick:    %.4f", audioSystem.getSmoothedKick());
    ImGui::Text("Bass:    %.4f", audioSystem.getSmoothedBass());
    ImGui::Text("Mid:     %.4f", audioSystem.getSmoothedMid());
    ImGui::Text("High:    %.4f", audioSystem.getSmoothedHigh());
    
    ImGui::Separator();
    
    // FFT Visualization
    ImGui::Text("FFT Spectrum (256 bins):");
    const std::vector<float>& fftMagnitudes = audioSystem.getFFTMagnitudes();
    
    // Draw FFT bars
    float maxMagnitude = 0.0f;
    for (float mag : fftMagnitudes) {
        if (mag > maxMagnitude) maxMagnitude = mag;
    }
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float barWidth = 200.0f / fftMagnitudes.size();
    float maxHeight = 100.0f;
    
    for (size_t i = 0; i < fftMagnitudes.size(); i++) {
        float normalizedHeight = maxMagnitude > 0 ? (fftMagnitudes[i] / maxMagnitude) * maxHeight : 0;
        ImVec2 barMin(p.x + i * barWidth, p.y + maxHeight - normalizedHeight);
        ImVec2 barMax(p.x + (i + 1) * barWidth - 1, p.y + maxHeight);
        
        // Color gradient from bass (blue) to high (red)
        float t = static_cast<float>(i) / fftMagnitudes.size();
        ImColor color(t, 0.5f * (1.0f - t), 1.0f - t);
        drawList->AddRectFilled(barMin, barMax, color);
    }
    
    ImGui::Dummy(ImVec2(200, maxHeight + 5));

    ImGui::Separator();
    ImGui::Text("Realtime shader modulation (per audio frame)");
    const auto& reactive = controls.runtime.audioReactive;
    ImGui::Text("State: %s", reactive.enabled ? "ENABLED" : "disabled");
    if (reactive.enabled) {
        ImGui::Text("Energy %.3f | Bass %.3f | Mid %.3f | High %.3f",
                    reactive.energy, reactive.bass, reactive.mid, reactive.high);

        ImGui::Separator();
        ImGui::Text("Spatial Distortion");
        ImGui::Text("uvWarp %.3f | ripple %.3f | swirl %.3f", reactive.uvWarpStrength,
                    reactive.rippleStrength, reactive.swirlStrength);
        ImGui::Text("displacement %.3f | bend %.3f", reactive.displacementAmount, reactive.bendAmount);

        ImGui::Separator();
        ImGui::Text("Temporal / Feedback");
        ImGui::Text("feedback %.3f | trails %.3f", reactive.feedbackAmount, reactive.trailStrength);

        ImGui::Separator();
        ImGui::Text("Glitch / Grain");
        ImGui::Text("jitter %.3f | rgbSplit %.3f | tearing %.3f | grain %.3f",
                    reactive.glitchJitter, reactive.glitchRGBSplit, reactive.glitchTearing, reactive.grainStrength);

        ImGui::Separator();
        ImGui::Text("Output Extras");
        ImGui::Text("zoomPulse %.3f | slowZoom %.3f | strobe %.3f | rgbShift %.3f",
                    reactive.zoomPulseAmount, reactive.slowZoomAmount, reactive.strobeSpeed, reactive.rgbShiftAmount);

        ImGui::Separator();
        ImGui::Text("Camera");
        ImGui::Text("zoom %.3f | panX %.3f | panY %.3f | rot %.3f",
                    reactive.cameraZoom, reactive.cameraPanX, reactive.cameraPanY, reactive.cameraRotation);
    }
}

void UISystem::drawParameterIndexContent() {
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Complete Parameter Reference");
    ImGui::Separator();
    ImGui::Text("Use these parameter names for MIDI CC and OSC mapping");

    auto listParams = [](std::initializer_list<const char*> params) {
        for (const char* name : params) {
            ImGui::BulletText("%s", name);
        }
    };

    if (ImGui::BeginTabBar("ParameterTabs")) {
        if (ImGui::BeginTabItem("Core & Color")) {
            listParams({
                "animationSpeed", "animationTargetSeconds", "tempo",
                "energy", "bass", "mid", "high",
                "colorBlend", "colorBalance (vec3)",
                "primaryColor", "secondaryColor", "rgbOverlay",
                "autoRandomizeColors", "colorRandomizeInterval",
                "gradeBrightness", "gradeContrast", "gradeSaturation",
                "gradeHueShift", "gradeGamma", "colorLUTIndex",
                "splitToneBalance", "splitToneShadows", "splitToneHighlights",
                "enableColorGrading", "enableRgbOverlay", "enablePostColorBalance"
            });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Video & Mix")) {
            listParams({
                "videoMix", "videoPlaybackRate", "videoDecodeOversample",
                "autoScaleVideo", "selectedVideoFolder", "loopBlendSeconds", "forcedFpsIndex",
                "randomVideoStart", "randomJumpInterval", "enableRandomJumpInterval",
                "enableDualVideo", "video2Mix", "video2BlendMode",
                "selectedVideo2Folder", "selectedVideo2Asset", "video2PlaybackRate",
                "randomVideo2Start", "randomJumpInterval2", "enableRandomJumpInterval2",
                "blendModeProcedural", "blendModeVideo", "blendModeFeedback",
                "blendProceduralMix", "blendVideoMix", "blendFeedbackMix"
            });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Spatial & Camera")) {
            listParams({
                "uvWarpStrength", "rippleStrength", "rippleFrequency",
                "swirlStrength", "displacementAmount", "bendAmount",
                "kaleidoSegments", "tunnelDepth", "tunnelCurvature",
                "cameraZoom", "cameraPanX", "cameraPanY", "cameraRotation",
                "enableCameraMovement", "zoomPulseAmount", "slowZoomAmount",
                "enableMirror", "mirrorAmount", "enableInvert",
                "enablePosterize", "posterizeLevels", "enableInfrared",
                "enableGrid", "gridMode", "gridCount", "gridRows", "gridColumns", "gridMirrorCells"
            });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Post FX & Blur")) {
            listParams({
                "bloomIntensity", "bloomThreshold", "enablePostBloom",
                "aberrationAmount", "enablePostAberration",
                "grainStrength", "enablePostGrain",
                "crtCurvature", "crtHorizontalCurvature", "crtFishEye",
                "crtScanlineIntensity", "crtMaskIntensity", "crtVignette",
                "enablePostCrtCurvature", "enablePostScanMask", "enablePostVignette", "enablePostFishEye",
                "gaussianBlur", "directionalBlur", "directionalBlurAngle",
                "zoomBlur", "motionBlur", "temporalBlur",
                "unsharpMask", "casAmount", "localContrast",
                "pixelateAmount", "enablePixelate",
                "strobeSpeed", "enableStrobe",
                "thresholdLevel", "enableThreshold",
                "slowZoomAmount", "enableSlowZoom",
                "enableEdgeDetect", "edgeStrength", "edgeThreshold", "edgeBlend", "edgeColor",
                "enableFXAA", "fxaaQualitySubpix", "fxaaQualityEdgeThreshold", "fxaaQualityEdgeThresholdMin"
            });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Glitch & Temporal")) {
            listParams({
                "feedbackAmount", "trailStrength", "temporalAccumulation",
                "temporalInterpolation", "temporalBlendStrength", "frameAccumulation",
                "feedbackDecay", "recursiveBlend", "slowMotionFactor",
                "glitchAmount", "glitchDatamosh", "glitchRGBSplit", "glitchScanlineBreak",
                "glitchJitter", "glitchTearing", "glitchPixelSort", "glitchBufferCorruption",
                "analogScanlineFocus", "analogMaskBalance", "analogNoise", "analogBloom",
                "vhsDistortion", "analogChromaticAberration",
                "enableFeedback", "enableTemporal", "enableGlitch", "enableDistortion", "enableAnalog",
                "enablePostBend", "enablePostGlitch"
            });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Extras & Audio")) {
            listParams({
                "enableMirror", "enableInvert", "enablePosterize", "enableInfrared",
                "enableZoomPulse", "zoomPulseAmount", "enableRGBShift", "rgbShiftAmount",
                "enableGrid", "gridMode", "gridCount", "gridRows", "gridColumns", "gridMirrorCells",
                "enableAudioReactive", "audioWarpResponse", "audioFeedbackResponse",
                "audioBlurResponse", "audioColorResponse", "audioGlitchResponse",
                "audioBeatSync", "audioLfoRate",
                "enableRandomJumpInterval2", "randomVideo2Start",
                "enableRandomJumpInterval", "randomVideoStart"
            });
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Trigger Actions")) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "OSC Triggers (send message without arguments)");
            ImGui::Text("randomizeVideo - Randomize current video");
            ImGui::Text("jumpRandom - Random jump within current video");
            ImGui::Text("folderChanged - Reload video folder");
            ImGui::Text("applyChanges - Apply render changes");
            
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "MIDI Note Triggers");
            ImGui::Text("Notes 36-48 - Mode switching");
            ImGui::Text("Note 60 - Random video change");
            ImGui::Text("Note 62 - Toggle bloom");
            ImGui::Text("Note 64 - Toggle glitch");
            ImGui::Text("Note 65 - Toggle bend");
            ImGui::Text("Note 67 - Toggle feedback");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
