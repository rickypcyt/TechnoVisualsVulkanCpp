// Application.cpp — refactorizado completo

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "Application.h"
#include "parameters/VisualControlsRegistry.h"
#include "parameters/JsonSerializer.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>
#include <future>
#include <thread>
#include <functional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#endif
#include "imgui.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers internos
// ─────────────────────────────────────────────────────────────────────────────

glm::ivec2 Application::getScreenSize() const {
    auto ext = previewPresenter.getExtent();
    return glm::ivec2(static_cast<int>(ext.width), static_cast<int>(ext.height));
}

// Sincroniza todos los descriptor sets con el estado actual de las texturas.
void Application::updateAllDescriptorSets() {
    const bool vid1Ready = videoTexture.isReady();
    const bool outVid1Ready = outputVideoTexture.isReady();

    // Descriptor sets principales (multipass) — only bind image if texture is ready
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            uniformBufferManager.getBuffers()[i],
            vid1Ready ? const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()) : nullptr,
            vid1Ready ? const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo()) : nullptr
        );
    }
    // Output descriptor sets
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        outputDescriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            outputUniformBufferManager.getBuffers()[i],
            outVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputVideoTexture.getDescriptorInfo()) : nullptr,
            outVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputVideoTexture.getPrevDescriptorInfo()) : nullptr
        );
    }
    updateFullscreenDescriptorSets();

    // Multipass pipeline — skip if texture not ready OR pipeline not yet initialized
    if (vid1Ready && multiPassPipeline.isInitialized()) {
        const bool has2 = videoTexture2.isReady();
        const bool has3 = videoTexture3.isReady();
        multiPassPipeline.updateDescriptorSets(
            uniformBufferManager.getBuffers(),
            videoTexture.getImageView(),
            videoTexture.getPrevImageView(),
            videoTexture.getSampler(),
            videoTexture.getSampler(),
            has2 ? videoTexture2.getImageView() : videoTexture.getImageView(),
            has2 ? videoTexture2.getSampler()   : videoTexture.getSampler(),
            has3 ? videoTexture3.getImageView() : videoTexture.getImageView(),
            has3 ? videoTexture3.getSampler()   : videoTexture.getSampler()
        );
    }
    if (outVid1Ready && outputMultiPassPipeline.isInitialized()) {
        const bool outputHas2 = outputVideoTexture2.isReady();
        const bool outputHas3 = outputVideoTexture3.isReady();
        outputMultiPassPipeline.updateDescriptorSets(
            outputUniformBufferManager.getBuffers(),
            outputVideoTexture.getImageView(),
            outputVideoTexture.getPrevImageView(),
            outputVideoTexture.getSampler(),
            outputVideoTexture.getSampler(),
            outputHas2 ? outputVideoTexture2.getImageView() : outputVideoTexture.getImageView(),
            outputHas2 ? outputVideoTexture2.getSampler()   : outputVideoTexture.getSampler(),
            outputHas3 ? outputVideoTexture3.getImageView() : outputVideoTexture.getImageView(),
            outputHas3 ? outputVideoTexture3.getSampler()   : outputVideoTexture.getSampler()
        );
    }
}

// Reconstruye GPU texture + CPU pool para un slot ya inicializado.
void Application::rebuildVideoTexture(int slot) {
    vkDeviceWaitIdle(vulkanContext.getDevice());
    if (slot == 0) {
        videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture.cleanup(resourceSystem);
        videoTexture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(videoPlayer.width()),
            static_cast<uint32_t>(videoPlayer.height())
        );
        cpuFramePool.resize(
            static_cast<uint32_t>(videoPlayer.width()),
            static_cast<uint32_t>(videoPlayer.height()),
            static_cast<uint32_t>(videoPlayer.width()) * 4
        );
        if (videoRenderer) videoRenderer->reset();
    } else if (slot == 1) {
        videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture2.cleanup(resourceSystem);
        videoTexture2.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(videoPlayer2.width()),
            static_cast<uint32_t>(videoPlayer2.height())
        );
        cpuFramePool2.resize(
            static_cast<uint32_t>(videoPlayer2.width()),
            static_cast<uint32_t>(videoPlayer2.height()),
            static_cast<uint32_t>(videoPlayer2.width()) * 4
        );
        if (videoRenderer2) videoRenderer2->reset();
    } else {
        videoTexture3.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture3.cleanup(resourceSystem);
        videoTexture3.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(videoPlayer3.width()),
            static_cast<uint32_t>(videoPlayer3.height())
        );
        cpuFramePool3.resize(
            static_cast<uint32_t>(videoPlayer3.width()),
            static_cast<uint32_t>(videoPlayer3.height()),
            static_cast<uint32_t>(videoPlayer3.width()) * 4
        );
        if (videoRenderer3) videoRenderer3->reset();
    }
}

// Recarga un slot completo desde una ruta dada.
bool Application::reloadVideoSlot(int slot, const std::string& path) {
    std::cout << "[reloadVideoSlot] Reloading slot " << slot << " with: " << path << "\n";

    // Restore per-video playback speed
    auto restoreSpeed = [&](int targetSlot, const std::string& videoPath) {
        auto it = videoSpeedCache.find(videoPath);
        float speed = (it != videoSpeedCache.end()) ? it->second : 1.0f;
        if (targetSlot == 0) {
            visualControls.playback.videoPlaybackRate = speed;
            lastPlaybackRate = speed;
        } else if (targetSlot == 1) {
            visualControls.playback.video2PlaybackRate = speed;
            lastPlaybackRate2 = speed;
        } else {
            visualControls.playback.video3PlaybackRate = speed;
            lastPlaybackRate3 = speed;
        }
    };

    // Start crossfade transition only if a previous video was loaded
    // (avoids sampling an uninitialized prev texture on first load)
    if (slot == 0 && videoTexture.isReady()) {
        transitionActive = true;
        transitionProgress = 0.0f;
        videoTexture.setFreezePrev(true);
    } else if (slot == 1 && videoTexture2.isReady()) {
        transitionActive = true;
        transitionProgress = 0.0f;
        videoTexture2.setFreezePrev(true);
    } else if (slot == 2 && videoTexture3.isReady()) {
        transitionActive = true;
        transitionProgress = 0.0f;
        videoTexture3.setFreezePrev(true);
    }

    if (slot == 0) {
        videoSourcePath = path;
        videoRenderer.reset();
        videoPlayer.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!videoPlayer.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadVideoSlot(0) failed: " << path << '\n';
            isReloadingVideo = false;
            return false;
        }

        videoPlayer.setAutoScale(visualControls.playback.autoScaleVideo);
        videoPlayer.setPlaybackRate(visualControls.playback.videoPlaybackRate);
        rebuildVideoTexture(0);
        updateAllDescriptorSets();

        videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);
        videoRandomizer.elapsedSeconds = 0.0f;
        videoRandomizer.currentVideoDuration = videoPlayer.durationSeconds();
        videoSubsystemInitialized = videoTexture.isReady();
    } else if (slot == 1) {
        videoSourcePath2 = path;
        videoRenderer2.reset();
        videoPlayer2.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!videoPlayer2.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadVideoSlot(1) failed: " << path << '\n';
            isReloadingVideo = false;
            return false;
        }

        videoPlayer2.setAutoScale(visualControls.playback.autoScaleVideo);
        videoPlayer2.setPlaybackRate(visualControls.playback.video2PlaybackRate);
        rebuildVideoTexture(1);
        updateAllDescriptorSets();

        videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
        videoRandomizer2.elapsedSeconds = 0.0f;
        videoRandomizer2.currentVideoDuration = videoPlayer2.durationSeconds();
        videoSubsystemInitialized2 = videoTexture2.isReady();
    } else {
        videoSourcePath3 = path;
        videoRenderer3.reset();
        videoPlayer3.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!videoPlayer3.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadVideoSlot(2) failed: " << path << '\n';
            isReloadingVideo3 = false;
            return false;
        }

        videoPlayer3.setAutoScale(visualControls.playback.autoScaleVideo);
        videoPlayer3.setPlaybackRate(visualControls.playback.video3PlaybackRate);
        rebuildVideoTexture(2);
        updateAllDescriptorSets();

        videoRenderer3 = std::make_unique<VideoRenderer>(videoPlayer3, videoTexture3, cpuFramePool3);
        videoRandomizer3.elapsedSeconds = 0.0f;
        videoRandomizer3.currentVideoDuration = videoPlayer3.durationSeconds();
        videoSubsystemInitialized3 = videoTexture3.isReady();
    }

    restoreSpeed(slot, path);

    if (slot == 2) isReloadingVideo3 = false;
    else isReloadingVideo = false;
    return true;
}

// Recarga un slot del output desde una ruta dada.
bool Application::reloadOutputVideoSlot(int slot, const std::string& path) {
    std::cout << "[reloadOutputVideoSlot] Reloading output slot " << slot << " with: " << path << "\n";

    if (slot == 0) {
        outputVideoSourcePath = path;
        outputVideoRenderer.reset();
        outputVideoPlayer.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!outputVideoPlayer.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadOutputVideoSlot(0) failed: " << path << '\n';
            outputIsReloadingVideo = false;
            return false;
        }

        outputVideoPlayer.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        outputVideoPlayer.setPlaybackRate(outputVisualControls.playback.videoPlaybackRate);

        // Rebuild texture
        vkDeviceWaitIdle(vulkanContext.getDevice());
        outputVideoTexture.destroy(resourceSystem, vulkanContext.getDevice());
        outputVideoTexture.cleanup(resourceSystem);
        outputVideoTexture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(outputVideoPlayer.width()),
            static_cast<uint32_t>(outputVideoPlayer.height()));
        outputCpuFramePool.resize(
            static_cast<uint32_t>(outputVideoPlayer.width()),
            static_cast<uint32_t>(outputVideoPlayer.height()),
            static_cast<uint32_t>(outputVideoPlayer.width()) * 4);
        if (outputVideoRenderer) outputVideoRenderer->reset();

        updateAllDescriptorSets();

        outputVideoRenderer = std::make_unique<VideoRenderer>(outputVideoPlayer, outputVideoTexture, outputCpuFramePool);
        outputVideoRandomizer.elapsedSeconds = 0.0f;
        outputVideoRandomizer.currentVideoDuration = outputVideoPlayer.durationSeconds();
        outputVideoSubsystemInitialized = outputVideoTexture.isReady();
    } else if (slot == 1) {
        outputVideoSourcePath2 = path;
        outputVideoRenderer2.reset();
        outputVideoPlayer2.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!outputVideoPlayer2.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadOutputVideoSlot(1) failed: " << path << '\n';
            outputIsReloadingVideo2 = false;
            return false;
        }

        outputVideoPlayer2.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        outputVideoPlayer2.setPlaybackRate(outputVisualControls.playback.video2PlaybackRate);

        vkDeviceWaitIdle(vulkanContext.getDevice());
        outputVideoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
        outputVideoTexture2.cleanup(resourceSystem);
        outputVideoTexture2.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(outputVideoPlayer2.width()),
            static_cast<uint32_t>(outputVideoPlayer2.height()));
        outputCpuFramePool2.resize(
            static_cast<uint32_t>(outputVideoPlayer2.width()),
            static_cast<uint32_t>(outputVideoPlayer2.height()),
            static_cast<uint32_t>(outputVideoPlayer2.width()) * 4);
        if (outputVideoRenderer2) outputVideoRenderer2->reset();

        updateAllDescriptorSets();

        outputVideoRenderer2 = std::make_unique<VideoRenderer>(outputVideoPlayer2, outputVideoTexture2, outputCpuFramePool2);
        outputVideoRandomizer2.elapsedSeconds = 0.0f;
        outputVideoRandomizer2.currentVideoDuration = outputVideoPlayer2.durationSeconds();
        outputVideoSubsystemInitialized2 = outputVideoTexture2.isReady();
    } else {
        outputVideoSourcePath3 = path;
        outputVideoRenderer3.reset();
        outputVideoPlayer3.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!outputVideoPlayer3.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadOutputVideoSlot(2) failed: " << path << '\n';
            outputIsReloadingVideo3 = false;
            return false;
        }

        outputVideoPlayer3.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        outputVideoPlayer3.setPlaybackRate(outputVisualControls.playback.video3PlaybackRate);

        vkDeviceWaitIdle(vulkanContext.getDevice());
        outputVideoTexture3.destroy(resourceSystem, vulkanContext.getDevice());
        outputVideoTexture3.cleanup(resourceSystem);
        outputVideoTexture3.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(outputVideoPlayer3.width()),
            static_cast<uint32_t>(outputVideoPlayer3.height()));
        outputCpuFramePool3.resize(
            static_cast<uint32_t>(outputVideoPlayer3.width()),
            static_cast<uint32_t>(outputVideoPlayer3.height()),
            static_cast<uint32_t>(outputVideoPlayer3.width()) * 4);
        if (outputVideoRenderer3) outputVideoRenderer3->reset();

        updateAllDescriptorSets();

        outputVideoRenderer3 = std::make_unique<VideoRenderer>(outputVideoPlayer3, outputVideoTexture3, outputCpuFramePool3);
        outputVideoRandomizer3.elapsedSeconds = 0.0f;
        outputVideoRandomizer3.currentVideoDuration = outputVideoPlayer3.durationSeconds();
        outputVideoSubsystemInitialized3 = outputVideoTexture3.isReady();
    }

    if (slot == 0) outputIsReloadingVideo = false;
    else if (slot == 1) outputIsReloadingVideo2 = false;
    else outputIsReloadingVideo3 = false;
    return true;
}

void Application::saveVideoSpeeds() const {
    try {
        nlohmann::json j;
        for (const auto& [k, v] : videoSpeedCache) j[k] = v;
        std::ofstream f(videoSpeedCachePath);
        f << j.dump(2);
    } catch (...) {}
}

void Application::loadVideoSpeeds() {
    try {
        std::ifstream f(videoSpeedCachePath);
        if (!f.is_open()) return;
        nlohmann::json j; f >> j;
        videoSpeedCache.clear();
        for (auto& [k, v] : j.items()) videoSpeedCache[k] = v.get<float>();
    } catch (...) {}
}

void Application::saveState() const {
    JsonSerializer::save(visualControlsPath, parameterRegistry);
    ControlState::save(
        controlStatePath,
        static_cast<const VideoRandomizerState&>(videoRandomizer),
        static_cast<const VideoRandomizerState2&>(videoRandomizer2),
        static_cast<const VideoRandomizerState2&>(videoRandomizer3),
        allowDimensionChangeRecreation,
        oscSystem,
        selectedVideoAsset,
        selectedVideoAsset2,
        selectedVideoAsset3,
        videoSourcePath,
        videoSourcePath2,
        videoSourcePath3,
        videoAssetsRoot
    );
    saveVideoSpeeds();
}

