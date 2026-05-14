#include "UISystem.h"
#include "OscSystem.h"

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
    const UICallbacks&    callbacks,
    MidiSystem&           midiSystem,
    OscSystem&            oscSystem
) {
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

    if (showMidiWindow) {
        drawMidiControls(midiSystem);
    }

    if (showOscWindow) {
        drawOscControls(oscSystem);
    }

    if (showParameterIndex) {
        drawParameterIndex();
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
    changed |= ImGui::SliderFloat("Speed", &controls.animationSpeed, 0.1f, 8.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Target change (s)", &controls.animationTargetSeconds, 0.1f, 5.0f, "%.2fs");
    ImGui::SameLine();
    if (ImGui::Button("Snap 1s")) {
        controls.animationTargetSeconds = 1.0f;
        controls.animationSpeed = 1.0f;
        changed = true;
    }

    ImGui::Separator();
    ImGui::Text("Layers");
    changed |= ImGui::Combo("Active Layer", &controls.activeMode, "Layer 0\0Layer 1\0");

    ImGui::Separator();
    ImGui::Text("Color Palette");
    changed |= ImGui::ColorEdit4("Primary",   glm::value_ptr(controls.primaryColor));
    changed |= ImGui::ColorEdit4("Secondary", glm::value_ptr(controls.secondaryColor));
    changed |= ImGui::SliderFloat("Blend", &controls.colorBlend, 0.0f, 1.0f);
    
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
        
        controls.primaryColorTarget = glm::vec4(hsvToRgb(primaryHue, primarySat, primaryVal), 1.0f);
        controls.secondaryColorTarget = glm::vec4(hsvToRgb(secondaryHue, secondarySat, secondaryVal), 1.0f);
        controls.primaryColor = controls.primaryColorTarget;
        controls.secondaryColor = controls.secondaryColorTarget;
        changed = true;
        controlsDirty = true;
    }
    
    changed |= ImGui::Checkbox("Auto Randomize", &controls.autoRandomizeColors);
    if (controls.autoRandomizeColors) {
        changed |= ImGui::SliderFloat("Interval (s)", &controls.colorRandomizeInterval, 0.1f, 5.0f, "%.1fs");
    }

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

    changed |= ImGui::Checkbox("Auto Scale Video", &controls.autoScaleVideo);

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
    if (!controls.selectedVideoFolder.empty()) {
        for (size_t i = 0; i < availableFolders.size(); ++i) {
            if (availableFolders[i] == controls.selectedVideoFolder) {
                currentFolderIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (ImGui::Combo("Load Folder", &currentFolderIndex, folderItems.data(), static_cast<int>(folderItems.size()))) {
        std::string newFolder = (currentFolderIndex == 0) ? "" : availableFolders[currentFolderIndex];
        if (controls.selectedVideoFolder != newFolder) {
            controls.selectedVideoFolder = newFolder;
            if (callbacks.onFolderChanged) {
                callbacks.onFolderChanged();
            }
        }
    }

    // Show current loaded folder
    if (controls.selectedVideoFolder.empty()) {
        ImGui::Text("Current loaded folder: All Folders");
    } else {
        ImGui::Text("Current loaded folder: %s", controls.selectedVideoFolder.c_str());
    }

    // Video assets from current folder
    const auto& assets = registry.getFilteredAssets(controls.selectedVideoFolder);
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
        c.enableFXAA = true;
        c.fxaaQualitySubpix = 0.75f;
        c.fxaaQualityEdgeThreshold = 0.125f;
        c.fxaaQualityEdgeThresholdMin = 0.0625f;
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

    // FXAA - Fast Approximate Anti-Aliasing for smooth HD edges
    ImGui::Spacing(); ImGui::Text("6. FXAA (Anti-Aliasing)"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##FXAA", &c.enableFXAA);
    ImGui::Separator();
    ImGui::BeginDisabled(!c.enableFXAA);
    changed |= ImGui::SliderFloat("Quality Subpix",      &c.fxaaQualitySubpix,       0.0f, 1.0f, "%.3f");
    changed |= ImGui::SliderFloat("Edge Threshold",      &c.fxaaQualityEdgeThreshold, 0.0f, 0.5f, "%.4f");
    changed |= ImGui::SliderFloat("Edge Threshold Min",  &c.fxaaQualityEdgeThresholdMin, 0.0f, 0.2f, "%.4f");
    ImGui::EndDisabled();

    // Grid / Mirroring - Show video in multiple positions
    ImGui::Spacing(); ImGui::Text("7. Grid / Mirroring"); ImGui::SameLine();
    changed |= ImGui::Checkbox("On##Grid", &c.enableGrid);
    ImGui::Separator();
    ImGui::BeginDisabled(!c.enableGrid);
    static const char* gridModeLabels[] = {"Vertical (side by side)", "Horizontal (stacked)", "Matrix (grid 2D)"};
    changed |= ImGui::Combo("Mode", &c.gridMode, gridModeLabels, 3);

    if (c.gridMode == 2) {
        // Matrix mode: show rows and columns
        changed |= ImGui::SliderInt("Rows", &c.gridRows, 1, 8);
        changed |= ImGui::SliderInt("Columns", &c.gridColumns, 1, 8);
    } else {
        // Vertical or Horizontal mode: show count
        changed |= ImGui::SliderInt("Count", &c.gridCount, 2, 8);
    }
    ImGui::EndDisabled();

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

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Animation Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("CPU elapsed: %.3f s", diag.animationElapsedSeconds);
        ImGui::Text("UBO time: %.3f", diag.animationTime);
        ImGui::Text("UBO delta: %.6f", diag.animationDelta);
        ImGui::Text("Speed multiplier: %.2fx", diag.animationRelativeSpeed);
        ImGui::Text("Seconds per unit: %.3f", diag.animationSecondsPerUnit);
        bool targetChanged = ImGui::SliderFloat("Target seconds", &controls.animationTargetSeconds, 0.1f, 5.0f, "%.2fs");
        if (targetChanged && callbacks.onControlsChanged) callbacks.onControlsChanged();
        float suggestedSpeed = 1.0f / std::max(0.1f, controls.animationTargetSeconds);
        ImGui::Text("Suggested speed: %.2fx", suggestedSpeed);
        if (ImGui::Button("Apply suggested speed")) {
            controls.animationSpeed = suggestedSpeed;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset time phase")) {
            controls.animationSpeed = 1.0f;
            controls.animationTargetSeconds = 1.0f;
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
        } else if (msg.type == MidiEventType::NOTE_ON) {
            ImGui::Text("Type: Note On");
            ImGui::Text("Note: %d", msg.note);
            ImGui::Text("Velocity: %d", msg.velocity);
        }

        ImGui::Separator();
        ImGui::Text("Assign to Parameter:");

        // List of all VisualControls parameters with their default ranges and categories
        struct ParameterInfo {
            const char* category;
            const char* name;
            float minVal;
            float maxVal;
        };
        static const ParameterInfo parameters[] = {
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
        static int selectedParamIndex = 0;

        // Build display strings with category prefix
        static std::vector<std::string> displayStrings;
        if (displayStrings.empty()) {
            displayStrings.reserve(IM_ARRAYSIZE(parameters));
            for (size_t i = 0; i < IM_ARRAYSIZE(parameters); ++i) {
                displayStrings.push_back(std::string(parameters[i].category) + ": " + parameters[i].name);
            }
        }

        if (ImGui::Combo("Parameter", &selectedParamIndex, [](void* data, int idx) -> const char* {
            const std::vector<std::string>* strings = static_cast<const std::vector<std::string>*>(data);
            return (*strings)[idx].c_str();
        }, (void*)&displayStrings, static_cast<int>(displayStrings.size()))) {
            // Parameter selection changed
        }

        static bool invert = false;
        ImGui::Checkbox("Invert", &invert);

        ImGui::Text("Range: %.2f to %.2f", parameters[selectedParamIndex].minVal, parameters[selectedParamIndex].maxVal);

        if (ImGui::Button("Assign Mapping")) {
            std::string paramName = parameters[selectedParamIndex].name;
            if (msg.type == MidiEventType::CONTROL_CHANGE) {
                midiSystem.addMapping(msg.controller, paramName,
                    parameters[selectedParamIndex].minVal,
                    parameters[selectedParamIndex].maxVal, invert);
                std::cout << "[UI] Assigned CC " << msg.controller << " to " << paramName << std::endl;
            }
            midiSystem.clearLearnedMessage();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            midiSystem.clearLearnedMessage();
        }
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

        // List of all VisualControls parameters with their default ranges and categories (same as MIDI)
        struct ParameterInfo {
            const char* category;
            const char* name;
            float minVal;
            float maxVal;
        };
        static const ParameterInfo parameters[] = {
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
        static int selectedParamIndex = 0;

        // Build display strings with category prefix
        static std::vector<std::string> displayStrings;
        if (displayStrings.empty()) {
            displayStrings.reserve(IM_ARRAYSIZE(parameters));
            for (size_t i = 0; i < IM_ARRAYSIZE(parameters); ++i) {
                displayStrings.push_back(std::string(parameters[i].category) + ": " + parameters[i].name);
            }
        }

        // Convert to const char* array for ImGui
        static std::vector<const char*> displayPtrs;
        if (displayPtrs.empty()) {
            displayPtrs.reserve(displayStrings.size());
            for (const auto& str : displayStrings) {
                displayPtrs.push_back(str.c_str());
            }
        }

        ImGui::Combo("Parameter", &selectedParamIndex, displayPtrs.data(), displayPtrs.size());

        static float minVal = 0.0f;
        static float maxVal = 1.0f;
        ImGui::DragFloat("Min", &minVal, 0.01f);
        ImGui::DragFloat("Max", &maxVal, 0.01f);

        static bool invert = false;
        ImGui::Checkbox("Invert", &invert);

        if (ImGui::Button("Assign Mapping")) {
            oscSystem.addMapping(msg.address, parameters[selectedParamIndex].name, minVal, maxVal, invert);
            std::cout << "[UI] Assigned OSC address " << msg.address << " to " << parameters[selectedParamIndex].name << std::endl;
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
    ImGui::Text("Send float values 0.0-1.0 for normalized control");

    ImGui::Separator();
    ImGui::Text("OSC Triggers (Buttons):");
    const auto& triggerMappings = oscSystem.getTriggerMappings();
    if (ImGui::BeginTable("OscTriggers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Remove");
        ImGui::TableHeadersRow();

        for (const auto& [addr, mapping] : triggerMappings) {
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

    static const char* actions[] = {
        "randomizeVideo",
        "jumpRandom",
        "folderChanged",
        "applyChanges"
    };
    static int selectedAction = 0;
    ImGui::Combo("Action", &selectedAction, actions, IM_ARRAYSIZE(actions));

    if (ImGui::Button("Add Trigger")) {
        oscSystem.addTriggerMapping(triggerAddress, actions[selectedAction]);
        std::cout << "[UI] Added OSC trigger " << triggerAddress << " -> " << actions[selectedAction] << std::endl;
    }

    ImGui::Separator();
    ImGui::Text("Trigger Examples:");
    ImGui::Text("/vjay/randomize (no arguments)");
    ImGui::Text("/vjay/jump (no arguments)");
    ImGui::Text("Send messages without arguments for triggers");

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

    ImGui::End();
}
