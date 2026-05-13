#include "UISystem.h"

// ImGui
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

// GLM para value_ptr
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// Tus tipos existentes
#include "app/VisualControls.h"      // struct VisualControls
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

#include <filesystem>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

// Constantes copiadas del main (muevelas a un Config.h cuando quieras)
static const std::array<int, 5> FORCED_FPS_OPTIONS_UI = {0, 15, 24, 30, 60};

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

    ImGui::SaveIniSettingsToDisk("imgui.ini");
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
    VideoPlayer&          player,
    VideoRegistry&        registry,
    int&                  selectedVideoAsset,
    float&                transitionDuration,
    bool&                 allowDimensionChangeRecreation,
    bool&                 controlsDirty,
    std::mt19937&         rng,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks)
{
    if (!initialized || !renderer) return;

    beginFrame();

    drawProceduralControls(controls, randomizer, player, registry,
                           selectedVideoAsset, transitionDuration,
                           allowDimensionChangeRecreation, controlsDirty,
                           rng, diag, callbacks);

    drawVJayBasics(controls, controlsDirty, rng);
    drawVJayExtra(controls, controlsDirty, rng);

    if (showNLEWindow) {
        drawNLEEditor(callbacks);
    }

    if (showDiagnostics) {
        drawDiagnostics(diag, player, registry, selectedVideoAsset, controls, callbacks);
    }

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
    float&                transitionDuration,
    bool&                 allowDimensionChange,
    bool&                 controlsDirty,
    std::mt19937&         rng,
    const UIDiagnostics&  diag,
    const UICallbacks&    callbacks)
{
    bool changed = false;

    ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Procedural Controls")) { ImGui::End(); return; }

    ImGui::Text("Animation");
    changed |= ImGui::SliderFloat("Speed", &controls.animationSpeed, 0.05f, 1.0f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Layers");
    changed |= ImGui::Combo("Active Layer", &controls.activeMode, "Layer 0\0Layer 1\0");

    ImGui::Separator();
    ImGui::Text("Color Palette");
    changed |= ImGui::ColorEdit4("Primary",   glm::value_ptr(controls.primaryColor));
    changed |= ImGui::ColorEdit4("Secondary", glm::value_ptr(controls.secondaryColor));
    changed |= ImGui::SliderFloat("Blend", &controls.colorBlend, 0.0f, 1.0f);

    ImGui::Separator();
    ImGui::Text("Audio-inspired inputs");
    changed |= ImGui::SliderFloat("Tempo",  &controls.tempo,  0.25f, 4.0f);
    changed |= ImGui::SliderFloat("Energy", &controls.energy, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Bass",   &controls.bass,   0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Mid",    &controls.mid,    0.0f, 1.0f);
    changed |= ImGui::SliderFloat("High",   &controls.high,   0.0f, 1.0f);

    ImGui::Separator();
    ImGui::Text("Video");
    changed |= ImGui::SliderFloat("Video Mix",   &controls.videoMix,         0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Video speed", &controls.videoPlaybackRate, 0.1f, 5.0f, "%.2fx");

    if (ImGui::SliderFloat("Decode oversample", &controls.videoDecodeOversample, 1.0f, 8.0f, "%.1fx")) {
        controls.videoDecodeOversample = std::clamp(controls.videoDecodeOversample, 1.0f, 8.0f);
        changed = true;
    }

    static const char* forceFpsLabels[] = {"Off", "15 fps", "24 fps", "30 fps", "60 fps"};
    int forceIdx = std::clamp(controls.forcedFpsIndex, 0,
                              static_cast<int>(FORCED_FPS_OPTIONS_UI.size()) - 1);
    if (forceIdx != controls.forcedFpsIndex) { controls.forcedFpsIndex = forceIdx; changed = true; }
    if (ImGui::Combo("Force FPS", &forceIdx, forceFpsLabels, IM_ARRAYSIZE(forceFpsLabels))) {
        controls.forcedFpsIndex = forceIdx;
        changed = true;
    }

    changed |= ImGui::SliderFloat("Grayscale", &controls.grayscaleAmount, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sharpen",   &controls.sharpenAmount,   0.0f, 1.0f);
    changed |= ImGui::Checkbox("Bicubic Upscale", &controls.upscaleEnabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Random start", &controls.randomVideoStart);

    if (controls.randomVideoStart) {
        ImGui::SameLine();
        if (ImGui::Button("Jump Random") && callbacks.onJumpRandom) {
            callbacks.onJumpRandom();
        }
    }

    ImGui::Indent();
    changed |= ImGui::Checkbox("Auto jump interval", &controls.enableRandomJumpInterval);
    if (controls.enableRandomJumpInterval) {
        changed |= ImGui::SliderFloat("Interval (s)", &controls.randomJumpInterval, 1.0f, 60.0f, "%.1f s");
    }
    ImGui::Unindent();

    changed |= ImGui::SliderFloat("Loop crossfade (s)", &controls.loopBlendSeconds, 0.0f, 2.0f, "%.2f s");

    // --- Post FX ---
    ImGui::Separator();
    ImGui::Text("Post FX");

    auto setPostFxEnabled = [&](bool enabled) {
        controls.enablePostCrtCurvature  = enabled;
        controls.enablePostScanMask      = enabled;
        controls.enablePostVignette      = enabled;
        controls.enablePostFishEye       = enabled;
        controls.enablePostBloom         = enabled;
        controls.enablePostAberration    = enabled;
        controls.enablePostGrain         = enabled;
        controls.enablePostBend          = enabled;
        controls.enablePostGlitch        = enabled;
        controls.enablePostColorBalance  = enabled;
    };

    if (ImGui::Button("Randomize Post FX")) {
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        auto rr = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };
        if (controls.enablePostCrtCurvature)  { controls.crtCurvature = rr(0,0.6f); controls.crtHorizontalCurvature = rr(0,0.6f); }
        if (controls.enablePostScanMask)       { controls.crtScanlineIntensity = u01(rng); controls.crtMaskIntensity = u01(rng); }
        if (controls.enablePostVignette)       { controls.crtVignette = u01(rng); }
        if (controls.enablePostFishEye)        { controls.crtFishEye = rr(-3,3); }
        if (controls.enablePostBloom)          { controls.bloomIntensity = rr(0,2); controls.bloomThreshold = u01(rng); }
        if (controls.enablePostAberration)     { controls.aberrationAmount = rr(-0.05f,0.05f); }
        if (controls.enablePostGrain)          { controls.grainStrength = rr(0,0.5f); }
        if (controls.enablePostBend)           { controls.bendAmount = u01(rng); }
        if (controls.enablePostGlitch)         { controls.glitchAmount = u01(rng); }
        if (controls.enablePostColorBalance)   { controls.colorBalance = glm::vec3(rr(0,2), rr(0,2), rr(0,2)); }
        controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Post FX")) {
        setPostFxEnabled(false);
        controls.crtCurvature           = 0.15f;
        controls.crtHorizontalCurvature = 0.15f;
        controls.crtScanlineIntensity   = 0.35f;
        controls.crtMaskIntensity       = 0.35f;
        controls.crtVignette            = 0.55f;
        controls.crtFishEye             = 0.0f;
        controls.bloomIntensity         = 0.45f;
        controls.bloomThreshold         = 0.7f;
        controls.aberrationAmount       = 0.02f;
        controls.grainStrength          = 0.15f;
        controls.bendAmount             = 0.0f;
        controls.glitchAmount           = 0.0f;
        controls.colorBalance           = glm::vec3(1.0f);
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Enable All"))  { setPostFxEnabled(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) { setPostFxEnabled(false); changed = true; controlsDirty = true; }

    ImGui::Separator();

    // Macros para no repetir el patron checkbox+slider
#define POST_FX_SECTION(label, toggleField, ...) \
    changed |= ImGui::Checkbox(label, &controls.toggleField); \
    ImGui::BeginDisabled(!controls.toggleField); \
    __VA_ARGS__ \
    ImGui::EndDisabled();

    POST_FX_SECTION("CRT Curvature", enablePostCrtCurvature,
        changed |= ImGui::SliderFloat("CRT Curvature V", &controls.crtCurvature, 0.0f, 0.6f, "%.2f");
        changed |= ImGui::SliderFloat("CRT Curvature H", &controls.crtHorizontalCurvature, 0.0f, 0.6f, "%.2f");
    )
    POST_FX_SECTION("Scanlines / Mask", enablePostScanMask,
        changed |= ImGui::SliderFloat("CRT Scanlines", &controls.crtScanlineIntensity, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("CRT Mask",      &controls.crtMaskIntensity,     0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Vignette", enablePostVignette,
        changed |= ImGui::SliderFloat("CRT Black Bars", &controls.crtVignette, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Fish-eye", enablePostFishEye,
        changed |= ImGui::SliderFloat("CRT Fish-eye", &controls.crtFishEye, -3.0f, 3.0f, "%.2f");
    )
    POST_FX_SECTION("Bloom", enablePostBloom,
        changed |= ImGui::SliderFloat("Bloom Intensity", &controls.bloomIntensity, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bloom Threshold", &controls.bloomThreshold, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Aberration##Toggle", enablePostAberration,
        changed |= ImGui::SliderFloat("Aberration", &controls.aberrationAmount, -0.05f, 0.05f, "%.3f");
    )
    POST_FX_SECTION("Film Grain##Toggle", enablePostGrain,
        changed |= ImGui::SliderFloat("Film Grain", &controls.grainStrength, 0.0f, 0.5f, "%.2f");
    )
    POST_FX_SECTION("Screen Bend", enablePostBend,
        changed |= ImGui::SliderFloat("Bend Amount", &controls.bendAmount, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("Glitch wrapper", enablePostGlitch,
        changed |= ImGui::SliderFloat("Glitch Intensity", &controls.glitchAmount, 0.0f, 1.0f, "%.2f");
    )
    POST_FX_SECTION("RGB Mix##Toggle", enablePostColorBalance,
        changed |= ImGui::SliderFloat3("RGB Mix", glm::value_ptr(controls.colorBalance), 0.0f, 2.0f);
    )
#undef POST_FX_SECTION

    // --- Video asset selector ---
    ImGui::TextWrapped("Video %s", diag.videoReady ? "online" : "unavailable");

    const auto& assets = registry.getAssets();
    if (assets.empty()) {
        ImGui::TextDisabled("No videos found");
    } else {
        if (selectedAsset < 0 || selectedAsset >= static_cast<int>(assets.size()))
            selectedAsset = 0;

        const std::string& currentLabel = assets[selectedAsset].metadata.filename;
        if (ImGui::BeginCombo("Video Asset", currentLabel.c_str())) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                bool isSelected = (i == selectedAsset);
                std::string label = assets[i].metadata.filename;
                fs::path assetPath(assets[i].metadata.path);
                // Mostrar subruta si no esta en la raiz
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

    ImGui::End();

    if (changed && callbacks.onControlsChanged) callbacks.onControlsChanged();
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

    auto rr  = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo,hi)(rng); };
    auto ri  = [&](int lo, int hi)     { return std::uniform_int_distribution<int>(lo,hi)(rng); };
    auto u01 = [&]()                   { return std::uniform_real_distribution<float>(0,1)(rng); };

    auto setAllToggles = [&](bool v) {
        c.enableColorGrading  = v; c.enableFeedback    = v; c.enableDistortion = v;
        c.enableBlurMotion    = v; c.enableSharpen     = v; c.enableGlitch     = v;
        c.enableBlending      = v; c.enableAnalog      = v; c.enableAudioReactive = v;
        c.enableTemporal      = v;
    };

    if (ImGui::Button("Randomize VJAY basics")) {
        if (c.enableColorGrading) {
            c.gradeBrightness = rr(-0.2f,0.2f); c.gradeContrast  = rr(0.8f,1.4f);
            c.gradeSaturation = rr(0.6f,1.6f);  c.gradeHueShift  = rr(-90,90);
            c.gradeGamma      = rr(0.8f,1.4f);  c.colorLUTIndex  = ri(0,5);
            c.splitToneBalance = u01() * 0.5f;
            c.splitToneShadows    = glm::vec3(u01(), u01(), u01());
            c.splitToneHighlights = glm::vec3(u01(), u01(), u01());
        }
        if (c.enableFeedback) {
            c.feedbackAmount = u01(); c.trailStrength = u01();
            c.temporalAccumulation = u01(); c.feedbackDecay = u01(); c.recursiveBlend = u01();
        }
        if (c.enableDistortion) {
            c.uvWarpStrength = rr(0,0.5f); c.rippleStrength = rr(0,0.5f); c.rippleFrequency = rr(0.5f,6);
            c.swirlStrength = rr(-0.5f,0.5f); c.displacementAmount = rr(0,0.5f);
            c.kaleidoSegments = rr(3,12); c.tunnelDepth = rr(0,0.5f); c.tunnelCurvature = rr(-0.5f,0.5f);
        }
        if (c.enableBlurMotion) {
            c.gaussianBlur = u01(); c.directionalBlur = u01(); c.directionalBlurAngle = rr(0,360);
            c.zoomBlur = u01(); c.motionBlur = u01(); c.temporalBlur = u01();
        }
        if (c.enableSharpen)  { c.unsharpMask = u01(); c.casAmount = u01(); c.localContrast = u01(); }
        if (c.enableGlitch) {
            c.glitchDatamosh = u01(); c.glitchRGBSplit = u01(); c.glitchScanlineBreak = u01();
            c.glitchJitter = u01(); c.glitchTearing = u01(); c.glitchPixelSort = u01();
            c.glitchBufferCorruption = u01();
        }
        if (c.enableBlending) {
            c.blendModeProcedural = ri(0,5); c.blendModeVideo = ri(0,5); c.blendModeFeedback = ri(0,5);
            c.blendProceduralMix = rr(0,2); c.blendVideoMix = rr(0,2); c.blendFeedbackMix = rr(0,2);
        }
        if (c.enableAnalog) {
            c.analogScanlineFocus = u01(); c.analogMaskBalance = u01(); c.analogNoise = u01();
            c.analogBloom = rr(0,2); c.vhsDistortion = u01(); c.analogChromaticAberration = rr(0,0.25f);
        }
        if (c.enableAudioReactive) {
            c.audioWarpResponse = rr(0,2); c.audioFeedbackResponse = rr(0,2);
            c.audioBlurResponse = rr(0,2); c.audioColorResponse = rr(0,2);
            c.audioGlitchResponse = rr(0,2); c.audioBeatSync = rr(0,4); c.audioLfoRate = rr(0.05f,4);
        }
        if (c.enableTemporal) {
            c.temporalInterpolation = u01(); c.temporalBlendStrength = u01();
            c.slowMotionFactor = rr(0.1f,4); c.frameAccumulation = u01();
        }
        changed = true; controlsDirty = true;
    }

    if (ImGui::Button("Turn all ON"))  { setAllToggles(true);  changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Turn all OFF")) { setAllToggles(false); changed = true; controlsDirty = true; }
    ImGui::SameLine();
    if (ImGui::Button("Reset VJAY basics")) {
        setAllToggles(false);
        c.gradeBrightness = 0; c.gradeContrast = 1; c.gradeSaturation = 1;
        c.gradeHueShift = 0; c.gradeGamma = 1; c.colorLUTIndex = 0;
        c.splitToneBalance = 0.5f;
        c.splitToneShadows = glm::vec3(0); c.splitToneHighlights = glm::vec3(1);
        c.feedbackAmount = 0; c.trailStrength = 0; c.temporalAccumulation = 0;
        c.feedbackDecay = 0; c.recursiveBlend = 0;
        c.uvWarpStrength = 0; c.rippleStrength = 0; c.rippleFrequency = 1;
        c.swirlStrength = 0; c.displacementAmount = 0; c.kaleidoSegments = 6;
        c.tunnelDepth = 0; c.tunnelCurvature = 0;
        c.gaussianBlur = 0; c.directionalBlur = 0; c.directionalBlurAngle = 0;
        c.zoomBlur = 0; c.motionBlur = 0; c.temporalBlur = 0;
        c.unsharpMask = 0; c.casAmount = 0; c.localContrast = 0;
        c.glitchDatamosh = 0; c.glitchRGBSplit = 0; c.glitchScanlineBreak = 0;
        c.glitchJitter = 0; c.glitchTearing = 0; c.glitchPixelSort = 0; c.glitchBufferCorruption = 0;
        c.blendModeProcedural = 0; c.blendModeVideo = 1; c.blendModeFeedback = 2;
        c.blendProceduralMix = 1; c.blendVideoMix = 1; c.blendFeedbackMix = 0.5f;
        c.analogScanlineFocus = 0.5f; c.analogMaskBalance = 0.5f; c.analogNoise = 0.2f;
        c.analogBloom = 0.3f; c.vhsDistortion = 0; c.analogChromaticAberration = 0.02f;
        c.audioWarpResponse = 0; c.audioFeedbackResponse = 0; c.audioBlurResponse = 0;
        c.audioColorResponse = 0; c.audioGlitchResponse = 0; c.audioBeatSync = 0; c.audioLfoRate = 0.5f;
        c.temporalInterpolation = 0; c.temporalBlendStrength = 0; c.slowMotionFactor = 1; c.frameAccumulation = 0;
        changed = true; controlsDirty = true;
    }

    // Macro para secciones con toggle on/off
#define VJAY_SECTION(num, title, toggleField, body) \
    ImGui::Text(num ". " title); ImGui::SameLine(); \
    changed |= ImGui::Checkbox("On##" title, &c.toggleField); \
    ImGui::Separator(); \
    ImGui::BeginDisabled(!c.toggleField); \
    body \
    ImGui::EndDisabled(); \
    ImGui::Spacing();

    VJAY_SECTION("1","Color grading dinamico", enableColorGrading,
        changed |= ImGui::SliderFloat("Brightness",       &c.gradeBrightness,  -1.0f, 1.0f,   "%.2f");
        changed |= ImGui::SliderFloat("Contrast",         &c.gradeContrast,     0.1f, 2.5f,   "%.2f");
        changed |= ImGui::SliderFloat("Saturation",       &c.gradeSaturation,   0.0f, 2.5f,   "%.2f");
        changed |= ImGui::SliderFloat("Hue shift",        &c.gradeHueShift,  -180.0f, 180.0f, "%.1f\xc2\xb0");
        changed |= ImGui::SliderFloat("Gamma",            &c.gradeGamma,        0.4f, 3.0f,   "%.2f");
        changed |= ImGui::Combo("LUT",                    &c.colorLUTIndex,     LUT_ITEMS);
        changed |= ImGui::SliderFloat("Split tone balance",&c.splitToneBalance, 0.0f, 1.0f,   "%.2f");
        changed |= ImGui::ColorEdit3("Split tone shadows",    glm::value_ptr(c.splitToneShadows));
        changed |= ImGui::ColorEdit3("Split tone highlights", glm::value_ptr(c.splitToneHighlights));
    )
    VJAY_SECTION("2","Feedback temporal", enableFeedback,
        changed |= ImGui::SliderFloat("Feedback",              &c.feedbackAmount,      0,1,"%.2f");
        changed |= ImGui::SliderFloat("Trails",                &c.trailStrength,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Temporal accumulation", &c.temporalAccumulation,0,1,"%.2f");
        changed |= ImGui::SliderFloat("Decay",                 &c.feedbackDecay,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Recursive blend",       &c.recursiveBlend,      0,1,"%.2f");
    )
    VJAY_SECTION("3","Distorsion espacial", enableDistortion,
        changed |= ImGui::SliderFloat("UV warp",      &c.uvWarpStrength,    0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Ripple",       &c.rippleStrength,    0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Ripple freq",  &c.rippleFrequency,   0.5f,  6.0f, "%.1f");
        changed |= ImGui::SliderFloat("Swirl",        &c.swirlStrength,    -0.5f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Displacement", &c.displacementAmount,0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Kaleido segs", &c.kaleidoSegments,   3.0f, 12.0f, "%.0f");
        changed |= ImGui::SliderFloat("Tunnel depth", &c.tunnelDepth,       0.0f,  0.5f, "%.3f");
        changed |= ImGui::SliderFloat("Tunnel curv",  &c.tunnelCurvature,  -0.5f,  0.5f, "%.3f");
    )
    VJAY_SECTION("4","Blur & motion", enableBlurMotion,
        changed |= ImGui::SliderFloat("Gaussian blur",     &c.gaussianBlur,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("Directional blur",  &c.directionalBlur,     0,1,"%.2f");
        changed |= ImGui::SliderFloat("Directional angle", &c.directionalBlurAngle,0,360,"%.0f\xc2\xb0");
        changed |= ImGui::SliderFloat("Zoom blur",         &c.zoomBlur,            0,1,"%.2f");
        changed |= ImGui::SliderFloat("Motion blur",       &c.motionBlur,          0,1,"%.2f");
        changed |= ImGui::SliderFloat("Temporal blur",     &c.temporalBlur,        0,1,"%.2f");
    )
    VJAY_SECTION("5","Sharpen / detalle", enableSharpen,
        changed |= ImGui::SliderFloat("Unsharp mask",  &c.unsharpMask,   0,1,"%.2f");
        changed |= ImGui::SliderFloat("CAS",           &c.casAmount,     0,1,"%.2f");
        changed |= ImGui::SliderFloat("Local contrast",&c.localContrast, 0,1,"%.2f");
    )
    VJAY_SECTION("6","Glitch / corruption", enableGlitch,
        changed |= ImGui::SliderFloat("Datamosh",          &c.glitchDatamosh,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("RGB split",         &c.glitchRGBSplit,       0,1,"%.2f");
        changed |= ImGui::SliderFloat("Scanline break",    &c.glitchScanlineBreak,  0,1,"%.2f");
        changed |= ImGui::SliderFloat("Jitter",            &c.glitchJitter,         0,1,"%.2f");
        changed |= ImGui::SliderFloat("Tearing",           &c.glitchTearing,        0,1,"%.2f");
        changed |= ImGui::SliderFloat("Pixel sorting",     &c.glitchPixelSort,      0,1,"%.2f");
        changed |= ImGui::SliderFloat("Buffer corruption", &c.glitchBufferCorruption,0,1,"%.2f");
    )
    VJAY_SECTION("7","Compositing & blending", enableBlending,
        changed |= ImGui::Combo("Procedural blend",&c.blendModeProcedural,BLEND_ITEMS);
        changed |= ImGui::Combo("Video blend",     &c.blendModeVideo,     BLEND_ITEMS);
        changed |= ImGui::Combo("Feedback blend",  &c.blendModeFeedback,  BLEND_ITEMS);
        changed |= ImGui::SliderFloat("Procedural mix",&c.blendProceduralMix,0,2,"%.2f");
        changed |= ImGui::SliderFloat("Video mix",     &c.blendVideoMix,    0,2,"%.2f");
        changed |= ImGui::SliderFloat("Feedback mix",  &c.blendFeedbackMix, 0,2,"%.2f");
    )
    VJAY_SECTION("8","CRT / analog simulation", enableAnalog,
        changed |= ImGui::SliderFloat("Scanline focus",&c.analogScanlineFocus,        0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Mask balance",  &c.analogMaskBalance,          0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Analog noise",  &c.analogNoise,                0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Analog bloom",  &c.analogBloom,                0,2,   "%.2f");
        changed |= ImGui::SliderFloat("VHS distortion",&c.vhsDistortion,              0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Analog chroma", &c.analogChromaticAberration,  0,0.25f,"%.3f");
    )
    VJAY_SECTION("9","Audio reactivity", enableAudioReactive,
        changed |= ImGui::SliderFloat("Warp response",    &c.audioWarpResponse,    0,2,"%.2f");
        changed |= ImGui::SliderFloat("Feedback response",&c.audioFeedbackResponse,0,2,"%.2f");
        changed |= ImGui::SliderFloat("Blur response",    &c.audioBlurResponse,    0,2,"%.2f");
        changed |= ImGui::SliderFloat("Color response",   &c.audioColorResponse,   0,2,"%.2f");
        changed |= ImGui::SliderFloat("Glitch response",  &c.audioGlitchResponse,  0,2,"%.2f");
        changed |= ImGui::SliderFloat("Beat sync",        &c.audioBeatSync,        0,4,"%.2f");
        changed |= ImGui::SliderFloat("LFO rate",         &c.audioLfoRate,     0.05f,4,"%.2f Hz");
    )
    VJAY_SECTION("10","Temporal speed processing", enableTemporal,
        changed |= ImGui::SliderFloat("Frame interpolation",&c.temporalInterpolation, 0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Temporal blend",     &c.temporalBlendStrength, 0,1,   "%.2f");
        changed |= ImGui::SliderFloat("Slow-motion",        &c.slowMotionFactor,  0.1f,4,   "%.2fx");
        changed |= ImGui::SliderFloat("Frame accumulation", &c.frameAccumulation,     0,1,   "%.2f");
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

    auto rr = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo,hi)(rng); };

    if (ImGui::Button("Randomize VJAY extra")) {
        c.pixelateAmount  = rr(0,1); c.strobeSpeed    = rr(0,10);
        c.thresholdLevel  = rr(0,1); c.slowZoomAmount = rr(0,1);
        changed = true; controlsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset VJAY extra")) {
        c.pixelateAmount = 0; c.strobeSpeed = 0; c.thresholdLevel = 0.5f; c.slowZoomAmount = 0;
        c.enablePixelate = c.enableStrobe = c.enableThreshold = c.enableSlowZoom = false;
        changed = true; controlsDirty = true;
    }

#define EXTRA_SECTION(num, label, toggleField, slider) \
    ImGui::Spacing(); ImGui::Text(num ". " label); ImGui::SameLine(); \
    changed |= ImGui::Checkbox("On##" label, &c.toggleField); \
    ImGui::Separator(); \
    ImGui::BeginDisabled(!c.toggleField); \
    slider \
    ImGui::EndDisabled();

    EXTRA_SECTION("1","Pixelate",  enablePixelate,  changed |= ImGui::SliderFloat("Pixelate amount", &c.pixelateAmount,  0,1,"%.2f");)
    EXTRA_SECTION("2","Strobe",    enableStrobe,    changed |= ImGui::SliderFloat("Strobe speed",    &c.strobeSpeed,    0,20,"%.1f Hz");)
    EXTRA_SECTION("3","Threshold", enableThreshold, changed |= ImGui::SliderFloat("Threshold level", &c.thresholdLevel,  0,1,"%.2f");)
    EXTRA_SECTION("4","Slow Zoom", enableSlowZoom,  changed |= ImGui::SliderFloat("Slow zoom amount",&c.slowZoomAmount,  0,1,"%.2f");)
    EXTRA_SECTION("5","Edge Detect", enableEdgeDetect,
        changed |= ImGui::SliderFloat("Edge strength",  &c.edgeStrength,  0.1f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("Edge threshold", &c.edgeThreshold, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Edge blend",     &c.edgeBlend,     0.0f, 1.0f, "%.2f");
        changed |= ImGui::ColorEdit3("Edge color", glm::value_ptr(c.edgeColor));
    )
#undef EXTRA_SECTION

    ImGui::End();
    if (changed) controlsDirty = true;
}

// ============================================================
// Ventana "NLE Editor"
// ============================================================

void UISystem::drawNLEEditor(const UICallbacks& callbacks) {
    ImGui::SetNextWindowSize(ImVec2(400.0f, 500.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("NLE Editor", &showNLEWindow)) { ImGui::End(); return; }

    ImGui::Text("NLE Effects (for rendering/exporting only)");
    ImGui::TextDisabled("These effects are applied when rendering to a new file");
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

    if (ImGui::Button("Render/Export")) {
        g_project_state.do_swap = false;
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
    VideoRegistry&       registry,
    int&                 selectedAsset,
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
            double oversample = std::max(1.0, static_cast<double>(controls.videoDecodeOversample));
            double playRate = [&]() -> double {
                // Calculo inline de effectivePlaybackRate
                double base = std::clamp((double)controls.videoPlaybackRate, 0.05, 8.0);
                int idx = std::clamp(controls.forcedFpsIndex, 0,
                                     static_cast<int>(FORCED_FPS_OPTIONS_UI.size())-1);
                int forced = FORCED_FPS_OPTIONS_UI[idx];
                if (forced <= 0) return base;
                return std::clamp(forced / fps, 0.05, 8.0);
            }();
            ImGui::Text("Clip FPS: %.2f",     fps);
            ImGui::Text("Display FPS: %.2f",  fps * playRate);
            ImGui::Text("Decode FPS:  %.2f",  fps * std::max(playRate, oversample));
            if (controls.forcedFpsIndex > 0)
                ImGui::Text("Forced FPS: %d", FORCED_FPS_OPTIONS_UI[controls.forcedFpsIndex]);
        }
    } else {
        ImGui::Text("Video offline");
    }

    if (ImGui::Button("Reset Palette")) {
        controls.primaryColor   = glm::vec4(0.9f, 0.4f, 0.1f, 1.0f);
        controls.secondaryColor = glm::vec4(0.1f, 0.5f, 0.8f, 1.0f);
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

    ImGui::End();
}