std::vector<std::string> Application::listPresets() const {
    std::vector<std::string> names;
    if (!std::filesystem::exists(presetsDir)) return names;
    for (const auto& entry : std::filesystem::directory_iterator(presetsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Application::savePreset(const std::string& name) {
    std::filesystem::create_directories(presetsDir);
    std::string path = presetsDir + "/" + name + ".json";

    nlohmann::json j;
    j["version"] = 2;
    j["visualControls"] = JsonSerializer::toJson(parameterRegistry);

    nlohmann::json video;
    video["videoSourcePath"] = videoSourcePath;
    video["videoSourcePath2"] = videoSourcePath2;
    video["videoSourcePath3"] = videoSourcePath3;
    video["selectedVideoAsset"] = selectedVideoAsset;
    video["selectedVideoAsset2"] = selectedVideoAsset2;
    video["selectedVideoAsset3"] = selectedVideoAsset3;
    video["allowDimensionChangeRecreation"] = allowDimensionChangeRecreation;

    nlohmann::json r1;
    r1["autoRandomize"] = videoRandomizer.autoRandomize;
    r1["useVideoDuration"] = videoRandomizer.useVideoDuration;
    r1["intervalSeconds"] = videoRandomizer.intervalSeconds;
    r1["useShuffleMode"] = videoRandomizer.useShuffleMode;
    video["randomizer1"] = r1;

    nlohmann::json r2;
    r2["autoRandomize"] = videoRandomizer2.autoRandomize;
    r2["useVideoDuration"] = videoRandomizer2.useVideoDuration;
    r2["intervalSeconds"] = videoRandomizer2.intervalSeconds;
    r2["useShuffleMode"] = videoRandomizer2.useShuffleMode;
    video["randomizer2"] = r2;

    nlohmann::json r3;
    r3["autoRandomize"] = videoRandomizer3.autoRandomize;
    r3["useVideoDuration"] = videoRandomizer3.useVideoDuration;
    r3["intervalSeconds"] = videoRandomizer3.intervalSeconds;
    r3["useShuffleMode"] = videoRandomizer3.useShuffleMode;
    video["randomizer3"] = r3;

    j["video"] = video;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(4);
    return true;
}

bool Application::loadPreset(const std::string& name) {
    std::string path = presetsDir + "/" + name + ".json";
    if (!std::filesystem::exists(path)) return false;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        std::cerr << "[Preset] Invalid JSON: " << path << std::endl;
        return false;
    }

    // Load visual controls
    if (j.contains("visualControls")) {
        JsonSerializer::fromJson(j["visualControls"], parameterRegistry);
    } else if (j.contains("version")) {
        // Legacy v1 preset: root is the visual controls
        JsonSerializer::fromJson(j, parameterRegistry);
    }

    // Load video state
    if (j.contains("video")) {
        auto& v = j["video"];
        if (v.contains("videoSourcePath")) videoSourcePath = v["videoSourcePath"];
        if (v.contains("videoSourcePath2")) videoSourcePath2 = v["videoSourcePath2"];
        if (v.contains("videoSourcePath3")) videoSourcePath3 = v["videoSourcePath3"];
        if (v.contains("selectedVideoAsset")) selectedVideoAsset = v["selectedVideoAsset"];
        if (v.contains("selectedVideoAsset2")) selectedVideoAsset2 = v["selectedVideoAsset2"];
        if (v.contains("selectedVideoAsset3")) selectedVideoAsset3 = v["selectedVideoAsset3"];
        if (v.contains("allowDimensionChangeRecreation")) allowDimensionChangeRecreation = v["allowDimensionChangeRecreation"];

        if (v.contains("randomizer1")) {
            auto& r1 = v["randomizer1"];
            if (r1.contains("autoRandomize")) videoRandomizer.autoRandomize = r1["autoRandomize"];
            if (r1.contains("useVideoDuration")) videoRandomizer.useVideoDuration = r1["useVideoDuration"];
            if (r1.contains("intervalSeconds")) videoRandomizer.intervalSeconds = r1["intervalSeconds"];
            if (r1.contains("useShuffleMode")) videoRandomizer.useShuffleMode = r1["useShuffleMode"];
        }
        if (v.contains("randomizer2")) {
            auto& r2 = v["randomizer2"];
            if (r2.contains("autoRandomize")) videoRandomizer2.autoRandomize = r2["autoRandomize"];
            if (r2.contains("useVideoDuration")) videoRandomizer2.useVideoDuration = r2["useVideoDuration"];
            if (r2.contains("intervalSeconds")) videoRandomizer2.intervalSeconds = r2["intervalSeconds"];
            if (r2.contains("useShuffleMode")) videoRandomizer2.useShuffleMode = r2["useShuffleMode"];
        }
        if (v.contains("randomizer3")) {
            auto& r3 = v["randomizer3"];
            if (r3.contains("autoRandomize")) videoRandomizer3.autoRandomize = r3["autoRandomize"];
            if (r3.contains("useVideoDuration")) videoRandomizer3.useVideoDuration = r3["useVideoDuration"];
            if (r3.contains("intervalSeconds")) videoRandomizer3.intervalSeconds = r3["intervalSeconds"];
            if (r3.contains("useShuffleMode")) videoRandomizer3.useShuffleMode = r3["useShuffleMode"];
        }
    }

    // Reload videos to match preset state
    if (canChangeVideo()) {
        if (!videoSourcePath.empty()) reloadVideoSlot(0, videoSourcePath);
        if (!videoSourcePath2.empty()) reloadVideoSlot(1, videoSourcePath2);
        if (!videoSourcePath3.empty()) reloadVideoSlot(2, videoSourcePath3);
    }

    controlsDirty = true;
    return true;
}

bool Application::deletePreset(const std::string& name) {
    std::string path = presetsDir + "/" + name + ".json";
    if (!std::filesystem::exists(path)) return false;
    return std::filesystem::remove(path);
}

bool Application::renamePreset(const std::string& oldName, const std::string& newName) {
    std::string oldPath = presetsDir + "/" + oldName + ".json";
    std::string newPath = presetsDir + "/" + newName + ".json";
    if (!std::filesystem::exists(oldPath)) return false;
    if (std::filesystem::exists(newPath)) return false;
    std::filesystem::rename(oldPath, newPath);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Init
// ─────────────────────────────────────────────────────────────────────────────

Application::Application() {
    VisualControlsRegistry::build(parameterRegistry, visualControls);
    loadVideoSpeeds();
}

Application::~Application() {
    cleanup();
}

void Application::run() {
    window.initSDL();
    const float s = window.getDpiScale();
    window.createMainWindow("[PREVIEW] Vulkan", (int)(1280 * s), (int)(720 * s));
    SDL_SetWindowPosition(window.getMainWindow(), (int)(50 * s), (int)(50 * s));
    window.createOutputWindow("[OUTPUT] Final", (int)(1280 * s), (int)(720 * s));
    SDL_SetWindowPosition(window.getOutputWindow(), (int)(50 * s), (int)(820 * s));
    window.createUiWindow("[CONTROLS] UI", (int)(420 * s), (int)(720 * s));
    SDL_SetWindowPosition(window.getUiWindow(), (int)(1350 * s), (int)(50 * s));

    initVulkan();
    initPresenters();
    initRenderPass();

    previewPresenter.createFramebuffers(renderPass);
    outputPresenter.createFramebuffers(renderPass);

    descriptorSetManager.createLayout(vulkanContext.getDevice());
    descriptorSetManager.createPool(vulkanContext.getDevice());
    outputDescriptorSetManager.createLayout(vulkanContext.getDevice());
    outputDescriptorSetManager.createPool(vulkanContext.getDevice());
    uniformBufferManager.createBuffers(resourceSystem, vulkanContext.getDevice());
    outputUniformBufferManager.createBuffers(resourceSystem, vulkanContext.getDevice());

    // Cargar estado antes del vídeo para que selectedVideoFolder sea correcto
    JsonSerializer::load(visualControlsPath, parameterRegistry);
    ControlState::load(
        controlStatePath,
        videoRandomizer,
        videoRandomizer2,
        videoRandomizer3,
        allowDimensionChangeRecreation,
        oscSystem,
        selectedVideoAsset,
        selectedVideoAsset2,
        selectedVideoAsset3,
        videoSourcePath,
        videoSourcePath2,
        videoSourcePath3,
        videoAssetsRoot
    );

    // Sync loaded preview state to output so the final renderer starts
    // with the same configuration as the saved session.
    outputVisualControls = visualControls;
    outputVideoSourcePath = videoSourcePath;
    outputVideoSourcePath2 = videoSourcePath2;
    outputVideoSourcePath3 = videoSourcePath3;
    outputSelectedVideoAsset = selectedVideoAsset;
    outputSelectedVideoAsset2 = selectedVideoAsset2;
    outputSelectedVideoAsset3 = selectedVideoAsset3;

    initVideo();
    initOutputVideo();

    descriptorSetManager.createSets(vulkanContext.getDevice());
    outputDescriptorSetManager.createSets(vulkanContext.getDevice());
    const bool initVid1Ready = videoTexture.isReady();
    const bool initOutVid1Ready = outputVideoTexture.isReady();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            uniformBufferManager.getBuffers()[i],
            initVid1Ready ? const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()) : nullptr,
            initVid1Ready ? const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo()) : nullptr
        );
        outputDescriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            outputUniformBufferManager.getBuffers()[i],
            initOutVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputVideoTexture.getDescriptorInfo()) : nullptr,
            initOutVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputVideoTexture.getPrevDescriptorInfo()) : nullptr
        );
    }

    initPipelines();
    updateFullscreenDescriptorSets();
    initUI();
    initNLE();
    initMultiPassPipeline();
    initMidi();
    initOsc();
    initAudio();
    initCommandBuffers();

    previewFrameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT,
                            static_cast<uint32_t>(previewPresenter.getImageCount()));
    outputFrameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT,
                           static_cast<uint32_t>(outputPresenter.getImageCount()));
    std::cout << "[Application] FrameSystems initialized\n";

    const auto now   = std::chrono::steady_clock::now();
    startTime        = now;
    lastControlSaveTime  = now;
    lastFrameTimestamp   = now;
    lastRandomJumpTime   = now;
    initializationComplete = true;

    mainLoop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Vulkan init helpers (sin cambios funcionales, solo limpieza menor)
// ─────────────────────────────────────────────────────────────────────────────

void Application::initVulkan() {
#ifdef NDEBUG
    constexpr bool enableValidation = false;
#else
    constexpr bool enableValidation = true;
#endif
    vulkanContext.init(window.getMainWindow(), enableValidation);

    VkSurfaceKHR tempSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window.getMainWindow(), vulkanContext.getInstance(), &tempSurface)) {
        throw std::runtime_error(std::string("failed to create temp surface: ") + SDL_GetError());
    }
    vulkanContext.selectDevice(tempSurface);
    vkDestroySurfaceKHR(vulkanContext.getInstance(), tempSurface, nullptr);

    vulkanContext.createCommandPool();
    resourceSystem.init(vulkanContext.getDevice(), vulkanContext.getPhysicalDevice());
}

void Application::initPresenters() {
    previewPresenter.init(vulkanContext.getInstance(), vulkanContext.getPhysicalDevice(),
                          vulkanContext.getDevice(), window.getMainWindow(), 854, 480);
    outputPresenter.init(vulkanContext.getInstance(), vulkanContext.getPhysicalDevice(),
                         vulkanContext.getDevice(), window.getOutputWindow(), 1920, 1080);
}

void Application::initRenderPass() {
    VkAttachmentDescription color{};
    color.format         = previewPresenter.getFormat();
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkRenderPassCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments    = &color;
    info.subpassCount    = 1;
    info.pSubpasses      = &subpass;

    if (vkCreateRenderPass(vulkanContext.getDevice(), &info, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("failed to create render pass");
}

void Application::initFramebuffers() {
    // Framebuffers now live inside VulkanPresenter instances
}

void Application::initPipelines() {
    // Create fullscreen descriptor set layout (only 2 bindings: UBO + 1 sampler)
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> fullscreenBindings = {uboLayoutBinding, samplerBinding};
    VkDescriptorSetLayoutCreateInfo fullscreenLayoutInfo{};
    fullscreenLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    fullscreenLayoutInfo.bindingCount = static_cast<uint32_t>(fullscreenBindings.size());
    fullscreenLayoutInfo.pBindings = fullscreenBindings.data();

    if (vkCreateDescriptorSetLayout(vulkanContext.getDevice(), &fullscreenLayoutInfo, nullptr, &fullscreenDescriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fullscreen descriptor set layout");
    }

    // Create fullscreen descriptor pool
    std::array<VkDescriptorPoolSize, 2> fullscreenPoolSizes{};
    fullscreenPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fullscreenPoolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    fullscreenPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    fullscreenPoolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo fullscreenPoolInfo{};
    fullscreenPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    fullscreenPoolInfo.poolSizeCount = static_cast<uint32_t>(fullscreenPoolSizes.size());
    fullscreenPoolInfo.pPoolSizes = fullscreenPoolSizes.data();
    fullscreenPoolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(vulkanContext.getDevice(), &fullscreenPoolInfo, nullptr, &fullscreenDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fullscreen descriptor pool");
    }

    // Allocate fullscreen descriptor sets
    std::vector<VkDescriptorSetLayout> fullscreenLayouts(MAX_FRAMES_IN_FLIGHT, fullscreenDescriptorSetLayout);
    VkDescriptorSetAllocateInfo fullscreenAllocInfo{};
    fullscreenAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    fullscreenAllocInfo.descriptorPool = fullscreenDescriptorPool;
    fullscreenAllocInfo.descriptorSetCount = static_cast<uint32_t>(fullscreenLayouts.size());
    fullscreenAllocInfo.pSetLayouts = fullscreenLayouts.data();

    fullscreenDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(vulkanContext.getDevice(), &fullscreenAllocInfo, fullscreenDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate fullscreen descriptor sets");
    }

    // Output fullscreen descriptor pool (same layout, separate pool)
    VkDescriptorPoolCreateInfo outputPoolInfo{};
    outputPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    outputPoolInfo.poolSizeCount = static_cast<uint32_t>(fullscreenPoolSizes.size());
    outputPoolInfo.pPoolSizes = fullscreenPoolSizes.data();
    outputPoolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

    if (vkCreateDescriptorPool(vulkanContext.getDevice(), &outputPoolInfo, nullptr, &outputFullscreenDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create output fullscreen descriptor pool");
    }

    std::vector<VkDescriptorSetLayout> outputLayouts(MAX_FRAMES_IN_FLIGHT, fullscreenDescriptorSetLayout);
    VkDescriptorSetAllocateInfo outputAllocInfo{};
    outputAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    outputAllocInfo.descriptorPool = outputFullscreenDescriptorPool;
    outputAllocInfo.descriptorSetCount = static_cast<uint32_t>(outputLayouts.size());
    outputAllocInfo.pSetLayouts = outputLayouts.data();

    outputFullscreenDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(vulkanContext.getDevice(), &outputAllocInfo, outputFullscreenDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate output fullscreen descriptor sets");
    }

    // Create pipeline layout for fullscreen using the fullscreen descriptor set layout
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(int);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &fullscreenDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(vulkanContext.getDevice(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout");
    }

    // Create fullscreen pipeline
    auto vertShaderCode = ShaderCompiler::loadFromFile("shaders/fullscreen.vert.spv");
    auto fragShaderCode = ShaderCompiler::loadFromFile("shaders/fullscreen.frag.spv");

    VkShaderModule vertShaderModule = ShaderCompiler::createModule(vulkanContext.getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = ShaderCompiler::createModule(vulkanContext.getDevice(), fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(previewPresenter.getExtent().width);
    viewport.height = static_cast<float>(previewPresenter.getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = previewPresenter.getExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic states
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(vulkanContext.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &fullscreenPipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fullscreen pipeline");
    }

    vkDestroyShaderModule(vulkanContext.getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(vulkanContext.getDevice(), vertShaderModule, nullptr);

    // Create swapchain sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(vulkanContext.getDevice(), &samplerInfo, nullptr, &swapchainSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swapchain sampler");
    }
}

void Application::updateFullscreenDescriptorSets() {
    if (fullscreenDescriptorSets.size() != MAX_FRAMES_IN_FLIGHT) {
        return; // Descriptor sets not yet created
    }

    const auto& previewBuffers = uniformBufferManager.getBuffers();
    if (previewBuffers.size() != MAX_FRAMES_IN_FLIGHT) {
        std::cerr << "[Application] updateFullscreenDescriptorSets: preview UBO buffers not ready\n";
        return;
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    bufferInfos.reserve(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> outputBufferInfos;
    outputBufferInfos.reserve(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(MAX_FRAMES_IN_FLIGHT * 2);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (previewBuffers[i] == VK_NULL_HANDLE) {
            std::cerr << "[Application] updateFullscreenDescriptorSets: preview UBO buffer " << i << " is null\n";
            continue;
        }
        VkDescriptorBufferInfo& bufferInfo = bufferInfos.emplace_back();
        bufferInfo.buffer = previewBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalParamsUBO);

        VkWriteDescriptorSet& uboWrite = writes.emplace_back();
        uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboWrite.dstSet = fullscreenDescriptorSets[i];
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &bufferInfos.back();
    }

    // Output fullscreen descriptor sets (output UBOs)
    if (outputFullscreenDescriptorSets.size() == MAX_FRAMES_IN_FLIGHT) {
        const auto& outputBuffers = outputUniformBufferManager.getBuffers();
        if (outputBuffers.size() != MAX_FRAMES_IN_FLIGHT) {
            std::cerr << "[Application] updateFullscreenDescriptorSets: output UBO buffers not ready\n";
        } else {
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                if (outputBuffers[i] == VK_NULL_HANDLE) {
                    std::cerr << "[Application] updateFullscreenDescriptorSets: output UBO buffer " << i << " is null\n";
                    continue;
                }
                VkDescriptorBufferInfo& bufferInfo = outputBufferInfos.emplace_back();
                bufferInfo.buffer = outputBuffers[i];
                bufferInfo.offset = 0;
                bufferInfo.range = sizeof(GlobalParamsUBO);

                VkWriteDescriptorSet& uboWrite = writes.emplace_back();
                uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                uboWrite.dstSet = outputFullscreenDescriptorSets[i];
                uboWrite.dstBinding = 0;
                uboWrite.dstArrayElement = 0;
                uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                uboWrite.descriptorCount = 1;
                uboWrite.pBufferInfo = &bufferInfo;
            }
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(vulkanContext.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    // Binding 1 (inputTexture) lo actualiza execute() cada frame
    // apuntando al output final del multipass pipeline
}

void Application::initCommandBuffers() {
    previewCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    outputCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = vulkanContext.getCommandPool();
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = static_cast<uint32_t>(previewCommandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanContext.getDevice(), &info, previewCommandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate preview command buffers");

    info.commandBufferCount = static_cast<uint32_t>(outputCommandBuffers.size());
    if (vkAllocateCommandBuffers(vulkanContext.getDevice(), &info, outputCommandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate output command buffers");
}

// ─────────────────────────────────────────────────────────────────────────────
// Video init
// ─────────────────────────────────────────────────────────────────────────────

void Application::initVideo() {
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    std::cout << "[Application] initVideo() START\n";
    std::cout << "[initVideo] videoAssetsRoot='" << videoAssetsRoot << "'\n";
    std::cout << "[initVideo] videoAssetsRoot exists=" << std::filesystem::exists(videoAssetsRoot)
              << " is_directory=" << std::filesystem::is_directory(videoAssetsRoot) << "\n";

    std::cout << "[initVideo] about to call videoRegistry.scan()...\n";
    std::cout.flush();
    auto tScan = Clock::now();
    videoRegistry.scan(videoAssetsRoot);
    auto tScanDone = Clock::now();
    std::cout << "[initVideo] videoRegistry.scan() returned\n";
    std::cout << "[initVideo] Registry scan: "
              << std::chrono::duration<double>(tScanDone - tScan).count()
              << "s, assets=" << videoRegistry.getAssets().size() << "\n";
    for (const auto& asset : videoRegistry.getAssets()) {
        std::cout << "[initVideo]   asset: '" << asset.metadata.filename
                  << "' folder='" << std::filesystem::path(asset.metadata.path).parent_path().string()
                  << "'\n";
    }
    glm::ivec2 screenSize = getScreenSize();
    // Decode preview videos to the preview window size, not full screen.
    // Preview is only 480p, so decoding to 1080p wastes a lot of startup time.
    glm::ivec2 previewSize = previewPresenter.getExtent().width > 0 && previewPresenter.getExtent().height > 0
                             ? glm::ivec2(previewPresenter.getExtent().width, previewPresenter.getExtent().height)
                             : screenSize;

    // ── Resolve paths ───────────────────────────────────────────────────────
    // Prefer saved paths; validate against the whole registry, then locate
    // inside the current folder.  If the path exists globally but not in the
    // selected folder we keep the path (video will load) and reset the index.
    auto resolvePath = [&](const std::string& folder,
                          int& selAsset,
                          std::string& path,
                          const char* slotName)
    {
        std::cout << "[initVideo] resolvePath " << slotName
                  << " folder='" << folder << "' selAsset=" << selAsset
                  << " path='" << path << "'\n";

        bool pathValid = false;
        if (!path.empty()) {
            for (const auto& asset : videoRegistry.getAssets()) {
                if (asset.metadata.path == path) { pathValid = true; break; }
            }
        }
        std::cout << "[initVideo] resolvePath " << slotName
                  << " pathValid=" << pathValid << "\n";

        const auto& assets = videoRegistry.getFilteredAssets(folder);
        std::cout << "[initVideo] resolvePath " << slotName
                  << " filteredAssets=" << assets.size() << "\n";

        if (!assets.empty()) {
            if (pathValid) {
                for (int i = 0; i < (int)assets.size(); ++i) {
                    if (assets[i].metadata.path == path) {
                        selAsset = i;
                        std::cout << "[initVideo] Path found in folder '" << folder
                                  << "' at index " << i << ": " << path << "\n";
                        return;
                    }
                }
                std::cout << "[initVideo] Path valid globally but not in folder '"
                          << folder << "', keeping path: " << path
                          << " (reset selAsset from " << selAsset << " to 0)\n";
                selAsset = 0;
                return;
            }
            std::cout << "[initVideo] Path invalid ('" << path
                      << "'), falling back to selAsset=" << selAsset
                      << " in folder '" << folder << "'\n";
            selAsset = std::clamp(selAsset, 0, (int)assets.size() - 1);
            path = assets[selAsset].metadata.path;
            std::cout << "[initVideo] Fallback path: " << path << "\n";
        } else {
            std::cout << "[initVideo] Folder '" << folder << "' is empty -> " << slotName << " path remains empty\n";
        }
    };

    std::cout << "[initVideo] Before resolve: V1 path=" << videoSourcePath
              << " idx=" << selectedVideoAsset << " folder="
              << visualControls.playback.selectedVideoFolder << "\n";
    std::cout << "[initVideo] Before resolve: V2 path=" << videoSourcePath2
              << " idx=" << selectedVideoAsset2 << " folder="
              << visualControls.playback.selectedVideo2Folder << "\n";
    std::cout << "[initVideo] Before resolve: V3 path=" << videoSourcePath3
              << " idx=" << selectedVideoAsset3 << " folder="
              << visualControls.playback.selectedVideo3Folder << "\n";

    resolvePath(visualControls.playback.selectedVideoFolder,
                selectedVideoAsset, videoSourcePath, "V1");
    resolvePath(visualControls.playback.selectedVideo2Folder,
                selectedVideoAsset2, videoSourcePath2, "V2");
    resolvePath(visualControls.playback.selectedVideo3Folder,
                selectedVideoAsset3, videoSourcePath3, "V3");

    std::cout << "[initVideo] After resolve:  V1 path=" << videoSourcePath
              << " idx=" << selectedVideoAsset << "\n";
    std::cout << "[initVideo] After resolve:  V2 path=" << videoSourcePath2
              << " idx=" << selectedVideoAsset2 << "\n";
    std::cout << "[initVideo] After resolve:  V3 path=" << videoSourcePath3
              << " idx=" << selectedVideoAsset3 << "\n";
    // ── Lazy init: only init slots 1 and 2 when dual video is enabled ────────────
    bool initExtraSlots = visualControls.playback.enableDualVideo;

    std::cout << "[initVideo] Preview decode size: " << previewSize.x << "x" << previewSize.y
              << " (screen=" << screenSize.x << "x" << screenSize.y << ")\n";
    std::cout << "[initVideo] enableDualVideo=" << visualControls.playback.enableDualVideo
              << " -> initExtraSlots=" << initExtraSlots << "\n";
    if (!initExtraSlots) {
        std::cout << "[initVideo] Skipping V2 and V3 player init because enableDualVideo is false\n";
    }

    // ── Parallel player initialization (heaviest part: disk + codecs) ──────
    auto tPlayer = Clock::now();

    std::cout << "[initVideo] Launching player init futures...\n";
    auto future0 = std::async(std::launch::async, [&]() {
        std::cout << "[initVideo] V1 initializing: '" << videoSourcePath << "'\n";
        return videoPlayer.initialize(videoSourcePath, previewSize.x, previewSize.y);
    });

    std::future<bool> future1;
    std::future<bool> future2;
    if (initExtraSlots) {
        future1 = std::async(std::launch::async, [&]() {
            std::cout << "[initVideo] V2 initializing: '" << videoSourcePath2 << "'\n";
            return videoPlayer2.initialize(videoSourcePath2, previewSize.x, previewSize.y);
        });
        future2 = std::async(std::launch::async, [&]() {
            std::cout << "[initVideo] V3 initializing: '" << videoSourcePath3 << "'\n";
            return videoPlayer3.initialize(videoSourcePath3, previewSize.x, previewSize.y);
        });
    }

    std::cout << "[initVideo] Waiting for V1 future...\n";
    bool ok0 = future0.get();
    std::cout << "[initVideo] V1 future returned: " << ok0 << "\n";

    bool ok1 = false;
    bool ok2 = false;
    if (initExtraSlots) {
        std::cout << "[initVideo] Waiting for V2 future...\n";
        ok1 = future1.get();
        std::cout << "[initVideo] V2 future returned: " << ok1 << "\n";
        std::cout << "[initVideo] Waiting for V3 future...\n";
        ok2 = future2.get();
        std::cout << "[initVideo] V3 future returned: " << ok2 << "\n";
    }

    auto tPlayerDone = Clock::now();
    std::cout << "[Application] Player init: "
              << std::chrono::duration<double>(tPlayerDone - tPlayer).count()
              << "s (slot0=" << ok0 << " slot1=" << ok1 << " slot2=" << ok2 << ")\n";
    if (ok0) {
        std::cout << "[initVideo] Slot0 loaded: " << videoSourcePath
                  << " | decode=" << videoPlayer.width() << "x" << videoPlayer.height()
                  << " | duration=" << videoPlayer.durationSeconds() << "s\n";
    } else {
        std::cerr << "[initVideo] Slot0 FAILED: " << videoSourcePath << "\n";
    }
    if (ok1) {
        std::cout << "[initVideo] Slot1 loaded: " << videoSourcePath2
                  << " | decode=" << videoPlayer2.width() << "x" << videoPlayer2.height()
                  << " | duration=" << videoPlayer2.durationSeconds() << "s\n";
    } else if (initExtraSlots) {
        std::cerr << "[initVideo] Slot1 FAILED: " << videoSourcePath2 << "\n";
    }
    if (ok2) {
        std::cout << "[initVideo] Slot2 loaded: " << videoSourcePath3
                  << " | decode=" << videoPlayer3.width() << "x" << videoPlayer3.height()
                  << " | duration=" << videoPlayer3.durationSeconds() << "s\n";
    } else if (initExtraSlots) {
        std::cerr << "[initVideo] Slot2 FAILED: " << videoSourcePath3 << "\n";
    }

    if (!ok0) {
        std::cerr << "[Application] Failed to initialize video player 1, aborting initVideo()\n";
        return;
    }
    std::cout << "[initVideo] V1 ok, configuring autoScale=" << visualControls.playback.autoScaleVideo << "\n";
    videoPlayer.setAutoScale(visualControls.playback.autoScaleVideo);

    // ── Vulkan resources (must be on main thread, but faster) ───────────────
    auto tVk = Clock::now();

    cpuFramePool.resize(videoPlayer.width(), videoPlayer.height(), videoPlayer.width() * 4);
    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                 vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                 videoPlayer.width(), videoPlayer.height());
    videoSubsystemInitialized = videoTexture.isReady();
    if (videoSubsystemInitialized)
        videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);

    if (initExtraSlots && ok1) {
        std::cout << "[initVideo] Setting up V2 Vulkan resources (" << videoPlayer2.width() << "x" << videoPlayer2.height() << ")\n";
        videoPlayer2.setAutoScale(visualControls.playback.autoScaleVideo);
        cpuFramePool2.resize(videoPlayer2.width(), videoPlayer2.height(), videoPlayer2.width() * 4);
        videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                      vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                      videoPlayer2.width(), videoPlayer2.height());
        videoSubsystemInitialized2 = videoTexture2.isReady();
        std::cout << "[initVideo] V2 texture ready=" << videoSubsystemInitialized2 << "\n";
        if (videoSubsystemInitialized2)
            videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
    } else {
        std::cout << "[initVideo] V2 skipped: initExtraSlots=" << initExtraSlots << " ok1=" << ok1 << "\n";
    }

    if (initExtraSlots && ok2) {
        std::cout << "[initVideo] Setting up V3 Vulkan resources (" << videoPlayer3.width() << "x" << videoPlayer3.height() << ")\n";
        videoPlayer3.setAutoScale(visualControls.playback.autoScaleVideo);
        cpuFramePool3.resize(videoPlayer3.width(), videoPlayer3.height(), videoPlayer3.width() * 4);
        videoTexture3.createResources(resourceSystem, vulkanContext.getDevice(),
                                      vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                      videoPlayer3.width(), videoPlayer3.height());
        videoSubsystemInitialized3 = videoTexture3.isReady();
        std::cout << "[initVideo] V3 texture ready=" << videoSubsystemInitialized3 << "\n";
        if (videoSubsystemInitialized3)
            videoRenderer3 = std::make_unique<VideoRenderer>(videoPlayer3, videoTexture3, cpuFramePool3);
    } else {
        std::cout << "[initVideo] V3 skipped: initExtraSlots=" << initExtraSlots << " ok2=" << ok2 << "\n";
    }

    auto tVkDone = Clock::now();
    std::cout << "[Application] Vulkan resources: "
              << std::chrono::duration<double>(tVkDone - tVk).count() << "s\n";
    std::cout << "[initVideo] Textures ready: v1=" << videoSubsystemInitialized
              << " v2=" << videoSubsystemInitialized2
              << " v3=" << videoSubsystemInitialized3 << "\n";

    auto t1 = Clock::now();
    std::cout << "[Application] initVideo() DONE — total="
              << std::chrono::duration<double>(t1 - t0).count()
              << "s v1=" << videoSubsystemInitialized
              << " v2=" << videoSubsystemInitialized2
              << " v3=" << videoSubsystemInitialized3 << '\n';
}

// ─────────────────────────────────────────────────────────────────────────────
// Output Video init (separate from preview)
// ─────────────────────────────────────────────────────────────────────────────

void Application::initOutputVideo() {
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    std::cout << "[Application] initOutputVideo()\n";
    glm::ivec2 screenSize = getScreenSize();

    // Copy paths from preview initially
    outputVideoSourcePath = videoSourcePath;
    outputVideoSourcePath2 = videoSourcePath2;
    outputVideoSourcePath3 = videoSourcePath3;
    outputSelectedVideoAsset = selectedVideoAsset;
    outputSelectedVideoAsset2 = selectedVideoAsset2;
    outputSelectedVideoAsset3 = selectedVideoAsset3;

    bool initExtraSlots = visualControls.playback.enableDualVideo;

    auto future0 = std::async(std::launch::async, [&]() {
        return outputVideoPlayer.initialize(outputVideoSourcePath, screenSize.x, screenSize.y);
    });

    std::future<bool> future1;
    std::future<bool> future2;
    if (initExtraSlots) {
        future1 = std::async(std::launch::async, [&]() {
            return outputVideoPlayer2.initialize(outputVideoSourcePath2, screenSize.x, screenSize.y);
        });
        future2 = std::async(std::launch::async, [&]() {
            return outputVideoPlayer3.initialize(outputVideoSourcePath3, screenSize.x, screenSize.y);
        });
    }

    bool ok0 = future0.get();
    bool ok1 = initExtraSlots ? future1.get() : false;
    bool ok2 = initExtraSlots ? future2.get() : false;

    if (!ok0) {
        std::cerr << "[Application] Failed to initialize output video player 1\n";
        return;
    }
    outputVideoPlayer.setAutoScale(visualControls.playback.autoScaleVideo);

    outputCpuFramePool.resize(outputVideoPlayer.width(), outputVideoPlayer.height(), outputVideoPlayer.width() * 4);
    outputVideoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                       vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                       outputVideoPlayer.width(), outputVideoPlayer.height());
    outputVideoSubsystemInitialized = outputVideoTexture.isReady();
    if (outputVideoSubsystemInitialized)
        outputVideoRenderer = std::make_unique<VideoRenderer>(outputVideoPlayer, outputVideoTexture, outputCpuFramePool);

    if (initExtraSlots && ok1) {
        outputVideoPlayer2.setAutoScale(visualControls.playback.autoScaleVideo);
        outputCpuFramePool2.resize(outputVideoPlayer2.width(), outputVideoPlayer2.height(), outputVideoPlayer2.width() * 4);
        outputVideoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                             outputVideoPlayer2.width(), outputVideoPlayer2.height());
        outputVideoSubsystemInitialized2 = outputVideoTexture2.isReady();
        if (outputVideoSubsystemInitialized2)
            outputVideoRenderer2 = std::make_unique<VideoRenderer>(outputVideoPlayer2, outputVideoTexture2, outputCpuFramePool2);
    }

    if (initExtraSlots && ok2) {
        outputVideoPlayer3.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        outputCpuFramePool3.resize(outputVideoPlayer3.width(), outputVideoPlayer3.height(), outputVideoPlayer3.width() * 4);
        outputVideoTexture3.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                             outputVideoPlayer3.width(), outputVideoPlayer3.height());
        outputVideoSubsystemInitialized3 = outputVideoTexture3.isReady();
        if (outputVideoSubsystemInitialized3)
            outputVideoRenderer3 = std::make_unique<VideoRenderer>(outputVideoPlayer3, outputVideoTexture3, outputCpuFramePool3);
    }

    auto t1 = Clock::now();
    std::cout << "[Application] initOutputVideo() done — total="
              << std::chrono::duration<double>(t1 - t0).count()
              << "s v1=" << outputVideoSubsystemInitialized
              << " v2=" << outputVideoSubsystemInitialized2
              << " v3=" << outputVideoSubsystemInitialized3 << '\n';
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem init
// ─────────────────────────────────────────────────────────────────────────────

void Application::initUI() {
    if (!uiSystem.initialize(window.getUiWindow(), window.getUiRenderer(), window.getDpiScale()))
        throw std::runtime_error("failed to initialize UI system");
}

void Application::initNLE() {
    g_project_state.active_file = videoSourcePath;
    renderWorker = std::make_unique<RenderWorker>();
    renderWorker->on_render_complete = [this](std::shared_ptr<RenderJob> job) {
        std::cout << "[Render] Completed: " << job->output_file << '\n';
        playbackClock.resume();
        if (job->do_swap) {
            std::lock_guard lock(completedRenderJobsMutex);
            completedRenderJobs.push(job);
        }
    };
}

void Application::initMidi() {
    if (!midiSystem.initialize()) {
        std::cerr << "[Application] Failed to initialize MIDI\n";
        return;
    }
    midiSystem.setEventCallback([this](const MidiMessage& msg) {
        midiSystem.applyToVisualControls(msg, visualControls);
    });
    midiSystem.setTriggerCallback([this](const std::string& action) {
        handleOscTrigger(action);
    });

    const unsigned int n = midiSystem.getPortCount();
    std::cout << "[Application] MIDI ports: " << n << '\n';
    for (unsigned int i = 0; i < n; ++i)
        std::cout << "  [" << i << "] " << midiSystem.getAvailablePorts()[i] << '\n';
    if (n > 0 && midiSystem.openPort(0))
        std::cout << "[Application] MIDI port 0 opened\n";
}

void Application::initOsc() {
    if (!oscSystem.initialize(9000)) {
        std::cerr << "[Application] Failed to initialize OSC\n";
        return;
    }
    oscSystem.setEventCallback([this](const OscMessage& msg) {
        oscSystem.applyToVisualControls(msg, visualControls);
    });
    oscSystem.setTriggerCallback([this](const std::string& action) {
        handleOscTrigger(action);
    });
    oscSystem.onMappingsChanged = [this]() { saveState(); };

    if (oscSystem.start())
        std::cout << "[Application] OSC started on port 9000\n";
    else
        std::cerr << "[Application] Failed to start OSC\n";
}

void Application::initAudio() {
    if (!audioSystem.initialize()) {
        std::cerr << "[Application] Failed to initialize Audio\n";
        return;
    }
    if (audioSystem.startStream())
        std::cout << "[Application] Audio started\n";
    else
        std::cerr << "[Application] Failed to start audio stream\n";
}

void Application::initMultiPassPipeline() {
    uint32_t queueFamily = 0; // TODO: obtener de VulkanContext

    // ── Preview pipeline ────────────────────────────────────────────────────
    if (videoSubsystemInitialized) {
        const bool has2 = videoTexture2.isReady();
        const bool has3 = videoTexture3.isReady();
        if (!multiPassPipeline.initialize(
                vulkanContext.getPhysicalDevice(), vulkanContext.getDevice(),
                vulkanContext.getGraphicsQueue(), queueFamily,
                previewPresenter.getExtent(), previewPresenter.getFormat(),
                videoTexture.getSampler(), videoTexture.getSampler(),
                videoTexture.getImageView(), videoTexture.getImageView(),
                has2 ? videoTexture2.getSampler()   : videoTexture.getSampler(),
                has2 ? videoTexture2.getImageView() : videoTexture.getImageView(),
                has3 ? videoTexture3.getSampler()   : videoTexture.getSampler(),
                has3 ? videoTexture3.getImageView() : videoTexture.getImageView(),
                uniformBufferManager.getBuffers(), UniformBufferManager::getBufferSize()))
        {
            std::cerr << "[Application] Failed to initialize MultiPassPipeline\n";
        } else {
            multiPassPipeline.updateDescriptorSets(
                uniformBufferManager.getBuffers(),
                videoTexture.getImageView(), videoTexture.getPrevImageView(),
                videoTexture.getSampler(),   videoTexture.getSampler(),
                has2 ? videoTexture2.getImageView() : videoTexture.getImageView(),
                has2 ? videoTexture2.getSampler()   : videoTexture.getSampler(),
                has3 ? videoTexture3.getImageView() : videoTexture.getImageView(),
                has3 ? videoTexture3.getSampler()   : videoTexture.getSampler()
            );
            std::cout << "[Application] Preview MultiPassPipeline initialized\n";
        }
    } else {
        std::cout << "[Application] Skipping preview MultiPassPipeline — video not initialized\n";
    }

    // ── Output pipeline (independent) ───────────────────────────────────────
    if (outputVideoSubsystemInitialized) {
        const bool outputHas2 = outputVideoTexture2.isReady();
        const bool outputHas3 = outputVideoTexture3.isReady();
        if (!outputMultiPassPipeline.initialize(
                vulkanContext.getPhysicalDevice(), vulkanContext.getDevice(),
                vulkanContext.getGraphicsQueue(), queueFamily,
                outputPresenter.getExtent(), outputPresenter.getFormat(),
                outputVideoTexture.getSampler(), outputVideoTexture.getSampler(),
                outputVideoTexture.getImageView(), outputVideoTexture.getImageView(),
                outputHas2 ? outputVideoTexture2.getSampler()   : outputVideoTexture.getSampler(),
                outputHas2 ? outputVideoTexture2.getImageView() : outputVideoTexture.getImageView(),
                outputHas3 ? outputVideoTexture3.getSampler()   : outputVideoTexture.getSampler(),
                outputHas3 ? outputVideoTexture3.getImageView() : outputVideoTexture.getImageView(),
                outputUniformBufferManager.getBuffers(), UniformBufferManager::getBufferSize()))
        {
            std::cerr << "[Application] Failed to initialize output MultiPassPipeline\n";
        } else {
            outputMultiPassPipeline.updateDescriptorSets(
                outputUniformBufferManager.getBuffers(),
                outputVideoTexture.getImageView(), outputVideoTexture.getPrevImageView(),
                outputVideoTexture.getSampler(),   outputVideoTexture.getSampler(),
                outputHas2 ? outputVideoTexture2.getImageView() : outputVideoTexture.getImageView(),
                outputHas2 ? outputVideoTexture2.getSampler()   : outputVideoTexture.getSampler(),
                outputHas3 ? outputVideoTexture3.getImageView() : outputVideoTexture.getImageView(),
                outputHas3 ? outputVideoTexture3.getSampler()   : outputVideoTexture.getSampler()
            );
            std::cout << "[Application] Output MultiPassPipeline initialized\n";
        }
    } else {
        std::cerr << "[Application] Skipping output MultiPassPipeline — output video not initialized\n";
    }

    // Make the list of loaded post-effect shaders available in the UI
    uiSystem.setPostEffectNames(multiPassPipeline.getPostEffectNames());
}

// ─────────────────────────────────────────────────────────────────────────────
// Video randomizer helpers
// ─────────────────────────────────────────────────────────────────────────────

int Application::pickNextVideoIndex(const std::vector<VideoAsset>& assets) {
    return pickNextIndex(assets, videoRandomizer, selectedVideoAsset);
}

int Application::pickNextVideoIndex2(const std::vector<VideoAsset>& assets) {
    return pickNextIndex(assets, videoRandomizer2, selectedVideoAsset2);
}

int Application::pickNextVideoIndex3(const std::vector<VideoAsset>& assets) {
    return pickNextIndex(assets, videoRandomizer3, selectedVideoAsset3);
}

bool Application::reloadVideoAtIndex(int newIndex, const std::vector<VideoAsset>& assets) {
    selectedVideoAsset = newIndex;
    transitionActive   = false;
    return reloadVideoSlot(0, assets[newIndex].metadata.path);
}

bool Application::reloadVideoAtIndex2(int newIndex, const std::vector<VideoAsset>& assets) {
    selectedVideoAsset2 = newIndex;
    transitionActive    = false;
    return reloadVideoSlot(1, assets[newIndex].metadata.path);
}

bool Application::reloadVideoAtIndex3(int newIndex, const std::vector<VideoAsset>& assets) {
    selectedVideoAsset3 = newIndex;
    transitionActive    = false;
    return reloadVideoSlot(2, assets[newIndex].metadata.path);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render job completion
// ─────────────────────────────────────────────────────────────────────────────

void Application::handleCompletedRenderJob(const std::shared_ptr<RenderJob>& job) {
    if (!job || !renderWorker) return;

    int slot = 0;
    if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_2) slot = 1;
    else if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_3) slot = 2;

    if (slot == 0) {
        videoRenderer.reset();
        videoPlayer.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(0, videoSourcePath);
        std::cout << "[Render] Auto-reloaded video 1: " << videoSourcePath << '\n';
    } else if (slot == 1) {
        videoRenderer2.reset();
        videoPlayer2.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(1, videoSourcePath2);
        std::cout << "[Render] Auto-reloaded video 2: " << videoSourcePath2 << '\n';
    } else {
        videoRenderer3.reset();
        videoPlayer3.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(2, videoSourcePath3);
        std::cout << "[Render] Auto-reloaded video 3: " << videoSourcePath3 << '\n';
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Video control helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Application::canChangeVideo() const {
    return !isReloadingVideo && !isReloadingVideo2 && !isReloadingVideo3 && !transitionActive;
}

// ─────────────────────────────────────────────────────────────────────────────
// OSC / Trigger dispatcher
// ─────────────────────────────────────────────────────────────────────────────

void Application::handleOscTrigger(const std::string& action) {
    if (action == "randomizeVideo") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        if (assets.size() > 1) reloadVideoAtIndex(pickNextVideoIndex(assets), assets);
        isReloadingVideo = false;

    } else if (action == "randomizeVideo2") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        if (assets.size() > 1) reloadVideoAtIndex2(pickNextVideoIndex2(assets), assets);
        isReloadingVideo = false;

    } else if (action == "randomizeVideo3") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        if (assets.size() > 1) reloadVideoAtIndex3(pickNextVideoIndex3(assets), assets);
        isReloadingVideo = false;

    } else if (action == "randomizePreviewVideo1") {
        uiSystem.forcePreviewShuffle(0);

    } else if (action == "randomizePreviewVideo2") {
        uiSystem.forcePreviewShuffle(1);

    } else if (action == "randomizePreviewVideo3") {
        uiSystem.forcePreviewShuffle(2);

    } else if (action == "jumpRandom") {
        if (!videoSubsystemInitialized) return;
        const double dur = videoPlayer.durationSeconds();
        if (dur > 0) videoPlayer.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));

    } else if (action == "folderChanged") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
        videoRandomizer3.shuffleQueue.clear(); videoRandomizer3.currentShuffleIndex = 0;
        if (!assets.empty()) {
            selectedVideoAsset = 0;
            reloadVideoSlot(0, assets[0].metadata.path);
        }
        isReloadingVideo = false;

    } else if (action == "applyChanges") {
        controlsDirty = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize handler
// ─────────────────────────────────────────────────────────────────────────────

void Application::handleWindowResize(uint32_t width, uint32_t height) {
    // If 0, query current drawable size (used by fullscreen toggle fallback)
    if (width == 0 || height == 0) {
        uint32_t w, h;
        window.getDrawableSize(w, h);
        width = w;
        height = h;
    }
    if (width == 0 || height == 0) return;

    std::cout << "[Resize] Preview window: " << width << "x" << height << "\n";
    vkDeviceWaitIdle(vulkanContext.getDevice());
    previewFrameSystem.cleanup();

    // Recreate preview swapchain with actual window size
    previewPresenter.recreate(width, height, renderPass);

    if (videoSubsystemInitialized && multiPassPipeline.isInitialized()) {
        multiPassPipeline.recreate(previewPresenter.getExtent());
    }

    updateAllDescriptorSets();

    previewFrameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT,
                            static_cast<uint32_t>(previewPresenter.getImageCount()));
    previewFrameSystem.resetCurrentFrame();
}

void Application::handleOutputWindowResize(uint32_t width, uint32_t height) {
    // If 0, query current drawable size (used by fullscreen toggle fallback)
    if (width == 0 || height == 0) {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &w, &h);
        width = static_cast<uint32_t>(w > 0 ? w : 0);
        height = static_cast<uint32_t>(h > 0 ? h : 0);
    }
    if (width == 0 || height == 0) return;

    std::cout << "[Resize] Output window requested: " << width << "x" << height << "\n";
    vkDeviceWaitIdle(vulkanContext.getDevice());
    outputFrameSystem.cleanup();

    // Recreate output swapchain with actual window size
    outputPresenter.recreate(width, height, renderPass);
    std::cout << "[Resize] Output swapchain extent: " << outputPresenter.getExtent().width
              << "x" << outputPresenter.getExtent().height << "\n";

    if (outputVideoSubsystemInitialized && outputMultiPassPipeline.isInitialized()) {
        outputMultiPassPipeline.recreate(outputPresenter.getExtent());
        std::cout << "[Resize] Output multipass pipeline recreated\n";
    } else {
        std::cout << "[Resize] Output multipass pipeline NOT recreated (vidInit="
                  << outputVideoSubsystemInitialized << " pipelineInit="
                  << outputMultiPassPipeline.isInitialized() << ")\n";
    }

    updateAllDescriptorSets();

    outputFrameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT,
                           static_cast<uint32_t>(outputPresenter.getImageCount()));
    outputFrameSystem.resetCurrentFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Process pending resizes (callable from mainLoop and renderOneFrame)
// ─────────────────────────────────────────────────────────────────────────────

void Application::processPendingResizes() {
    // Don't recreate swapchains during Win32 modal loop (window drag/resize).
    // handleWindowResize/handleOutputWindowResize call vkDeviceWaitIdle() which
    // blocks the modal loop, preventing WM_TIMER from firing and freezing all
    // windows. Instead, let tryRenderPreview/tryRenderOutput skip the out-of-date
    // swapchain and continue rendering the other window. The resize is processed
    // when the modal loop ends (WM_EXITSIZEMOVE sets inModalLoop=false and
    // forces resizeDebounceTime to now).
    if (inModalLoop) return;

    const auto now = std::chrono::steady_clock::now();
    uint32_t drawableWidth = 0;
    uint32_t drawableHeight = 0;
    window.getDrawableSize(drawableWidth, drawableHeight);
    if (drawableWidth > 0 && drawableHeight > 0 &&
        (drawableWidth != previewPresenter.getExtent().width ||
         drawableHeight != previewPresenter.getExtent().height)) {
        pendingResizeW = drawableWidth;
        pendingResizeH = drawableHeight;
        resizePending = true;
        resizeDebounceTime = std::min(resizeDebounceTime, now);
    }

    int outputDrawableWidth = 0;
    int outputDrawableHeight = 0;
    SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &outputDrawableWidth, &outputDrawableHeight);
    if (outputDrawableWidth > 0 && outputDrawableHeight > 0 &&
        (static_cast<uint32_t>(outputDrawableWidth) != outputPresenter.getExtent().width ||
         static_cast<uint32_t>(outputDrawableHeight) != outputPresenter.getExtent().height)) {
        pendingOutputResizeW = static_cast<uint32_t>(outputDrawableWidth);
        pendingOutputResizeH = static_cast<uint32_t>(outputDrawableHeight);
        outputResizePending = true;
        resizeDebounceTime = std::min(resizeDebounceTime, now);
    }

    if (resizePending || outputResizePending) {
        if (std::chrono::steady_clock::now() >= resizeDebounceTime) {
            if (resizePending) {
                if (pendingResizeW == 0 || pendingResizeH == 0) {
                    // Fullscreen and borderless transitions are asynchronous on SDL/Win32.
                    // Always sample the final drawable size at processing time;
                    // dimensions captured when the toggle was pressed can be stale.
                    uint32_t w = 0, h = 0;
                    window.getDrawableSize(w, h);
                    if (w == 0 || h == 0) {
                        w = pendingResizeW;
                        h = pendingResizeH;
                    }
                    if (w > 0 && h > 0) {
                        resizePending = false;
                        handleWindowResize(w, h);
                    } else {
                        resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    }
                } else {
                    // Re-query even when an event supplied dimensions: a fullscreen
                    // transition may emit SIZE_CHANGED before the final drawable size.
                    uint32_t w = 0, h = 0;
                    window.getDrawableSize(w, h);
                    if (w == 0 || h == 0) {
                        w = pendingResizeW;
                        h = pendingResizeH;
                    }
                    if (w > 0 && h > 0) {
                        resizePending = false;
                        handleWindowResize(w, h);
                    } else {
                        resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    }
                }
            }
            if (outputResizePending) {
                if (pendingOutputResizeW == 0 || pendingOutputResizeH == 0) {
                    // Fullscreen and borderless transitions are asynchronous on SDL/Win32.
                    // Always sample the final drawable size at processing time.
                    int ow = 0, oh = 0;
                    SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &ow, &oh);
                    if (ow == 0 || oh == 0) {
                        ow = static_cast<int>(pendingOutputResizeW);
                        oh = static_cast<int>(pendingOutputResizeH);
                    }
                    std::cout << "[Resize] Output drawable size query: " << ow << "x" << oh << "\n";
                    if (ow > 0 && oh > 0) {
                        outputResizePending = false;
                        handleOutputWindowResize(static_cast<uint32_t>(ow), static_cast<uint32_t>(oh));
                    } else {
                        resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    }
                } else {
                    // Re-query the drawable size because SIZE_CHANGED can precede
                    // the final size reported after a fullscreen transition.
                    int ow = 0, oh = 0;
                    SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &ow, &oh);
                    if (ow == 0 || oh == 0) {
                        ow = static_cast<int>(pendingOutputResizeW);
                        oh = static_cast<int>(pendingOutputResizeH);
                    }
                    std::cout << "[Resize] Output drawable size: " << ow << "x" << oh << "\n";
                    if (ow > 0 && oh > 0) {
                        outputResizePending = false;
                        handleOutputWindowResize(static_cast<uint32_t>(ow), static_cast<uint32_t>(oh));
                    } else {
                        resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    }
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render one frame (used by mainLoop and Win32 modal loop timer)
// ─────────────────────────────────────────────────────────────────────────────

void Application::renderOneFrame() {
    if (renderingInProgress) return;
    renderingInProgress = true;

    // RAII guard: ensures renderingInProgress is always reset, even if an
    // exception escapes (e.g. during modal loop rendering).
    struct ScopeGuard {
        std::function<void()> fn;
        ~ScopeGuard() { if (fn) fn(); }
    } guard{ [&] { renderingInProgress = false; } };

    // Process pending resizes first — critical during Win32 modal loops
    // (window drag/resize) where mainLoop() is blocked and cannot process them.
    processPendingResizes();

    // Update output window visibility flag
    outputWindowHidden = !outputWindowVisible;

    // ── Delta time ──────────────────────────────────────────────────────
    const auto now       = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - lastFrameTimestamp).count();
    lastFrameTimestamp   = now;
    currentDeltaTime     = deltaTime;

    // ── FPS counter + window titles ─────────────────────────────────────
    frameCount++;
    fpsAccumTime += deltaTime;
    if (fpsAccumTime >= 0.5f) {
        currentFps = frameCount / fpsAccumTime;
        frameCount = 0;
        fpsAccumTime = 0.0f;
        char title[128];
        if (previewPaused)
            snprintf(title, sizeof(title), "[PREVIEW PAUSED] Vulkan — %.1f FPS", currentFps);
        else
            snprintf(title, sizeof(title), "[PREVIEW] Vulkan — %.1f FPS", currentFps);
        SDL_SetWindowTitle(window.getMainWindow(), title);
        snprintf(title, sizeof(title), "[CONTROLS] UI — %.1f FPS", currentFps);
        SDL_SetWindowTitle(window.getUiWindow(), title);
    }

    // ── Render jobs pendientes ───────────────────────────────────────────
    {
        std::lock_guard lock(completedRenderJobsMutex);
        while (!completedRenderJobs.empty()) {
            handleCompletedRenderJob(completedRenderJobs.front());
            completedRenderJobs.pop();
        }
    }

    // ── Video transition animation ───────────────────────────────────────
    if (transitionActive) {
        float step = deltaTime / transitionDuration;
        transitionProgress += step;
        if (transitionProgress >= 1.0f) {
            transitionProgress = 1.0f;
            transitionActive = false;
            videoTexture.setFreezePrev(false);
            videoTexture2.setFreezePrev(false);
        }
    }

    // ── Auto-randomize colors ────────────────────────────────────────────
    float quickEnergy = std::min(audioSystem.getRMS() * visualControls.audio.inputGain * 3.0f, 1.0f);
    tickAutoColors(deltaTime, quickEnergy);

    // ── Auto-randomize videos ────────────────────────────────────────────
    tickAutoRandomize(deltaTime, now);

    // ── Auto random jump ─────────────────────────────────────────────────
    if (visualControls.playback.enableRandomJumpInterval &&
        visualControls.playback.randomVideoStart && videoSubsystemInitialized)
    {
        float elapsed = std::chrono::duration<float>(now - lastRandomJumpTime).count();
        if (elapsed >= visualControls.playback.randomJumpInterval) {
            const double dur = videoPlayer.durationSeconds();
            if (dur > 0) {
                videoPlayer.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));
                lastRandomJumpTime = now;
            }
        }
    }

    // ── Actualizar subsistemas ───────────────────────────────────────────
    midiSystem.update();
    oscSystem.update();

    // ── Post effect setup ────────────────────────────────────────────────
    multiPassPipeline.setPostEffect(visualControls.post.postEffectName);
    outputMultiPassPipeline.setPostEffect(outputVisualControls.post.postEffectName);

    // ── Playback rates ───────────────────────────────────────────────────
    if (videoSubsystemInitialized)  videoPlayer.setPlaybackRate(visualControls.playback.videoPlaybackRate);
    if (videoSubsystemInitialized2) videoPlayer2.setPlaybackRate(visualControls.playback.video2PlaybackRate);
    if (videoSubsystemInitialized3) videoPlayer3.setPlaybackRate(visualControls.playback.video3PlaybackRate);

    if (visualControls.playback.videoPlaybackRate != lastPlaybackRate) {
        lastPlaybackRate = visualControls.playback.videoPlaybackRate;
        videoSpeedCache[videoSourcePath] = lastPlaybackRate;
    }
    if (visualControls.playback.video2PlaybackRate != lastPlaybackRate2) {
        lastPlaybackRate2 = visualControls.playback.video2PlaybackRate;
        videoSpeedCache[videoSourcePath2] = lastPlaybackRate2;
    }
    if (visualControls.playback.video3PlaybackRate != lastPlaybackRate3) {
        lastPlaybackRate3 = visualControls.playback.video3PlaybackRate;
        videoSpeedCache[videoSourcePath3] = lastPlaybackRate3;
    }

    // ── UI ───────────────────────────────────────────────────────────────
    UIDiagnostics diag;
    diag.lastFrameFrameIndex       = previewFrameSystem.currentFrameIndex();
    diag.lastFrameImageIndex       = 0;
    diag.swapchainWidth            = previewPresenter.getExtent().width;
    diag.swapchainHeight           = previewPresenter.getExtent().height;
    diag.currentMode               = visualControls.playback.activeMode;
    diag.videoReady                = videoSubsystemInitialized && videoTexture.isReady();
    diag.videoWidth                = videoTexture.getWidth();
    diag.videoHeight               = videoTexture.getHeight();
    diag.animationTime             = debugAnimationTime;
    diag.animationDelta            = debugAnimationDelta;
    diag.animationElapsedSeconds   = debugAnimationElapsedSeconds;
    diag.gpuPassTimes              = multiPassPipeline.lastGpuPassTimes;
    diag.gpuTotalTime              = multiPassPipeline.lastGpuTotalTime;
    diag.appFps                    = currentFps;

    UICallbacks callbacks = buildUICallbacks();
    uiSystem.render(visualControls, videoRandomizer, videoRandomizer2, videoRandomizer3,
                    videoPlayer, videoPlayer2, videoPlayer3, videoRegistry,
                    selectedVideoAsset, selectedVideoAsset2, selectedVideoAsset3,
                    transitionDuration, transitionDuration2, transitionDuration3,
                    allowDimensionChangeRecreation, controlsDirty,
                    rng, diag, callbacks, midiSystem, oscSystem, audioSystem,
                    videoSourcePath, videoSourcePath2, videoSourcePath3,
                    videoAssetsRoot);

    // ── Render preview and output independently ──────────────────────────
    tryRenderPreview();
    tryRenderOutput();
}

// ─────────────────────────────────────────────────────────────────────────────
// tryRenderPreview — fully independent preview window render
// ─────────────────────────────────────────────────────────────────────────────

bool Application::tryRenderPreview() {
    if (previewWindowMinimized ||
        (inModalLoop && modalPreviewResize) ||
        previewPresenter.getExtent().width == 0 || previewPresenter.getExtent().height == 0)
        return false;

    uint32_t previewImageIndex;
    VkResult previewResult;
    FrameContext* previewFrame = previewFrameSystem.beginFrame(
        previewPresenter.getSwapchain(), previewImageIndex, previewResult);

    if (!previewFrame) {
        if (previewResult == VK_ERROR_OUT_OF_DATE_KHR ||
            previewResult == VK_ERROR_SURFACE_LOST_KHR ||
            previewResult == VK_NOT_READY ||
            previewResult == VK_ERROR_DEVICE_LOST)
        {
            uint32_t w, h;
            window.getDrawableSize(w, h);
            if (w > 0 && h > 0) {
                pendingResizeW = w;
                pendingResizeH = h;
                resizePending = true;
                auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
            }
        }
        return false;
    }

    // Preview renders at 30fps (update UBOs every 2 frames)
    bool updatePreview = (previewFrameCounter % 2 == 0);
    ++previewFrameCounter;

    if (updatePreview) {
        updateUniformBuffer(previewFrame->frameIndex, visualControls,
                            uniformBufferManager, previewPresenter, previewAnim,
                            videoTexture, videoTexture2, videoTexture3,
                            videoSubsystemInitialized, videoSubsystemInitialized2, videoSubsystemInitialized3);
    }

    // Update preview video renderers
    if (!uiSystem.isRendererPaused() && !previewPaused) {
        if (videoRenderer)  videoRenderer->update(currentDeltaTime, previewFrame->frameIndex);
        if (videoRenderer2) videoRenderer2->update(currentDeltaTime, previewFrame->frameIndex);
        if (videoRenderer3) videoRenderer3->update(currentDeltaTime, previewFrame->frameIndex);
    }

    // Record command buffer
    recordPreviewCommandBuffer(previewCommandBuffers[previewFrame->frameIndex],
                               *previewFrame, previewImageIndex);

    // Submit
    auto previewRenderFinished = previewFrameSystem.getRenderFinishedSemaphore(previewImageIndex).value_or(VK_NULL_HANDLE);
    if (previewRenderFinished == VK_NULL_HANDLE)
        throw std::runtime_error("invalid preview renderFinished semaphore");

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &previewFrame->imageAvailableSemaphore;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &previewCommandBuffers[previewFrame->frameIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &previewRenderFinished;

    if (vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &submit, previewFrame->inFlightFence) != VK_SUCCESS) {
        std::cerr << "[tryRenderPreview] vkQueueSubmit failed — device may be lost\n";
        // Fence was reset in beginFrame but never signaled because submit failed.
        // Submit an empty queue operation to signal the fence so the next
        // beginFrame's vkWaitForFences doesn't block forever.
        VkSubmitInfo emptySubmit{};
        emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &emptySubmit, previewFrame->inFlightFence);
        previewFrameSystem.endFrame();
        return false;
    }

    // Present
    VkSwapchainKHR previewSc = previewPresenter.getSwapchain();
    VkPresentInfoKHR previewPresent{};
    previewPresent.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    previewPresent.waitSemaphoreCount = 1;
    previewPresent.pWaitSemaphores    = &previewRenderFinished;
    previewPresent.swapchainCount     = 1;
    previewPresent.pSwapchains        = &previewSc;
    previewPresent.pImageIndices      = &previewImageIndex;

    VkResult previewPresentResult = vkQueuePresentKHR(vulkanContext.getPresentQueue(), &previewPresent);
    if (previewPresentResult == VK_ERROR_OUT_OF_DATE_KHR || previewPresentResult == VK_SUBOPTIMAL_KHR) {
        uint32_t w, h;
        window.getDrawableSize(w, h);
        if (w > 0 && h > 0) {
            pendingResizeW = w;
            pendingResizeH = h;
            resizePending = true;
            auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
        }
    }

    previewFrameSystem.endFrame();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// tryRenderOutput — fully independent output window render
// ─────────────────────────────────────────────────────────────────────────────

bool Application::tryRenderOutput() {
    if (outputWindowHidden || outputWindowMinimized ||
        (inModalLoop && modalOutputResize) ||
        outputPresenter.getExtent().width == 0 || outputPresenter.getExtent().height == 0)
        return false;

    uint32_t outputImageIndex;
    VkResult outputResult;
    FrameContext* outputFrame = outputFrameSystem.beginFrame(
        outputPresenter.getSwapchain(), outputImageIndex, outputResult);

    if (!outputFrame) {
        if (outputResult == VK_ERROR_OUT_OF_DATE_KHR ||
            outputResult == VK_ERROR_SURFACE_LOST_KHR ||
            outputResult == VK_NOT_READY ||
            outputResult == VK_ERROR_DEVICE_LOST)
        {
            int ow = 0, oh = 0;
            SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &ow, &oh);
            if (ow > 0 && oh > 0) {
                pendingOutputResizeW = static_cast<uint32_t>(ow);
                pendingOutputResizeH = static_cast<uint32_t>(oh);
                outputResizePending = true;
                auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
            }
        }
        return false;
    }

    // Output always renders at 60fps with its own controls and its own frame index
    updateUniformBuffer(outputFrame->frameIndex, outputVisualControls,
                        outputUniformBufferManager, outputPresenter, outputAnim,
                        outputVideoTexture, outputVideoTexture2, outputVideoTexture3,
                        outputVideoSubsystemInitialized, outputVideoSubsystemInitialized2, outputVideoSubsystemInitialized3,
                        true);

    // Update output video renderers with output frame index
    if (outputVideoRenderer)  outputVideoRenderer->update(currentDeltaTime, outputFrame->frameIndex);
    if (outputVideoRenderer2) outputVideoRenderer2->update(currentDeltaTime, outputFrame->frameIndex);
    if (outputVideoRenderer3) outputVideoRenderer3->update(currentDeltaTime, outputFrame->frameIndex);

    // Record command buffer
    recordOutputCommandBuffer(outputCommandBuffers[outputFrame->frameIndex],
                              *outputFrame, outputImageIndex);

    // Submit
    auto outputRenderFinished = outputFrameSystem.getRenderFinishedSemaphore(outputImageIndex).value_or(VK_NULL_HANDLE);
    if (outputRenderFinished == VK_NULL_HANDLE)
        throw std::runtime_error("invalid output renderFinished semaphore");

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &outputFrame->imageAvailableSemaphore;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &outputCommandBuffers[outputFrame->frameIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &outputRenderFinished;

    if (vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &submit, outputFrame->inFlightFence) != VK_SUCCESS) {
        std::cerr << "[tryRenderOutput] vkQueueSubmit failed — device may be lost\n";
        // Fence was reset in beginFrame but never signaled because submit failed.
        // Submit an empty queue operation to signal the fence so the next
        // beginFrame's vkWaitForFences doesn't block forever.
        VkSubmitInfo emptySubmit{};
        emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &emptySubmit, outputFrame->inFlightFence);
        outputFrameSystem.endFrame();
        return false;
    }

    // Present
    VkSwapchainKHR outputSc = outputPresenter.getSwapchain();
    VkPresentInfoKHR outputPresent{};
    outputPresent.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    outputPresent.waitSemaphoreCount = 1;
    outputPresent.pWaitSemaphores    = &outputRenderFinished;
    outputPresent.swapchainCount     = 1;
    outputPresent.pSwapchains        = &outputSc;
    outputPresent.pImageIndices      = &outputImageIndex;

    VkResult outputPresentResult = vkQueuePresentKHR(vulkanContext.getPresentQueue(), &outputPresent);
    if (outputPresentResult == VK_ERROR_OUT_OF_DATE_KHR || outputPresentResult == VK_SUBOPTIMAL_KHR) {
        int ow = 0, oh = 0;
        SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &ow, &oh);
        if (ow > 0 && oh > 0) {
            pendingOutputResizeW = static_cast<uint32_t>(ow);
            pendingOutputResizeH = static_cast<uint32_t>(oh);
            outputResizePending = true;
            auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
        }
    }

    outputFrameSystem.endFrame();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
void CALLBACK Application::modalTimerProc(HWND hwnd, UINT, UINT_PTR, DWORD) {
    auto* app = reinterpret_cast<Application*>(GetPropW(hwnd, L"TechnoVisualsVulkanApp"));
    if (!app || !app->inModalLoop || app->renderingInProgress) return;

    try {
        app->renderOneFrame();
    } catch (...) {
    }
}
#endif

void Application::mainLoop() {

#ifdef _WIN32
    // When the user drags a window title bar, Win32 enters a modal loop in
    // DefWindowProc that blocks SDL_PollEvent. SDL_SetWindowsMessageHook is
    // still called from within that modal loop for every message dispatched.
    // We render on every message during the modal loop to keep all windows alive.
    SDL_SetWindowsMessageHook([](void* userdata, void* hWnd, unsigned int message, Uint64 wParam, Sint64 lParam) {
        Application* app = static_cast<Application*>(userdata);
        if (!app) return;

        // WM_ENTERSIZEMOVE = 0x0231, WM_EXITSIZEMOVE = 0x0232
        if (message == 0x0231) {
            app->inModalLoop = true;

            SDL_SysWMinfo wmInfo{};
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo(app->window.getMainWindow(), &wmInfo) == SDL_TRUE &&
                wmInfo.info.win.window == hWnd) {
                app->modalPreviewResize = true;
                app->modalOutputResize = false;
            } else if (SDL_GetWindowWMInfo(app->window.getOutputWindow(), &wmInfo) == SDL_TRUE &&
                       wmInfo.info.win.window == hWnd) {
                app->modalPreviewResize = false;
                app->modalOutputResize = true;
            } else {
                app->modalPreviewResize = false;
                app->modalOutputResize = false;
            }

            // Install a high-frequency timer so WM_TIMER messages keep flowing
            // through the hook during the modal loop, ensuring renderOneFrame()
            // is called even when DefWindowProc swallows mouse/keyboard messages.
            SetPropW((HWND)hWnd, L"TechnoVisualsVulkanApp", app);
            SetTimer((HWND)hWnd, 1, 8, &Application::modalTimerProc); // ~120fps timer
        } else if (message == 0x0232) {
            app->inModalLoop = false;
            app->modalPreviewResize = false;
            app->modalOutputResize = false;
            KillTimer((HWND)hWnd, 1);
            RemovePropW((HWND)hWnd, L"TechnoVisualsVulkanApp");
            // Force immediate resize processing on next renderOneFrame.
            // processPendingResizes() was skipped during the modal loop,
            // so the swapchain needs to be recreated now that the drag ended.
            app->resizeDebounceTime = std::chrono::steady_clock::time_point::min();
        }

        // Render only from the timer during the modal loop. Rendering from
        // every resize/mouse message can recursively enter Vulkan while the
        // window is still being resized and stall the other windows.
        if (app->inModalLoop && message == 0x0113 && !app->renderingInProgress) {
            try {
                app->renderOneFrame();
            } catch (...) {
                // Ignore errors during modal loop rendering
            }
        }
    }, this);
#endif

    while (running) {

        // ── Eventos SDL ─────────────────────────────────────────────────────
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Global hotkeys that must work even when ImGui has focus
            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                if (event.key.keysym.sym == SDLK_p) {
                    uiSystem.toggleRendererPause();
                    std::cout << "[UI] Renderer " << (uiSystem.isRendererPaused() ? "PAUSED" : "RESUMED") << "\n";
                    continue;
                }
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    // Remove window focus so keyboard hotkeys are not captured by ImGui widgets
                    ImGui::SetWindowFocus(nullptr);
                    continue;
                }
                if (event.key.keysym.sym == SDLK_F11) {
                    int postW = 0, postH = 0;
                    if (!outputWindowVisible) {
                        SDL_ShowWindow(window.getOutputWindow());
                        outputWindowVisible = true;
                        outputWindowMinimized = false;
                        std::cout << "[Output] Window restored\n";
                    } else {
                        uint32_t flags = SDL_GetWindowFlags(window.getOutputWindow());
                        bool isFs = (flags & SDL_WINDOW_FULLSCREEN);
                        int preW = 0, preH = 0;
                        SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &preW, &preH);
                        std::cout << "[F11] Pre-toggle drawable: " << preW << "x" << preH
                                  << " flags=0x" << std::hex << flags << std::dec
                                  << " isFs=" << isFs << "\n";
                        if (!isFs) {
                            SDL_SetWindowBordered(window.getOutputWindow(), SDL_FALSE);
                            SDL_SetWindowFullscreen(window.getOutputWindow(), SDL_WINDOW_FULLSCREEN_DESKTOP);
                        } else {
                            SDL_SetWindowFullscreen(window.getOutputWindow(), 0);
                            SDL_SetWindowBordered(window.getOutputWindow(), SDL_TRUE);
                        }
                        SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &postW, &postH);
                        std::cout << "[F11] Post-toggle drawable: " << postW << "x" << postH
                                  << " Fullscreen: " << (!isFs ? "ON" : "OFF") << "\n";
                    }
                    // Use the drawable size we just queried (post-toggle) as the
                    // pending resize dims.  The SIZE_CHANGED event may still fire
                    // and update them, but this way we have a valid size even if
                    // the user presses F11 again quickly.
                    outputResizePending = true;
                    // The immediate query can still return the pre-toggle size.
                    // Force processPendingResizes() to query the final size later.
                    pendingOutputResizeW = 0;
                    pendingOutputResizeH = 0;
                    resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    continue;
                }
                if (event.key.keysym.sym == SDLK_F12) {
                    uint32_t flags = SDL_GetWindowFlags(window.getMainWindow());
                    bool isFs = (flags & SDL_WINDOW_FULLSCREEN);
                    if (!isFs) {
                        SDL_SetWindowBordered(window.getMainWindow(), SDL_FALSE);
                        SDL_SetWindowFullscreen(window.getMainWindow(), SDL_WINDOW_FULLSCREEN_DESKTOP);
                    } else {
                        SDL_SetWindowFullscreen(window.getMainWindow(), 0);
                        SDL_SetWindowBordered(window.getMainWindow(), SDL_TRUE);
                    }
                    std::cout << "[Preview] Fullscreen: " << (!isFs ? "ON" : "OFF") << std::endl;
                    int preW2 = 0, preH2 = 0;
                    SDL_Vulkan_GetDrawableSize(window.getMainWindow(), &preW2, &preH2);
                    resizePending = true;
                    // The immediate query can still return the pre-toggle size.
                    // Force processPendingResizes() to query the final size later.
                    pendingResizeW = 0;
                    pendingResizeH = 0;
                    resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    continue;
                }
                if (event.key.keysym.sym == SDLK_1 || event.key.keysym.sym == SDLK_KP_1) {
                    handleOscTrigger("randomizePreviewVideo1");
                    continue;
                }
                if (event.key.keysym.sym == SDLK_2 || event.key.keysym.sym == SDLK_KP_2) {
                    handleOscTrigger("randomizePreviewVideo2");
                    continue;
                }
                if (event.key.keysym.sym == SDLK_3 || event.key.keysym.sym == SDLK_KP_3) {
                    handleOscTrigger("randomizePreviewVideo3");
                    continue;
                }
            }

            bool consumed = uiSystem.processEvent(event);

            if (event.type == SDL_QUIT) { running = false; break; }

            // Mouse wheel zoom for procedural camera (when not interacting with UI)
            if (event.type == SDL_MOUSEWHEEL) {
                ImGuiIO& io = ImGui::GetIO();
                if (!io.WantCaptureMouse) {
                    float zoomSpeed = 0.1f;
                    float delta = event.wheel.y > 0 ? -zoomSpeed : (event.wheel.y < 0 ? zoomSpeed : 0.0f);
                    visualControls.camera.zoom = std::clamp(
                        visualControls.camera.zoom + delta,
                        0.01f, 5.0f);
                    controlsDirty = true;
                }
            }

            if (event.type == SDL_KEYDOWN && !event.key.repeat && !consumed) {
                switch (event.key.keysym.sym) {
                    case SDLK_r: {
                        static std::random_device rd;
                        static std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(0, 1);
                        auto& p = visualControls.post;
                        auto& f = visualControls.fx;
                        // Randomize post FX toggles
                        p.enablePostCrtCurvature    = dis(gen) == 1;
                        p.enablePostScanMask        = dis(gen) == 1;
                        p.enablePostVignette        = dis(gen) == 1;
                        p.enablePostFishEye         = dis(gen) == 1;
                        p.enablePostBloom           = dis(gen) == 1;
                        p.enablePostAberration      = dis(gen) == 1;
                        p.enablePostGrain           = dis(gen) == 1;
                        p.enablePostBend            = dis(gen) == 1;
                        p.enablePostGlitch          = dis(gen) == 1;
                        p.enablePostColorBalance    = dis(gen) == 1;
                        p.enableAnalog              = dis(gen) == 1;
                        f.enableMirror              = dis(gen) == 1;
                        f.enableInvert              = dis(gen) == 1;
                        f.enablePosterize           = dis(gen) == 1;
                        f.enableInfrared            = dis(gen) == 1;
                        f.enableRGBShift            = dis(gen) == 1;
                        f.enableDistortion          = dis(gen) == 1;
                        f.enableBlurMotion          = dis(gen) == 1;
                        f.enableSharpen             = dis(gen) == 1;
                        break;
                    }
                    case SDLK_t:
                        uiSystem.randomizeVJayBasics(visualControls);
                        break;
                    case SDLK_y:
                        uiSystem.randomizeVJayExtra(visualControls);
                        break;
                    case SDLK_LEFT: {
                        visualControls.playback.activeMode = uiSystem.cycleLayerMode(visualControls.playback.activeMode, -1);
                        controlsDirty = true;
                        std::cout << "[Mode] Previous: " << visualControls.playback.activeMode << std::endl;
                        break;
                    }
                    case SDLK_RIGHT: {
                        visualControls.playback.activeMode = uiSystem.cycleLayerMode(visualControls.playback.activeMode, 1);
                        controlsDirty = true;
                        std::cout << "[Mode] Next: " << visualControls.playback.activeMode << std::endl;
                        break;
                    }
                    case SDLK_UP: {
                        visualControls.post.postEffectName = uiSystem.cyclePostEffect(visualControls.post.postEffectName, 1);
                        controlsDirty = true;
                        std::cout << "[Post-Effect] Up: " << visualControls.post.postEffectName << std::endl;
                        break;
                    }
                    case SDLK_DOWN: {
                        visualControls.post.postEffectName = uiSystem.cyclePostEffect(visualControls.post.postEffectName, -1);
                        controlsDirty = true;
                        std::cout << "[Post-Effect] Down: " << visualControls.post.postEffectName << std::endl;
                        break;
                    }
                    case SDLK_RETURN:
                        outputVisualControls = visualControls;
                        // Copy video state from preview to output
                        // Only reload output players when the path actually changed
                        // to avoid the big delay of re-initializing the same video.
                        {
                            std::string oldOutputVideoSourcePath = outputVideoSourcePath;
                            std::string oldOutputVideoSourcePath2 = outputVideoSourcePath2;
                            std::string oldOutputVideoSourcePath3 = outputVideoSourcePath3;
                            outputVideoSourcePath = videoSourcePath;
                            outputVideoSourcePath2 = videoSourcePath2;
                            outputVideoSourcePath3 = videoSourcePath3;
                            outputSelectedVideoAsset = selectedVideoAsset;
                            outputSelectedVideoAsset2 = selectedVideoAsset2;
                            outputSelectedVideoAsset3 = selectedVideoAsset3;

                            if (!outputVideoSourcePath.empty() && outputVideoSourcePath != oldOutputVideoSourcePath)
                                reloadOutputVideoSlot(0, outputVideoSourcePath);
                            if (!outputVideoSourcePath2.empty() && outputVideoSourcePath2 != oldOutputVideoSourcePath2)
                                reloadOutputVideoSlot(1, outputVideoSourcePath2);
                            if (!outputVideoSourcePath3.empty() && outputVideoSourcePath3 != oldOutputVideoSourcePath3)
                                reloadOutputVideoSlot(2, outputVideoSourcePath3);
                        }
                        std::cout << "[Output] Controls + videos committed from preview\n";
                        break;
                    case SDLK_SPACE:
                        previewPaused = !previewPaused;
                        std::cout << "[Preview] " << (previewPaused ? "PAUSED" : "RESUMED") << "\n";
                        break;
                }
            }

            if (event.type == SDL_WINDOWEVENT) {
                SDL_Window* src = SDL_GetWindowFromID(event.window.windowID);
                const auto  ev  = event.window.event;

                if (ev == SDL_WINDOWEVENT_CLOSE) {
                    if (src == window.getMainWindow()) {
                        running = false; break;
                    } else if (src == window.getOutputWindow()) {
                        SDL_HideWindow(window.getOutputWindow());
                        outputWindowVisible = false;
                        std::cout << "[Output] Window hidden (use F11 to restore)\n";
                    } else if (src == window.getUiWindow()) {
                        SDL_HideWindow(window.getUiWindow());
                        std::cout << "[UI] Window hidden\n";
                    }
                }

                if (ev == SDL_WINDOWEVENT_MINIMIZED || ev == SDL_WINDOWEVENT_HIDDEN) {
                    if (src == window.getMainWindow()) {
                        previewWindowMinimized = true;
                    } else if (src == window.getOutputWindow()) {
                        outputWindowMinimized = true;
                    }
                }

                if (ev == SDL_WINDOWEVENT_RESTORED || ev == SDL_WINDOWEVENT_SHOWN) {
                    if (src == window.getMainWindow()) {
                        previewWindowMinimized = false;
                    } else if (src == window.getOutputWindow()) {
                        outputWindowMinimized = false;
                        outputWindowVisible = true;
                    }
                }

                if (src == window.getMainWindow() && initializationComplete &&
                    (ev == SDL_WINDOWEVENT_RESIZED || ev == SDL_WINDOWEVENT_SIZE_CHANGED))
                {
                    window.resetResizeFlag();
                    // Always query fresh drawable size so we get the FINAL
                    // dimensions after async Win32 fullscreen transitions.
                    // (Previously we skipped this when pendingResizeW was 0,
                    //  which meant F12's 0-values got stuck at the old size.)
                    uint32_t w, h;
                    window.getDrawableSize(w, h);
                    pendingResizeW = w;
                    pendingResizeH = h;
                    resizePending = true;
                    // Don't reduce a longer debounce set by fullscreen toggle (F12)
                    auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
                }

                if (src == window.getOutputWindow() && initializationComplete &&
                    (ev == SDL_WINDOWEVENT_RESIZED || ev == SDL_WINDOWEVENT_SIZE_CHANGED))
                {
                    // Always query fresh drawable size so we get the FINAL
                    // dimensions after async Win32 fullscreen transitions.
                    int ow = 0, oh = 0;
                    SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &ow, &oh);
                    std::cout << "[Event] Output SIZE_CHANGED: " << ow << "x" << oh << "\n";
                    pendingOutputResizeW = static_cast<uint32_t>(ow > 0 ? ow : 0);
                    pendingOutputResizeH = static_cast<uint32_t>(oh > 0 ? oh : 0);
                    outputResizePending = true;
                    // Don't reduce a longer debounce set by fullscreen toggle (F11)
                    auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
                }
            }
        }
        if (!running) break;

        // ── Process debounced resize ──────────────────────────────────────────
        processPendingResizes();

        // ── Render one frame ──────────────────────────────────────────────────
        try {
            renderOneFrame();
            consecutiveRenderErrors = 0;
        } catch (const std::exception& e) {
            std::cerr << "[MainLoop] Exception during renderOneFrame: " << e.what() << "\n";
            consecutiveRenderErrors++;
            if (consecutiveRenderErrors >= 3) {
                std::cerr << "[MainLoop] Too many consecutive render errors, exiting\n";
                break;
            }
            // Force resize on next iteration to recover from swapchain/device errors
            if (previewPresenter.getExtent().width > 0) {
                resizePending = true;
                resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            }
        } catch (...) {
            std::cerr << "[MainLoop] Unknown exception during renderOneFrame\n";
            consecutiveRenderErrors++;
            if (consecutiveRenderErrors >= 3) {
                std::cerr << "[MainLoop] Too many consecutive render errors, exiting\n";
                break;
            }
        }

        // ── Frame rate limiter (adaptive) ─────────────────────────────────
        constexpr float targetFrameTime = 1.0f / 60.0f;
        constexpr auto minCooldown = std::chrono::milliseconds(2);
        const auto frameEnd = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(frameEnd - lastFrameTimestamp).count();
        if (elapsed < targetFrameTime) {
            const auto sleepNs = std::chrono::nanoseconds(
                static_cast<int64_t>((targetFrameTime - elapsed) * 1e9f));
            std::this_thread::sleep_for(sleepNs + minCooldown);
        } else {
            std::this_thread::sleep_for(minCooldown);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick helpers (extraídos del mainLoop)
// ─────────────────────────────────────────────────────────────────────────────

void Application::tickAutoColors(float dt, float energy) {
    auto& c = visualControls.color;
    if (!c.autoRandomizeColors) return;

    auto hsvToRgb = [](float h, float s, float v) -> glm::vec3 {
        float c2 = v * s, x = c2 * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f)), m = v - c2;
        float r, g, b;
        if      (h < 60)  { r=c2; g=x;  b=0;  }
        else if (h < 120) { r=x;  g=c2; b=0;  }
        else if (h < 180) { r=0;  g=c2; b=x;  }
        else if (h < 240) { r=0;  g=x;  b=c2; }
        else if (h < 300) { r=x;  g=0;  b=c2; }
        else              { r=c2; g=0;  b=x;  }
        return { r+m, g+m, b+m };
    };

    if (visualControls.system.enableAudioReactive) {
        // Continuous hue rotation driven by energy — no timer, smooth flow
        static float currentHue = 0.0f;
        float speed = 30.0f + energy * 200.0f; // 30-230 deg/sec based on energy
        currentHue += speed * dt;
        if (currentHue >= 360.0f) currentHue -= 360.0f;

        float sat = 0.5f + energy * 0.5f;  // 0.5-1.0
        float val = 0.6f + energy * 0.4f;  // 0.6-1.0

        c.primaryColorTarget   = glm::vec4(hsvToRgb(currentHue, sat, val), 1.f);
        c.secondaryColorTarget = glm::vec4(hsvToRgb(std::fmod(currentHue + 180.f, 360.f), sat, val), 1.f);
    } else {
        // Original timer-based randomization
        c.colorRandomizeElapsed += dt;
        if (c.colorRandomizeElapsed >= c.colorRandomizeInterval) {
            std::uniform_real_distribution<float> hueDist(0.0f, 360.0f);
            std::uniform_real_distribution<float> satDist(0.6f, 1.0f);
            std::uniform_real_distribution<float> valDist(0.7f, 1.0f);

            float ph = hueDist(rng), ps = satDist(rng), pv = valDist(rng);
            c.primaryColorTarget   = glm::vec4(hsvToRgb(ph, ps, pv), 1.f);
            c.secondaryColorTarget = glm::vec4(hsvToRgb(std::fmod(ph + 180.f, 360.f), ps, pv), 1.f);
            c.colorRandomizeElapsed = 0.f;
        }
    }

    float t = 2.f * dt;
    c.primaryColor   = glm::mix(c.primaryColor,   c.primaryColorTarget,   t);
    c.secondaryColor = glm::mix(c.secondaryColor, c.secondaryColorTarget, t);
}

void Application::tickAutoRandomize(float dt,
                                    const std::chrono::steady_clock::time_point& /*now*/)
{
    auto tick = [&](bool enabled, bool initialized,
                    auto& rz, int slot,
                    const std::string& folder) {
        if (!enabled || !initialized) return;
        rz.elapsedSeconds += dt;
        float target = (rz.useVideoDuration && rz.currentVideoDuration > 0.f)
                       ? rz.currentVideoDuration : rz.intervalSeconds;
        if (rz.elapsedSeconds < target) return;
        if (!canChangeVideo()) { rz.elapsedSeconds = 0.f; return; }

        const auto& assets = videoRegistry.getFilteredAssets(folder);
        if (assets.size() > 1) {
            int idx = (slot == 0) ? pickNextVideoIndex(assets) : pickNextVideoIndex2(assets);
            if (slot == 0) reloadVideoAtIndex(idx, assets);
            else           reloadVideoAtIndex2(idx, assets);
            uiSystem.forcePreviewShuffle(slot);
        }
        isReloadingVideo = false;
    };

    tick(videoRandomizer.autoRandomize,  videoSubsystemInitialized,
         videoRandomizer,  0, visualControls.playback.selectedVideoFolder);

    tick(videoRandomizer2.autoRandomize && visualControls.playback.enableDualVideo,
         videoSubsystemInitialized,
         videoRandomizer2, 1, visualControls.playback.selectedVideo2Folder);

    tick(videoRandomizer3.autoRandomize && visualControls.playback.enableDualVideo,
         videoSubsystemInitialized,
         videoRandomizer3, 2, visualControls.playback.selectedVideo3Folder);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI callbacks — centralizado
// ─────────────────────────────────────────────────────────────────────────────

UICallbacks Application::buildUICallbacks() {
    UICallbacks cb;

    cb.onControlsChanged = [this]() { controlsDirty = true; };

    cb.onApplyChanges = [this]() {
        if (!canChangeVideo()) return;
        int slot = 0;
        std::string path = videoSourcePath;
        if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_2) { slot = 1; path = videoSourcePath2; }
        else if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_3) { slot = 2; path = videoSourcePath3; }
        reloadVideoSlot(slot, path);
    };

    cb.onFolderChanged = [this]() {
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
        videoRandomizer3.shuffleQueue.clear(); videoRandomizer3.currentShuffleIndex = 0;
    };

    cb.onFolderChanged2 = [this]() {
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
        videoRandomizer3.shuffleQueue.clear(); videoRandomizer3.currentShuffleIndex = 0;
    };

    cb.onFolderChanged3 = [this]() {
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
        videoRandomizer3.shuffleQueue.clear(); videoRandomizer3.currentShuffleIndex = 0;
    };

    cb.onRandomizeVideo = [this]() {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        if (assets.size() > 1) reloadVideoAtIndex(pickNextVideoIndex(assets), assets);
        isReloadingVideo = false;
    };

    cb.onRandomizeVideo2 = [this]() {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        if (assets.size() > 1) reloadVideoAtIndex2(pickNextVideoIndex2(assets), assets);
        isReloadingVideo = false;
    };

    cb.onRandomizeVideo3 = [this]() {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        if (assets.size() > 1) reloadVideoAtIndex3(pickNextVideoIndex3(assets), assets);
        isReloadingVideo = false;
    };

    cb.onRandomizePreviewVideo1 = [this]() {
        uiSystem.forcePreviewShuffle(0);
    };

    cb.onRandomizePreviewVideo2 = [this]() {
        uiSystem.forcePreviewShuffle(1);
    };

    cb.onRandomizePreviewVideo3 = [this]() {
        uiSystem.forcePreviewShuffle(2);
    };

    cb.onJumpRandom = [this]() {
        if (!videoSubsystemInitialized) return;
        const double dur = videoPlayer.durationSeconds();
        if (dur > 0) videoPlayer.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));
    };

    cb.onReloadVideo  = [this](const std::string& path) {
        if (!canChangeVideo()) return;
        reloadVideoSlot(0, path);
    };
    cb.onReloadVideo2 = [this](const std::string& path) {
        if (!canChangeVideo()) return;
        reloadVideoSlot(1, path);
    };
    cb.onReloadVideo3 = [this](const std::string& path) {
        if (!canChangeVideo()) return;
        reloadVideoSlot(2, path);
    };

    cb.onGetVideoSpeed = [this](const std::string& path) -> float {
        auto it = videoSpeedCache.find(path);
        return (it != videoSpeedCache.end()) ? it->second : 1.0f;
    };
    cb.onSetVideoSpeed = [this](const std::string& path, float speed) {
        videoSpeedCache[path] = speed;
    };

    cb.onVideoAssetsRootChanged = [this](const std::string& newPath) {
        if (newPath.empty() || !std::filesystem::exists(newPath) || !std::filesystem::is_directory(newPath)) {
            std::cerr << "[Application] onVideoAssetsRootChanged: invalid path '" << newPath << "'\n";
            return;
        }
        videoAssetsRoot = newPath;
        std::cout << "[Application] Video assets root changed to: " << videoAssetsRoot << "\n";

        // Re-scan registry with new root
        videoRegistry.scan(videoAssetsRoot);

        // Reset folder selections so they fall back to "All Folders"
        visualControls.playback.selectedVideoFolder.clear();
        visualControls.playback.selectedVideo2Folder.clear();
        visualControls.playback.selectedVideo3Folder.clear();

        // Clear saved paths so resolvePath picks fresh defaults from the new root
        videoSourcePath.clear();
        videoSourcePath2.clear();
        videoSourcePath3.clear();
        selectedVideoAsset = 0;
        selectedVideoAsset2 = 0;
        selectedVideoAsset3 = 0;

        // Reinitialize video players from the new root
        initVideo();
        initOutputVideo();

        // If MultiPassPipeline wasn't initialized (e.g. app started with no video),
        // initialize it now that video textures are available.
        if (videoSubsystemInitialized && !multiPassPipeline.isInitialized()) {
            initMultiPassPipeline();
        }

        updateAllDescriptorSets();
        saveState();
    };

    cb.onListPresets = [this]() { return listPresets(); };
    cb.onSavePreset = [this](const std::string& name) { return savePreset(name); };
    cb.onLoadPreset = [this](const std::string& name) { return loadPreset(name); };
    cb.onDeletePreset = [this](const std::string& name) { return deletePreset(name); };
    cb.onRenamePreset = [this](const std::string& oldName, const std::string& newName) { return renamePreset(oldName, newName); };

    return cb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform buffer
// ─────────────────────────────────────────────────────────────────────────────

void Application::updateUniformBuffer(uint32_t frameIndex, VisualControls& controls,
                                      UniformBufferManager& uboManager,
                                      const VulkanPresenter& presenter,
                                      AnimState& anim,
                                      VideoTexture& vid1, VideoTexture& vid2, VideoTexture& vid3,
                                      bool vid1Init, bool vid2Init, bool vid3Init,
                                      bool forOutput) {
    GlobalParamsUBO ubo{};

    // Calculate time delta and accumulate (similar to your system)
    auto currentTime = std::chrono::high_resolution_clock::now();
    if (!anim.timeInitialized) {
        anim.lastGlobalTime = currentTime;
        anim.accumulatedTime = 0.0f;
        anim.timeInitialized = true;
    }

    float globalDeltaTime = std::chrono::duration<float>(currentTime - anim.lastGlobalTime).count();
    anim.lastGlobalTime = currentTime;

    // Apply animation speed to delta
    float speed = std::max(0.01f, controls.playback.animationSpeed);
    anim.accumulatedTime += globalDeltaTime * speed;

    anim.debugElapsed = anim.accumulatedTime;
    anim.debugDelta = globalDeltaTime;
    float time = anim.accumulatedTime;

    constexpr float kTwoPi = 6.28318530718f;
    if (controls.playback.enableTempoLfo) {
        float lfoSpeed = std::max(0.01f, controls.playback.tempoLfoSpeed);
        float phaseAdvance = globalDeltaTime * lfoSpeed * kTwoPi;
        controls.playback.tempoLfoPhase = std::fmod(controls.playback.tempoLfoPhase + phaseAdvance, kTwoPi);
        if (controls.playback.tempoLfoPhase < 0.0f) controls.playback.tempoLfoPhase += kTwoPi;
    }

    // Update audio values from AudioSystem (normalized + smoothed)
    auto normalizeAudioLevel = [](float rawValue, float gain, float gamma) {
        float scaled = std::clamp(rawValue * gain, 0.0f, 1.0f);
        return (gamma != 1.0f) ? std::pow(scaled, gamma) : scaled;
    };

    float inputGain = controls.audio.inputGain;
    float liveEnergy = normalizeAudioLevel(audioSystem.getRMS() * inputGain,           1.0f, 0.85f);
    float liveBass   = normalizeAudioLevel(audioSystem.getSmoothedBass() * inputGain,  2.0f, 1.10f);
    float liveMid    = normalizeAudioLevel(audioSystem.getSmoothedMid() * inputGain,   2.0f, 1.00f);
    float liveHigh   = normalizeAudioLevel(audioSystem.getSmoothedHigh() * inputGain,   2.0f, 1.00f);

    // Per-band EQ gains
    liveBass = std::clamp(liveBass * controls.audio.bassGain, 0.0f, 1.0f);
    liveMid  = std::clamp(liveMid  * controls.audio.midGain,  0.0f, 1.0f);
    liveHigh = std::clamp(liveHigh * controls.audio.highGain, 0.0f, 1.0f);

    float tempoValue;
    if (controls.system.enableAudioReactive) {
        // Auto-tempo driven by audio energy: 0x (silence) to 5x (loud)
        // Power curve makes it harder to reach 5 (needs very high energy)
        float drive = controls.audio.reactiveDrive;
        float curvedEnergy = std::pow(liveEnergy, 1.5f);
        tempoValue = curvedEnergy * 5.0f * drive;
        tempoValue = std::clamp(tempoValue, 0.0f, 5.0f);
        // Sync playback.tempo so the UI slider moves too
        controls.playback.tempo = tempoValue;
        // Note: video playback rate is NOT synced to tempo — videos keep their manual speed
    } else {
        tempoValue = controls.playback.tempo;
        if (controls.playback.enableTempoLfo) {
            float lfoValue = std::sin(controls.playback.tempoLfoPhase);
            tempoValue += controls.playback.tempoLfoDepth * lfoValue;
        }
        tempoValue = std::clamp(tempoValue, 0.05f, 8.0f);
    }

    // Shader time: accumulates at tempo speed so procedural layers
    // (Layer 0, Layer 1, Anaglyph) animate with audio energy
    if (controls.system.enableAudioReactive) {
        anim.shaderTime += globalDeltaTime * tempoValue;
    } else {
        anim.shaderTime += globalDeltaTime * std::max(0.01f, controls.playback.animationSpeed);
    }

    auto& reactive = controls.runtime.audioReactive;
    reactive.enabled = controls.system.enableAudioReactive;
    reactive.energy = liveEnergy;
    reactive.bass   = liveBass;
    reactive.mid    = liveMid;
    reactive.high   = liveHigh;

    if (controls.system.enableAudioReactive) {
        controls.audio.energy = liveEnergy;
        controls.audio.bass   = liveBass;
        controls.audio.mid    = liveMid;
        controls.audio.high   = liveHigh;
    }

    // Set basic UBO values
    ubo.model = glm::mat4(1.0f);
    ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0), glm::vec3(0.0, 1.0, 0.0));
    float extW = static_cast<float>(presenter.getExtent().width);
    float extH = static_cast<float>(presenter.getExtent().height);
    float aspect = (extH > 0.0f) ? (extW / extH) : 1.0f;
    ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1.0f;
    ubo.resolution = glm::vec2(static_cast<float>(presenter.getExtent().width),
                               static_cast<float>(presenter.getExtent().height));
    ubo.videoResolution = glm::vec2(static_cast<float>(vid1.getWidth()),
                                    static_cast<float>(vid1.getHeight()));
    ubo.time = controls.system.enableAudioReactive ? anim.shaderTime : time;
    ubo.mode = controls.playback.activeMode;
    ubo.videoMix = controls.playback.videoMix;
    ubo.videoAvailable = (vid1Init && vid1.isReady()) ? 1.0f : 0.0f;
    ubo.video2Mix = controls.playback.video2Mix;
    ubo.video2Available = (vid2Init && vid2.isReady()) ? 1.0f : 0.0f;
    ubo.video2BlendMode = controls.playback.video2BlendMode;
    ubo.video3Mix = controls.playback.video3Mix;
    ubo.video3Available = (vid3Init && vid3.isReady()) ? 1.0f : 0.0f;
    ubo.video3BlendMode = controls.playback.video3BlendMode;
    ubo.outputAspectRatio = controls.playback.outputAspectRatio;

    // Set visual control values
    ubo.primaryColor = controls.color.primaryColor;
    ubo.secondaryColor = controls.color.secondaryColor;
    ubo.colorBlend = controls.color.colorBlend;
    ubo.tempo = tempoValue;
    ubo.energy = controls.audio.energy;
    ubo.bass = controls.audio.bass;
    ubo.mid = controls.audio.mid;
    ubo.high = controls.audio.high;
    ubo.audioReactiveDrive = controls.audio.reactiveDrive;
    
    // Audio reactivity parameters
    ubo.audioWarpResponse = controls.audio.warpResponse;
    ubo.audioFeedbackResponse = controls.audio.feedbackResponse;
    ubo.audioBlurResponse = controls.audio.blurResponse;
    ubo.audioColorResponse = controls.audio.colorResponse;
    ubo.audioGlitchResponse = controls.audio.glitchResponse;
    ubo.audioBeatSync = controls.audio.beatSync;
    ubo.audioLfoRate = controls.audio.lfoRate;
    ubo.enableAudioReactive = controls.system.enableAudioReactive ? 1 : 0;
    
    // Post FX basicos
    ubo.grayscaleAmount = controls.playback.grayscaleAmount;
    ubo.sharpenAmount = controls.playback.sharpenAmount;
    ubo.upscaleEnabled = controls.playback.upscaleEnabled ? 1.0f : 0.0f;

    // Enable/Disable flags for post FX
    ubo.enablePostCrtCurvature = controls.post.enablePostCrtCurvature ? 1 : 0;
    ubo.enablePostScanMask = controls.post.enablePostScanMask ? 1 : 0;
    ubo.enablePostVignette = controls.post.enablePostVignette ? 1 : 0;
    ubo.enablePostFishEye = controls.post.enablePostFishEye ? 1 : 0;
    ubo.enablePostBloom = controls.post.enablePostBloom ? 1 : 0;
    ubo.enablePostAberration = controls.post.enablePostAberration ? 1 : 0;
    ubo.enablePostGrain = controls.post.enablePostGrain ? 1 : 0;
    ubo.enablePostBend = controls.post.enablePostBend ? 1 : 0;
    ubo.enablePostGlitch = controls.post.enablePostGlitch ? 1 : 0;
    ubo.enablePostColorBalance = controls.post.enablePostColorBalance ? 1 : 0;

    // Enable/Disable flags for VJAY BASICS
    ubo.enableColorGrading = controls.color.enableColorGrading ? 1 : 0;
    ubo.enableFeedback = controls.temporal.enableFeedback ? 1 : 0;
    ubo.enableDistortion = controls.fx.enableDistortion ? 1 : 0;
    ubo.enableBlurMotion = controls.fx.enableBlurMotion ? 1 : 0;
    ubo.enableSharpen = controls.fx.enableSharpen ? 1 : 0;
    ubo.enablePostGlitch = controls.fx.enableGlitch ? 1 : 0;
    ubo.enableBlending = controls.blending.enableBlending ? 1 : 0;
    ubo.enableAnalog = controls.post.enableAnalog ? 1 : 0;
    ubo.enableAudioReactive = controls.system.enableAudioReactive ? 1 : 0;
    ubo.enableTemporal = controls.temporal.enableTemporal ? 1 : 0;

    // Enable/Disable flags for VJAY EXTRA
    ubo.enablePixelate = controls.fx.enablePixelate ? 1 : 0;
    ubo.enableStrobe = controls.fx.enableStrobe ? 1 : 0;
    ubo.enableThreshold = controls.fx.enableThreshold ? 1 : 0;
    ubo.enableSlowZoom = controls.fx.enableSlowZoom ? 1 : 0;
    ubo.enableMirror = controls.fx.enableMirror ? 1 : 0;
    ubo.enableInvert = controls.fx.enableInvert ? 1 : 0;
    ubo.enablePosterize = controls.fx.enablePosterize ? 1 : 0;
    ubo.enableInfrared = controls.fx.enableInfrared ? 1 : 0;
    ubo.enableZoomPulse = controls.fx.enableZoomPulse ? 1 : 0;
    ubo.enableRGBShift = controls.fx.enableRGBShift ? 1 : 0;

    // CRT
    ubo.crtCurvature = controls.post.crtCurvature;
    ubo.crtHorizontalCurvature = controls.post.crtHorizontalCurvature;
    ubo.crtScanlineIntensity = controls.post.crtScanlineIntensity;
    ubo.crtMaskIntensity = controls.post.crtMaskIntensity;
    ubo.crtVignette = controls.post.crtVignette;
    ubo.crtFishEye = controls.post.crtFishEye;
    
    // Bloom
    ubo.bloomIntensity = controls.post.bloomIntensity;
    ubo.bloomThreshold = controls.post.bloomThreshold;
    
    // Aberracion / grano / bend / glitch
    ubo.aberrationAmount = controls.post.aberrationAmount;
    ubo.grainStrength = controls.post.grainStrength;
    ubo.bendAmount = controls.fx.bendAmount;
    ubo.glitchAmount = controls.fx.glitchAmount;
    
    // Color grading
    ubo.colorBalance = controls.color.colorBalance;
    ubo.gradeBrightness = controls.color.gradeBrightness;
    ubo.gradeContrast = controls.color.gradeContrast;
    ubo.gradeSaturation = controls.color.gradeSaturation;
    ubo.gradeHueShift = controls.color.gradeHueShift;
    ubo.gradeGamma = controls.color.gradeGamma;
    ubo.colorLUTIndex = controls.color.colorLUTIndex;
    ubo.splitToneBalance = controls.color.splitToneBalance;
    ubo.splitToneShadows = controls.color.splitToneShadows;
    ubo.splitToneHighlights = controls.color.splitToneHighlights;
    
    // Feedback temporal
    ubo.feedbackAmount = controls.temporal.feedbackAmount;
    ubo.trailStrength = controls.temporal.trailStrength;
    ubo.temporalAccumulation = controls.temporal.temporalAccumulation;
    ubo.feedbackDecay = controls.temporal.feedbackDecay;
    ubo.recursiveBlend = controls.temporal.recursiveBlend;
    
    // Distorsion espacial
    ubo.uvWarpStrength = controls.fx.uvWarpStrength;
    ubo.rippleStrength = controls.fx.rippleStrength;
    ubo.rippleFrequency = controls.fx.rippleFrequency;
    ubo.swirlStrength = controls.fx.swirlStrength;
    ubo.displacementAmount = controls.fx.displacementAmount;
    ubo.kaleidoSegments = controls.fx.kaleidoSegments;
    ubo.tunnelDepth = controls.fx.tunnelDepth;
    ubo.tunnelCurvature = controls.fx.tunnelCurvature;
    
    // Blur y motion
    ubo.gaussianBlur = controls.fx.gaussianBlur;
    ubo.directionalBlur = controls.fx.directionalBlur;
    ubo.directionalBlurAngle = controls.fx.directionalBlurAngle;
    ubo.zoomBlur = controls.fx.zoomBlur;
    ubo.motionBlur = controls.fx.motionBlur;
    ubo.temporalBlur = controls.fx.temporalBlur;
    
    // Sharpening
    ubo.unsharpMask = controls.fx.unsharpMask;
    ubo.casAmount = controls.fx.casAmount;
    ubo.localContrast = controls.fx.localContrast;
    
    // Glitch detallado
    ubo.glitchDatamosh = controls.fx.glitchDatamosh;
    ubo.glitchRGBSplit = controls.fx.glitchRGBSplit;
    ubo.glitchScanlineBreak = controls.fx.glitchScanlineBreak;
    ubo.glitchJitter = controls.fx.glitchJitter;
    ubo.glitchTearing = controls.fx.glitchTearing;
    ubo.glitchPixelSort = controls.fx.glitchPixelSort;
    ubo.glitchBufferCorruption = controls.fx.glitchBufferCorruption;
    
    // Blending / compositing
    ubo.blendModeProcedural = controls.blending.blendModeProcedural;
    ubo.blendModeVideo = controls.blending.blendModeVideo;
    ubo.blendModeFeedback = controls.blending.blendModeFeedback;
    ubo.blendProceduralMix = controls.blending.blendProceduralMix;
    ubo.blendVideoMix = controls.blending.blendVideoMix;
    ubo.blendFeedbackMix = controls.blending.blendFeedbackMix;
    
    // Analog / CRT avanzado
    ubo.analogScanlineFocus = controls.post.analogScanlineFocus;
    ubo.analogMaskBalance = controls.post.analogMaskBalance;
    
    // Temporal
    ubo.frameAccumulation = controls.playback.frameAccumulation;
    ubo.slowMotionFactor = controls.playback.slowMotionFactor;
    ubo.temporalInterpolation = controls.playback.temporalInterpolation;
    
    // Efectos extra (VJAY EXTRA)
    ubo.pixelateAmount = controls.fx.pixelateAmount;
    ubo.strobeSpeed = controls.fx.strobeSpeed;
    ubo.thresholdLevel = controls.fx.thresholdLevel;
    ubo.slowZoomAmount = controls.fx.slowZoomAmount;
    ubo.enableEdgeDetect = controls.fx.enableEdgeDetect ? 1 : 0;
    ubo.edgeStrength = controls.fx.edgeStrength;
    ubo.edgeThreshold = controls.fx.edgeThreshold;
    ubo.edgeBlend = controls.fx.edgeBlend;
    ubo.edgeColor = controls.fx.edgeColor;
    ubo.mirrorAmount = controls.fx.mirrorAmount;
    ubo.posterizeLevels = controls.fx.posterizeLevels;
    ubo.zoomPulseAmount = controls.fx.zoomPulseAmount;
    ubo.rgbShiftAmount = controls.fx.rgbShiftAmount;
    
    // FXAA
    ubo.enableFXAA = controls.system.enableFXAA ? 1 : 0;
    ubo.fxaaQualitySubpix = controls.system.fxaaQualitySubpix;
    ubo.fxaaQualityEdgeThreshold = controls.system.fxaaQualityEdgeThreshold;
    ubo.fxaaQualityEdgeThresholdMin = controls.system.fxaaQualityEdgeThresholdMin;

    // Grid / Mirroring
    ubo.enableGrid = controls.grid.enabled ? 1 : 0;
    ubo.gridMode = controls.grid.mode;
    ubo.gridCount = controls.grid.count;
    ubo.gridRows = controls.grid.rows;
    ubo.gridColumns = controls.grid.columns;
    ubo.gridMirrorCells = controls.grid.mirrorCells ? 1 : 0;
    ubo.gridShowLines = controls.grid.showLines ? 1 : 0;
    ubo.gridLineWidth = controls.grid.lineWidth;
    ubo.gridLineIntensity = controls.grid.lineIntensity;
    ubo.gridLineColor = controls.grid.lineColor;

    // Camera movement
    ubo.cameraZoom = controls.camera.zoom;
    ubo.cameraPanX = controls.camera.panX;
    ubo.cameraPanY = controls.camera.panY;
    ubo.cameraRotation = controls.camera.rotation;
    ubo.enableCameraMovement = controls.camera.enableMovement ? 1 : 0;

    // Final RGB overlay
    ubo.rgbOverlay = controls.color.rgbOverlay;
    ubo.enableRgbOverlay = controls.color.enableRgbOverlay ? 1 : 0;

    // Master brightness (independent from color grading)
    ubo.masterBrightness = controls.post.masterBrightness;

    // ------------------------------------------------------------------
    // AUDIO REACTIVITY AUTO-MODULATION
    // When enabled, audio levels automatically drive effect intensities
    // so the layers animate without manual slider tweaking.
    // ------------------------------------------------------------------
    float envClamped  = std::clamp(controls.audio.energy, 0.0f, 1.0f);
    float bassClamped = std::clamp(controls.audio.bass,   0.0f, 1.0f);
    float midClamped  = std::clamp(controls.audio.mid,    0.0f, 1.0f);
    float highClamped = std::clamp(controls.audio.high,   0.0f, 1.0f);

    float warpGain     = std::max(0.0f, controls.audio.warpResponse);
    float feedbackGain = std::max(0.0f, controls.audio.feedbackResponse);
    float blurGain     = std::max(0.0f, controls.audio.blurResponse);
    float colorGain    = std::max(0.0f, controls.audio.colorResponse);
    float glitchGain   = std::max(0.0f, controls.audio.glitchResponse);

    if (controls.system.enableAudioReactive) {
        // Spatial distortion (Pass B)
        ubo.uvWarpStrength    = std::max(ubo.uvWarpStrength,    envClamped  * 0.15f * warpGain);
        ubo.rippleStrength    = std::max(ubo.rippleStrength,    bassClamped * 0.30f * warpGain);
        ubo.swirlStrength     = std::max(ubo.swirlStrength,     bassClamped * 0.20f * warpGain);
        ubo.displacementAmount= std::max(ubo.displacementAmount,envClamped  * 0.12f * warpGain);
        ubo.bendAmount        = std::max(ubo.bendAmount,        envClamped  * 0.10f * warpGain);

        // Temporal / feedback (Pass D)
        ubo.feedbackAmount    = std::max(ubo.feedbackAmount,    envClamped  * 0.25f * feedbackGain);
        ubo.trailStrength     = std::max(ubo.trailStrength,     bassClamped * 0.20f * feedbackGain);

        // Degradation / glitch (Pass E)
        ubo.glitchJitter      = std::max(ubo.glitchJitter,      envClamped  * 0.15f * glitchGain);
        ubo.glitchRGBSplit    = std::max(ubo.glitchRGBSplit,    bassClamped * 0.10f * glitchGain);
        ubo.glitchTearing     = std::max(ubo.glitchTearing,     midClamped  * 0.08f * glitchGain);
        ubo.grainStrength     = std::max(ubo.grainStrength,     envClamped  * 0.20f * blurGain);

        // Output extras (Pass G)
        ubo.zoomPulseAmount   = std::max(ubo.zoomPulseAmount,   envClamped  * 0.25f * colorGain);
        ubo.slowZoomAmount    = std::max(ubo.slowZoomAmount,    envClamped  * 0.30f * blurGain);
        ubo.strobeSpeed       = std::max(ubo.strobeSpeed,       bassClamped * 3.0f * colorGain);
        ubo.rgbShiftAmount    = std::max(ubo.rgbShiftAmount,    highClamped * 0.03f * colorGain);

        // Camera movement (2D layer camera)
        if (controls.camera.enableMovement) {
            // Strong zoom pulse driven by bass (max 20% closer)
            float zoomPulse = 1.0f + (bassClamped * 0.20f + envClamped * 0.06f) * warpGain;
            ubo.cameraZoom = std::max(ubo.cameraZoom, zoomPulse);

            // Fast orbit rotation driven by energy
            ubo.cameraRotation += envClamped * 0.8f * globalDeltaTime * warpGain;

            // Strong X pan driven by bass — side-to-side bounce with the beat
            float panXAudio = bassClamped * 0.55f * std::sin(ubo.time * 3.0f) * warpGain;
            panXAudio += bassClamped * bassClamped * 0.30f * std::sin(ubo.time * 7.0f + 1.0f) * warpGain;
            ubo.cameraPanX = controls.camera.panX + panXAudio;

            // Y pan driven by mids/highs for floating feel
            float panYAudio = midClamped * 0.40f * std::cos(ubo.time * 2.5f) * warpGain;
            panYAudio += highClamped * 0.20f * std::sin(ubo.time * 5.5f) * warpGain;
            ubo.cameraPanY = controls.camera.panY + panYAudio;
        }
    }

    reactive.uvWarpStrength     = ubo.uvWarpStrength;
    reactive.rippleStrength     = ubo.rippleStrength;
    reactive.swirlStrength      = ubo.swirlStrength;
    reactive.displacementAmount = ubo.displacementAmount;
    reactive.bendAmount         = ubo.bendAmount;
    reactive.feedbackAmount     = ubo.feedbackAmount;
    reactive.trailStrength      = ubo.trailStrength;
    reactive.glitchJitter       = ubo.glitchJitter;
    reactive.glitchRGBSplit     = ubo.glitchRGBSplit;
    reactive.glitchTearing      = ubo.glitchTearing;
    reactive.grainStrength      = ubo.grainStrength;
    reactive.zoomPulseAmount    = ubo.zoomPulseAmount;
    reactive.slowZoomAmount     = ubo.slowZoomAmount;
    reactive.strobeSpeed        = ubo.strobeSpeed;
    reactive.rgbShiftAmount     = ubo.rgbShiftAmount;
    reactive.cameraZoom         = ubo.cameraZoom;
    reactive.cameraPanX         = ubo.cameraPanX;
    reactive.cameraPanY         = ubo.cameraPanY;
    reactive.cameraRotation     = ubo.cameraRotation;

    // Video transition crossfade progress
    // For the output window we always use 1.0 so that preview-only transitions
    // do not disturb the output renderer.
    ubo.transitionProgress = forOutput ? 1.0f : transitionProgress;

    // Pass culling: disable GPU passes whose effect intensity is zero after
    // audio reactivity has been applied. This saves a fullscreen drawcall and
    // its associated render pass when the pass would be a no-op.
    MultiPassPipeline& pipeline = forOutput ? outputMultiPassPipeline : multiPassPipeline;

    const bool detailPassActive =
        ubo.gaussianBlur != 0.0f || ubo.directionalBlur != 0.0f || ubo.zoomBlur != 0.0f ||
        ubo.motionBlur != 0.0f || ubo.temporalBlur != 0.0f ||
        ubo.unsharpMask != 0.0f || ubo.casAmount != 0.0f || ubo.localContrast != 0.0f;
    pipeline.setPassEnabled(2, detailPassActive);

    const bool degradationPassActive =
        ubo.glitchAmount != 0.0f || ubo.glitchDatamosh != 0.0f || ubo.glitchRGBSplit != 0.0f ||
        ubo.glitchScanlineBreak != 0.0f || ubo.glitchJitter != 0.0f || ubo.glitchTearing != 0.0f ||
        ubo.glitchPixelSort != 0.0f || ubo.glitchBufferCorruption != 0.0f;
    pipeline.setPassEnabled(4, degradationPassActive);

    pipeline.setPassEnabled(5, ubo.enableColorGrading != 0);

    // Selectable post-effect slot parameters
    ubo.postEffectStrength = controls.post.postEffectStrength;
    ubo.postEffectIntensity = controls.post.postEffectIntensity;
    ubo.postEffectMode = controls.post.postEffectMode;
    ubo.postEffectBass = controls.audio.bass;
    ubo.postEffectRgbAdjust = controls.post.postEffectRgbAdjust;

    uboManager.update(frameIndex, ubo, vulkanContext.getDevice());
}

// ─────────────────────────────────────────────────────────────────────────────
// Record command buffers (separate per window)
// ─────────────────────────────────────────────────────────────────────────────

void Application::recordPreviewCommandBuffer(VkCommandBuffer cmd,
                                              FrameContext& previewFrame, uint32_t previewImageIndex) {
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        throw std::runtime_error("failed to begin preview command buffer");

    videoTexture.recordPendingUpload(cmd, previewFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (videoSubsystemInitialized2 && videoTexture2.isReady())
        videoTexture2.recordPendingUpload(cmd, previewFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (videoSubsystemInitialized3 && videoTexture3.isReady())
        videoTexture3.recordPendingUpload(cmd, previewFrame.frameIndex, vulkanContext.getGraphicsQueue());

    if (previewPaused) {
        VkClearValue clear{ .color = {0.f, 0.f, 0.f, 1.f} };
        const auto ext = previewPresenter.getExtent();
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass        = renderPass;
        rp.framebuffer       = previewPresenter.getFramebuffers()[previewImageIndex];
        rp.renderArea.extent = ext;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fullscreenPipeline);
        VkDescriptorSet ds = fullscreenDescriptorSets[previewFrame.frameIndex];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
        int previewOverlay = 2; // PAUSED
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &previewOverlay);

        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0, 1 };
        VkRect2D   sc{ {0,0}, ext };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    } else if (videoSubsystemInitialized && multiPassPipeline.isInitialized()) {
        multiPassPipeline.execute(
            cmd, previewFrame.frameIndex,
            descriptorSetManager.getSet(previewFrame.frameIndex),
            renderPass, previewPresenter.getFramebuffers(), previewImageIndex,
            fullscreenPipeline, pipelineLayout,
            fullscreenDescriptorSets[previewFrame.frameIndex],
            previewPresenter.getExtent(), swapchainSampler,
            1 // previewOverlay
        );
    } else {
        VkClearValue clear{ .color = {0.f, 0.f, 0.f, 1.f} };
        const auto ext = previewPresenter.getExtent();
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass        = renderPass;
        rp.framebuffer       = previewPresenter.getFramebuffers()[previewImageIndex];
        rp.renderArea.extent = ext;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fullscreenPipeline);
        VkDescriptorSet ds = fullscreenDescriptorSets[previewFrame.frameIndex];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
        int previewOverlay = 1;
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &previewOverlay);

        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0, 1 };
        VkRect2D   sc{ {0,0}, ext };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to end preview command buffer");
}

void Application::recordOutputCommandBuffer(VkCommandBuffer cmd,
                                             FrameContext& outputFrame, uint32_t outputImageIndex) {
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        throw std::runtime_error("failed to begin output command buffer");

    if (outputVideoSubsystemInitialized && outputVideoTexture.isReady())
        outputVideoTexture.recordPendingUpload(cmd, outputFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (outputVideoSubsystemInitialized2 && outputVideoTexture2.isReady())
        outputVideoTexture2.recordPendingUpload(cmd, outputFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (outputVideoSubsystemInitialized3 && outputVideoTexture3.isReady())
        outputVideoTexture3.recordPendingUpload(cmd, outputFrame.frameIndex, vulkanContext.getGraphicsQueue());

    if (outputVideoSubsystemInitialized && outputMultiPassPipeline.isInitialized()) {
        outputMultiPassPipeline.execute(
            cmd, outputFrame.frameIndex,
            outputDescriptorSetManager.getSet(outputFrame.frameIndex),
            renderPass, outputPresenter.getFramebuffers(), outputImageIndex,
            fullscreenPipeline, pipelineLayout,
            outputFullscreenDescriptorSets[outputFrame.frameIndex],
            outputPresenter.getExtent(), swapchainSampler,
            0 // no overlay on output
        );
    } else {
        const auto outExt = outputPresenter.getExtent();
        VkClearValue clear{ .color = {0.f, 0.f, 0.f, 1.f} };
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass        = renderPass;
        rp.framebuffer       = outputPresenter.getFramebuffers()[outputImageIndex];
        rp.renderArea.extent = outExt;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmd);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to end output command buffer");
}

// ─────────────────────────────────────────────────────────────────────────────
// Cleanup
// ─────────────────────────────────────────────────────────────────────────────

void Application::cleanup() {
    if (!initializationComplete) return;
    vkDeviceWaitIdle(vulkanContext.getDevice());

    saveState();
    uiSystem.shutdown();
    previewFrameSystem.cleanup();
    outputFrameSystem.cleanup();

    // Destroy VideoRenderers FIRST to stop decoder threads before
    // freeing the FFmpeg resources they access (formatCtx, etc.)
    videoRenderer.reset();
    videoRenderer2.reset();
    videoRenderer3.reset();
    outputVideoRenderer.reset();
    outputVideoRenderer2.reset();
    outputVideoRenderer3.reset();

    videoPlayer.shutdown();
    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture.cleanup(resourceSystem);
    videoPlayer2.shutdown();
    videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture2.cleanup(resourceSystem);
    videoPlayer3.shutdown();
    videoTexture3.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture3.cleanup(resourceSystem);
    outputVideoPlayer.shutdown();
    outputVideoTexture.destroy(resourceSystem, vulkanContext.getDevice());
    outputVideoTexture.cleanup(resourceSystem);
    outputVideoPlayer2.shutdown();
    outputVideoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
    outputVideoTexture2.cleanup(resourceSystem);
    outputVideoPlayer3.shutdown();
    outputVideoTexture3.destroy(resourceSystem, vulkanContext.getDevice());
    outputVideoTexture3.cleanup(resourceSystem);

    uniformBufferManager.destroy(resourceSystem, vulkanContext.getDevice());
    outputUniformBufferManager.destroy(resourceSystem, vulkanContext.getDevice());
    descriptorSetManager.destroy(vulkanContext.getDevice());
    outputDescriptorSetManager.destroy(vulkanContext.getDevice());
    multiPassPipeline.cleanup();
    outputMultiPassPipeline.cleanup();

    // Shutdown audio/MIDI/OSC systems before ResourceSystem cleanup
    audioSystem.shutdown();
    midiSystem.shutdown();
    oscSystem.shutdown();

    if (fullscreenPipeline          != VK_NULL_HANDLE) vkDestroyPipeline           (vulkanContext.getDevice(), fullscreenPipeline,          nullptr);
    if (pipelineLayout              != VK_NULL_HANDLE) vkDestroyPipelineLayout      (vulkanContext.getDevice(), pipelineLayout,              nullptr);
    if (fullscreenDescriptorPool    != VK_NULL_HANDLE) vkDestroyDescriptorPool      (vulkanContext.getDevice(), fullscreenDescriptorPool,    nullptr);
    if (outputFullscreenDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool   (vulkanContext.getDevice(), outputFullscreenDescriptorPool, nullptr);
    if (fullscreenDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vulkanContext.getDevice(), fullscreenDescriptorSetLayout, nullptr);
    if (swapchainSampler            != VK_NULL_HANDLE) vkDestroySampler             (vulkanContext.getDevice(), swapchainSampler,            nullptr);
    if (renderPass                  != VK_NULL_HANDLE) vkDestroyRenderPass          (vulkanContext.getDevice(), renderPass,                  nullptr);

    previewPresenter.cleanup();
    outputPresenter.cleanup();

    if (vertexBufferHandle.type != ResourceType::Unknown)
        resourceSystem.destroy(vertexBufferHandle);

    resourceSystem.cleanup();
}
