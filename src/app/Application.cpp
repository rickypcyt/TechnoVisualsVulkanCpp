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
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>
#include <future>
#include <thread>
#include <functional>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "VideoRotationStore.h"
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
    const bool vid1Ready = previewSlots[0].texture.isReady();
    const bool outVid1Ready = outputSlots[0].texture.isReady();

    // Descriptor sets principales (multipass) — only bind image if texture is ready
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            uniformBufferManager.getBuffers()[i],
            vid1Ready ? const_cast<VkDescriptorImageInfo*>(&previewSlots[0].texture.getDescriptorInfo()) : nullptr,
            vid1Ready ? const_cast<VkDescriptorImageInfo*>(&previewSlots[0].texture.getPrevDescriptorInfo()) : nullptr
        );
    }
    // Output descriptor sets
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        outputDescriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            outputUniformBufferManager.getBuffers()[i],
            outVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputSlots[0].texture.getDescriptorInfo()) : nullptr,
            outVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputSlots[0].texture.getPrevDescriptorInfo()) : nullptr
        );
    }
    updateFullscreenDescriptorSets();

    // Multipass pipeline — skip if texture not ready OR pipeline not yet initialized
    if (vid1Ready && multiPassPipeline.isInitialized()) {
        const bool has2 = previewSlots[1].texture.isReady();
        const bool has3 = previewSlots[2].texture.isReady();
        multiPassPipeline.updateDescriptorSets(
            uniformBufferManager.getBuffers(),
            previewSlots[0].texture.getImageView(),
            previewSlots[0].texture.getPrevImageView(),
            previewSlots[0].texture.getSampler(),
            previewSlots[0].texture.getSampler(),
            has2 ? previewSlots[1].texture.getImageView() : previewSlots[0].texture.getImageView(),
            has2 ? previewSlots[1].texture.getSampler()   : previewSlots[0].texture.getSampler(),
            has3 ? previewSlots[2].texture.getImageView() : previewSlots[0].texture.getImageView(),
            has3 ? previewSlots[2].texture.getSampler()   : previewSlots[0].texture.getSampler()
        );
    }
    if (outVid1Ready && outputMultiPassPipeline.isInitialized()) {
        const bool outputHas2 = outputSlots[1].texture.isReady();
        const bool outputHas3 = outputSlots[2].texture.isReady();
        outputMultiPassPipeline.updateDescriptorSets(
            outputUniformBufferManager.getBuffers(),
            outputSlots[0].texture.getImageView(),
            outputSlots[0].texture.getPrevImageView(),
            outputSlots[0].texture.getSampler(),
            outputSlots[0].texture.getSampler(),
            outputHas2 ? outputSlots[1].texture.getImageView() : outputSlots[0].texture.getImageView(),
            outputHas2 ? outputSlots[1].texture.getSampler()   : outputSlots[0].texture.getSampler(),
            outputHas3 ? outputSlots[2].texture.getImageView() : outputSlots[0].texture.getImageView(),
            outputHas3 ? outputSlots[2].texture.getSampler()   : outputSlots[0].texture.getSampler()
        );
    }
}

// Reconstruye GPU texture + CPU pool para un slot ya inicializado.
void Application::rebuildVideoTexture(int slot) {
    previewFrameSystem.waitForAllFences();
    outputFrameSystem.waitForAllFences();
    if (slot == 0) {
        previewSlots[0].texture.destroy(resourceSystem, vulkanContext.getDevice());
        previewSlots[0].texture.cleanup(resourceSystem);
        previewSlots[0].texture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(previewSlots[0].player.width()),
            static_cast<uint32_t>(previewSlots[0].player.height())
        );
        previewSlots[0].framePool.resize(
            static_cast<uint32_t>(previewSlots[0].player.width()),
            static_cast<uint32_t>(previewSlots[0].player.height()),
            static_cast<uint32_t>(previewSlots[0].player.width()) * 4
        );
        if (previewSlots[0].renderer) previewSlots[0].renderer->reset();
    } else if (slot == 1) {
        previewSlots[1].texture.destroy(resourceSystem, vulkanContext.getDevice());
        previewSlots[1].texture.cleanup(resourceSystem);
        previewSlots[1].texture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(previewSlots[1].player.width()),
            static_cast<uint32_t>(previewSlots[1].player.height())
        );
        previewSlots[1].framePool.resize(
            static_cast<uint32_t>(previewSlots[1].player.width()),
            static_cast<uint32_t>(previewSlots[1].player.height()),
            static_cast<uint32_t>(previewSlots[1].player.width()) * 4
        );
        if (previewSlots[1].renderer) previewSlots[1].renderer->reset();
    } else {
        previewSlots[2].texture.destroy(resourceSystem, vulkanContext.getDevice());
        previewSlots[2].texture.cleanup(resourceSystem);
        previewSlots[2].texture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(previewSlots[2].player.width()),
            static_cast<uint32_t>(previewSlots[2].player.height())
        );
        previewSlots[2].framePool.resize(
            static_cast<uint32_t>(previewSlots[2].player.width()),
            static_cast<uint32_t>(previewSlots[2].player.height()),
            static_cast<uint32_t>(previewSlots[2].player.width()) * 4
        );
        if (previewSlots[2].renderer) previewSlots[2].renderer->reset();
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
            previewSlots[0].lastPlaybackRate = speed;
        } else if (targetSlot == 1) {
            visualControls.playback.video2PlaybackRate = speed;
            previewSlots[1].lastPlaybackRate = speed;
        } else {
            visualControls.playback.video3PlaybackRate = speed;
            previewSlots[2].lastPlaybackRate = speed;
        }
    };

    // Start crossfade transition only if a previous video was loaded
    // (avoids sampling an uninitialized prev texture on first load)
    if (slot == 0 && previewSlots[0].texture.isReady()) {
        transitionActive = true;
        transitionProgress = 0.0f;
        previewSlots[0].texture.setFreezePrev(true);
    } else if (slot == 1 && previewSlots[1].texture.isReady()) {
        transitionActive = true;
        transitionProgress = 0.0f;
        previewSlots[1].texture.setFreezePrev(true);
    } else if (slot == 2 && previewSlots[2].texture.isReady()) {
        transitionActive = true;
        transitionProgress = 0.0f;
        previewSlots[2].texture.setFreezePrev(true);
    }

    if (slot == 0) {
        previewSlots[0].sourcePath = path;
        if (!loadingPreset) visualControls.playback.video1Rotation = VideoRotationStore::get(path);
        previewSlots[0].renderer.reset();
        previewSlots[0].player.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!previewSlots[0].player.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadVideoSlot(0) failed: " << path << '\n';
            previewSlots[0].isReloading = false;
            return false;
        }

        previewSlots[0].player.setAutoScale(visualControls.playback.autoScaleVideo);
        previewSlots[0].player.setPlaybackRate(visualControls.playback.videoPlaybackRate);
        rebuildVideoTexture(0);
        updateAllDescriptorSets();

        previewSlots[0].renderer = std::make_unique<VideoRenderer>(previewSlots[0].player, previewSlots[0].texture, previewSlots[0].framePool);
        previewSlots[0].randomizer.elapsedSeconds = 0.0f;
        previewSlots[0].randomizer.currentVideoDuration = previewSlots[0].player.durationSeconds();
        previewSlots[0].initialized = previewSlots[0].texture.isReady();
    } else if (slot == 1) {
        previewSlots[1].sourcePath = path;
        if (!loadingPreset) visualControls.playback.video2Rotation = VideoRotationStore::get(path);
        previewSlots[1].renderer.reset();
        previewSlots[1].player.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!previewSlots[1].player.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadVideoSlot(1) failed: " << path << '\n';
            previewSlots[1].isReloading = false;
            return false;
        }

        previewSlots[1].player.setAutoScale(visualControls.playback.autoScaleVideo);
        previewSlots[1].player.setPlaybackRate(visualControls.playback.video2PlaybackRate);
        rebuildVideoTexture(1);
        updateAllDescriptorSets();

        previewSlots[1].renderer = std::make_unique<VideoRenderer>(previewSlots[1].player, previewSlots[1].texture, previewSlots[1].framePool);
        previewSlots[1].randomizer.elapsedSeconds = 0.0f;
        previewSlots[1].randomizer.currentVideoDuration = previewSlots[1].player.durationSeconds();
        previewSlots[1].initialized = previewSlots[1].texture.isReady();
    } else {
        previewSlots[2].sourcePath = path;
        if (!loadingPreset) visualControls.playback.video3Rotation = VideoRotationStore::get(path);
        previewSlots[2].renderer.reset();
        previewSlots[2].player.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!previewSlots[2].player.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadVideoSlot(2) failed: " << path << '\n';
            previewSlots[2].isReloading = false;
            return false;
        }

        previewSlots[2].player.setAutoScale(visualControls.playback.autoScaleVideo);
        previewSlots[2].player.setPlaybackRate(visualControls.playback.video3PlaybackRate);
        rebuildVideoTexture(2);
        updateAllDescriptorSets();

        previewSlots[2].renderer = std::make_unique<VideoRenderer>(previewSlots[2].player, previewSlots[2].texture, previewSlots[2].framePool);
        previewSlots[2].randomizer.elapsedSeconds = 0.0f;
        previewSlots[2].randomizer.currentVideoDuration = previewSlots[2].player.durationSeconds();
        previewSlots[2].initialized = previewSlots[2].texture.isReady();
    }

    restoreSpeed(slot, path);

    if (slot == 0) previewSlots[0].isReloading = false;
    else if (slot == 1) previewSlots[1].isReloading = false;
    else previewSlots[2].isReloading = false;
    return true;
}

// Recarga un slot del output desde una ruta dada.
bool Application::reloadOutputVideoSlot(int slot, const std::string& path) {
    std::cout << "[reloadOutputVideoSlot] Reloading output slot " << slot << " with: " << path << "\n";

    if (slot == 0) {
        outputSlots[0].sourcePath = path;
        outputSlots[0].renderer.reset();
        outputSlots[0].player.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!outputSlots[0].player.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadOutputVideoSlot(0) failed: " << path << '\n';
            outputSlots[0].isReloading = false;
            return false;
        }

        outputSlots[0].player.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        outputSlots[0].player.setPlaybackRate(outputVisualControls.playback.videoPlaybackRate);

        // Rebuild texture
        previewFrameSystem.waitForAllFences();
        outputFrameSystem.waitForAllFences();
        outputSlots[0].texture.destroy(resourceSystem, vulkanContext.getDevice());
        outputSlots[0].texture.cleanup(resourceSystem);
        outputSlots[0].texture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(outputSlots[0].player.width()),
            static_cast<uint32_t>(outputSlots[0].player.height()));
        outputSlots[0].framePool.resize(
            static_cast<uint32_t>(outputSlots[0].player.width()),
            static_cast<uint32_t>(outputSlots[0].player.height()),
            static_cast<uint32_t>(outputSlots[0].player.width()) * 4);
        if (outputSlots[0].renderer) outputSlots[0].renderer->reset();

        updateAllDescriptorSets();

        outputSlots[0].renderer = std::make_unique<VideoRenderer>(outputSlots[0].player, outputSlots[0].texture, outputSlots[0].framePool);
        outputSlots[0].randomizer.elapsedSeconds = 0.0f;
        outputSlots[0].randomizer.currentVideoDuration = outputSlots[0].player.durationSeconds();
        outputSlots[0].initialized = outputSlots[0].texture.isReady();
    } else {
        auto& s = outputSlots[slot];
        s.sourcePath = path;
        s.renderer.reset();
        s.player.shutdown();

        glm::ivec2 screenSize = getScreenSize();
        if (!s.player.initialize(path, screenSize.x, screenSize.y)) {
            std::cerr << "[Application] reloadOutputVideoSlot(" << slot << ") failed: " << path << '\n';
            s.isReloading = false;
            return false;
        }

        float rate = (slot == 1) ? outputVisualControls.playback.video2PlaybackRate
                                 : outputVisualControls.playback.video3PlaybackRate;
        s.player.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        s.player.setPlaybackRate(rate);

        previewFrameSystem.waitForAllFences();
        outputFrameSystem.waitForAllFences();
        s.texture.destroy(resourceSystem, vulkanContext.getDevice());
        s.texture.cleanup(resourceSystem);
        s.texture.createResources(
            resourceSystem, vulkanContext.getDevice(),
            vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
            static_cast<uint32_t>(s.player.width()),
            static_cast<uint32_t>(s.player.height()));
        s.framePool.resize(
            static_cast<uint32_t>(s.player.width()),
            static_cast<uint32_t>(s.player.height()),
            static_cast<uint32_t>(s.player.width()) * 4);
        if (s.renderer) s.renderer->reset();

        updateAllDescriptorSets();

        s.renderer = std::make_unique<VideoRenderer>(s.player, s.texture, s.framePool);
        s.randomizer.elapsedSeconds = 0.0f;
        s.randomizer.currentVideoDuration = s.player.durationSeconds();
        s.initialized = s.texture.isReady();
    }

    if (slot == 0) outputSlots[0].isReloading = false;
    else if (slot == 1) outputSlots[1].isReloading = false;
    else outputSlots[2].isReloading = false;
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
        static_cast<const VideoRandomizerState&>(previewSlots[0].randomizer),
        static_cast<const VideoRandomizerState&>(previewSlots[1].randomizer),
        static_cast<const VideoRandomizerState&>(previewSlots[2].randomizer),
        allowDimensionChangeRecreation,
        oscSystem,
        previewSlots[0].selectedVideoAsset,
        previewSlots[1].selectedVideoAsset,
        previewSlots[2].selectedVideoAsset,
        previewSlots[0].sourcePath,
        previewSlots[1].sourcePath,
        previewSlots[2].sourcePath,
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

namespace {

// Preset names become file names: strip characters Windows does not allow.
std::string sanitizePresetName(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.empty() ? std::string("preset") : out;
}

// Write to a temp file first so a crash or full disk never leaves a truncated preset behind.
bool writeJsonAtomically(const std::string& path, const nlohmann::json& j) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) return false;
        f << j.dump(4);
        f.flush();
        if (!f.good()) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {                                    // Windows: rename onto an existing file can fail
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(tmp, ec2);
        return false;
    }
    return true;
}

} // namespace

bool Application::savePreset(const std::string& name) {
    return savePresetFromOutput(name);
}

bool Application::savePresetFromOutput(const std::string& name) {
    std::filesystem::create_directories(presetsDir);
    std::string path = presetsDir + "/" + sanitizePresetName(name) + ".json";

    // Save OUTPUT controls so the preset captures exactly what is on the final output window.
    ParameterRegistry outputRegistry;
    VisualControlsRegistry::build(outputRegistry, outputVisualControls);

    nlohmann::json j;
    j["version"] = 3;
    j["visualControls"] = JsonSerializer::toJson(outputRegistry);

    // Save OUTPUT video state so the preset restores the exact output sources
    nlohmann::json video;
    video["videoSourcePath"] = outputSlots[0].sourcePath;
    video["videoSourcePath2"] = outputSlots[1].sourcePath;
    video["videoSourcePath3"] = outputSlots[2].sourcePath;
    video["selectedVideoAsset"] = outputSlots[0].selectedVideoAsset;
    video["selectedVideoAsset2"] = outputSlots[1].selectedVideoAsset;
    video["selectedVideoAsset3"] = outputSlots[2].selectedVideoAsset;
    video["allowDimensionChangeRecreation"] = allowDimensionChangeRecreation;

    nlohmann::json r1;
    r1["autoRandomize"] = previewSlots[0].randomizer.autoRandomize;
    r1["useVideoDuration"] = previewSlots[0].randomizer.useVideoDuration;
    r1["intervalSeconds"] = previewSlots[0].randomizer.intervalSeconds;
    r1["useShuffleMode"] = previewSlots[0].randomizer.useShuffleMode;
    video["randomizer1"] = r1;

    nlohmann::json r2;
    r2["autoRandomize"] = previewSlots[1].randomizer.autoRandomize;
    r2["useVideoDuration"] = previewSlots[1].randomizer.useVideoDuration;
    r2["intervalSeconds"] = previewSlots[1].randomizer.intervalSeconds;
    r2["useShuffleMode"] = previewSlots[1].randomizer.useShuffleMode;
    video["randomizer2"] = r2;

    nlohmann::json r3;
    r3["autoRandomize"] = previewSlots[2].randomizer.autoRandomize;
    r3["useVideoDuration"] = previewSlots[2].randomizer.useVideoDuration;
    r3["intervalSeconds"] = previewSlots[2].randomizer.intervalSeconds;
    r3["useShuffleMode"] = previewSlots[2].randomizer.useShuffleMode;
    video["randomizer3"] = r3;

    j["video"] = video;

    return writeJsonAtomically(path, j);
}

bool Application::savePresetFromPreview(const std::string& name) {
    std::filesystem::create_directories(presetsDir);
    std::string path = presetsDir + "/" + sanitizePresetName(name) + ".json";

    // Save PREVIEW controls so the preset captures what is on the preview window.
    ParameterRegistry previewRegistry;
    VisualControlsRegistry::build(previewRegistry, visualControls);

    nlohmann::json j;
    j["version"] = 3;
    j["visualControls"] = JsonSerializer::toJson(previewRegistry);

    // Save PREVIEW video state
    nlohmann::json video;
    video["videoSourcePath"] = previewSlots[0].sourcePath;
    video["videoSourcePath2"] = previewSlots[1].sourcePath;
    video["videoSourcePath3"] = previewSlots[2].sourcePath;
    video["selectedVideoAsset"] = previewSlots[0].selectedVideoAsset;
    video["selectedVideoAsset2"] = previewSlots[1].selectedVideoAsset;
    video["selectedVideoAsset3"] = previewSlots[2].selectedVideoAsset;
    video["allowDimensionChangeRecreation"] = allowDimensionChangeRecreation;

    nlohmann::json r1;
    r1["autoRandomize"] = previewSlots[0].randomizer.autoRandomize;
    r1["useVideoDuration"] = previewSlots[0].randomizer.useVideoDuration;
    r1["intervalSeconds"] = previewSlots[0].randomizer.intervalSeconds;
    r1["useShuffleMode"] = previewSlots[0].randomizer.useShuffleMode;
    video["randomizer1"] = r1;

    nlohmann::json r2;
    r2["autoRandomize"] = previewSlots[1].randomizer.autoRandomize;
    r2["useVideoDuration"] = previewSlots[1].randomizer.useVideoDuration;
    r2["intervalSeconds"] = previewSlots[1].randomizer.intervalSeconds;
    r2["useShuffleMode"] = previewSlots[1].randomizer.useShuffleMode;
    video["randomizer2"] = r2;

    nlohmann::json r3;
    r3["autoRandomize"] = previewSlots[2].randomizer.autoRandomize;
    r3["useVideoDuration"] = previewSlots[2].randomizer.useVideoDuration;
    r3["intervalSeconds"] = previewSlots[2].randomizer.intervalSeconds;
    r3["useShuffleMode"] = previewSlots[2].randomizer.useShuffleMode;
    video["randomizer3"] = r3;

    j["video"] = video;

    return writeJsonAtomically(path, j);
}

bool Application::loadPreset(const std::string& name) {
    std::string path = presetsDir + "/" + sanitizePresetName(name) + ".json";
    if (!std::filesystem::exists(path)) return false;

    // A half-applied preset (controls changed, videos not) is worse than a refused one.
    if (!canChangeVideo()) {
        std::cerr << "[Preset] A video is switching right now, try again in a moment: " << name << std::endl;
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) return false;

    // Validate BEFORE touching any state
    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        std::cerr << "[Preset] Invalid JSON: " << path << std::endl;
        return false;
    }
    if (!j.is_object()) {
        std::cerr << "[Preset] Not a preset file: " << path << std::endl;
        return false;
    }

    loadingPreset = true;   // keeps per-clip rotations from overriding the preset's own values

    // Start from defaults so values the file does not define cannot leak in from the previous state
    visualControls = VisualControls{};

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
        // Reset paths to empty before loading — ensures missing keys = no video
        previewSlots[0].sourcePath.clear();
        previewSlots[1].sourcePath.clear();
        previewSlots[2].sourcePath.clear();
        if (v.contains("videoSourcePath")) previewSlots[0].sourcePath = v["videoSourcePath"];
        if (v.contains("videoSourcePath2")) previewSlots[1].sourcePath = v["videoSourcePath2"];
        if (v.contains("videoSourcePath3")) previewSlots[2].sourcePath = v["videoSourcePath3"];
        if (v.contains("selectedVideoAsset")) previewSlots[0].selectedVideoAsset = v["selectedVideoAsset"];
        if (v.contains("selectedVideoAsset2")) previewSlots[1].selectedVideoAsset = v["selectedVideoAsset2"];
        if (v.contains("selectedVideoAsset3")) previewSlots[2].selectedVideoAsset = v["selectedVideoAsset3"];
        if (v.contains("allowDimensionChangeRecreation")) allowDimensionChangeRecreation = v["allowDimensionChangeRecreation"];

        if (v.contains("randomizer1")) {
            auto& r1 = v["randomizer1"];
            if (r1.contains("autoRandomize")) previewSlots[0].randomizer.autoRandomize = r1["autoRandomize"];
            if (r1.contains("useVideoDuration")) previewSlots[0].randomizer.useVideoDuration = r1["useVideoDuration"];
            if (r1.contains("intervalSeconds")) previewSlots[0].randomizer.intervalSeconds = r1["intervalSeconds"];
            if (r1.contains("useShuffleMode")) previewSlots[0].randomizer.useShuffleMode = r1["useShuffleMode"];
        }
        if (v.contains("randomizer2")) {
            auto& r2 = v["randomizer2"];
            if (r2.contains("autoRandomize")) previewSlots[1].randomizer.autoRandomize = r2["autoRandomize"];
            if (r2.contains("useVideoDuration")) previewSlots[1].randomizer.useVideoDuration = r2["useVideoDuration"];
            if (r2.contains("intervalSeconds")) previewSlots[1].randomizer.intervalSeconds = r2["intervalSeconds"];
            if (r2.contains("useShuffleMode")) previewSlots[1].randomizer.useShuffleMode = r2["useShuffleMode"];
        }
        if (v.contains("randomizer3")) {
            auto& r3 = v["randomizer3"];
            if (r3.contains("autoRandomize")) previewSlots[2].randomizer.autoRandomize = r3["autoRandomize"];
            if (r3.contains("useVideoDuration")) previewSlots[2].randomizer.useVideoDuration = r3["useVideoDuration"];
            if (r3.contains("intervalSeconds")) previewSlots[2].randomizer.intervalSeconds = r3["intervalSeconds"];
            if (r3.contains("useShuffleMode")) previewSlots[2].randomizer.useShuffleMode = r3["useShuffleMode"];
        }
    }

    // Reload videos to match preset state
    if (canChangeVideo()) {
        if (!previewSlots[0].sourcePath.empty()) reloadVideoSlot(0, previewSlots[0].sourcePath);
        if (!previewSlots[1].sourcePath.empty()) reloadVideoSlot(1, previewSlots[1].sourcePath);
        if (!previewSlots[2].sourcePath.empty()) reloadVideoSlot(2, previewSlots[2].sourcePath);
    }

    // Reset animation state so time-based effects start fresh
    previewAnim = AnimState{};
    prevAudioSmooth = AudioSmoothState{};

    // Sync output to match the loaded preset so preview == output
    outputVisualControls = visualControls;
    outputSlots[0].sourcePath = previewSlots[0].sourcePath;
    outputSlots[1].sourcePath = previewSlots[1].sourcePath;
    outputSlots[2].sourcePath = previewSlots[2].sourcePath;
    outputSlots[0].selectedVideoAsset = previewSlots[0].selectedVideoAsset;
    outputSlots[1].selectedVideoAsset = previewSlots[1].selectedVideoAsset;
    outputSlots[2].selectedVideoAsset = previewSlots[2].selectedVideoAsset;
    // Reset animation and audio smoothing so both start from the same base
    outputAnim = previewAnim;
    outputPrevAudioSmooth = prevAudioSmooth;
    if (canChangeVideo()) {
        if (!outputSlots[0].sourcePath.empty()) reloadOutputVideoSlot(0, outputSlots[0].sourcePath);
        if (!outputSlots[1].sourcePath.empty()) reloadOutputVideoSlot(1, outputSlots[1].sourcePath);
        if (!outputSlots[2].sourcePath.empty()) reloadOutputVideoSlot(2, outputSlots[2].sourcePath);
        updateAllDescriptorSets();
    }

    loadingPreset = false;
    controlsDirty = true;
    std::cout << "[Preset] Loaded: " << name << std::endl;
    return true;
}

bool Application::deletePreset(const std::string& name) {
    std::string path = presetsDir + "/" + sanitizePresetName(name) + ".json";
    if (!std::filesystem::exists(path)) return false;
    return std::filesystem::remove(path);
}

bool Application::renamePreset(const std::string& oldName, const std::string& newName) {
    std::string oldPath = presetsDir + "/" + sanitizePresetName(oldName) + ".json";
    std::string newPath = presetsDir + "/" + sanitizePresetName(newName) + ".json";
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
    // The window hosts the whole UI now: below this size the panels stop being usable
    SDL_SetWindowMinimumSize(window.getMainWindow(), (int)(1100 * s), (int)(660 * s));
    window.createOutputWindow("[OUTPUT] Final", (int)(1280 * s), (int)(720 * s));
    SDL_SetWindowPosition(window.getOutputWindow(), (int)(50 * s), (int)(820 * s));
    // The UI lives inside the preview window now (single-window layout), no separate controls window.

    // Per-phase startup timings (so a slow start can be pinned to one step)
    auto stepStart = std::chrono::steady_clock::now();
    auto logStep = [&](const char* name) {
        const auto now = std::chrono::steady_clock::now();
        std::cout << "[Startup] " << name << ": "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(now - stepStart).count() << " ms" << std::endl;
        stepStart = now;
    };

    initVulkan();               logStep("initVulkan");
    initPresenters();           logStep("initPresenters (swapchains)");
    initRenderPass();           logStep("initRenderPass");

    previewPresenter.createFramebuffers(renderPass);
    outputPresenter.createFramebuffers(renderPass);
    logStep("framebuffers");

    descriptorSetManager.createLayout(vulkanContext.getDevice());
    descriptorSetManager.createPool(vulkanContext.getDevice());
    outputDescriptorSetManager.createLayout(vulkanContext.getDevice());
    outputDescriptorSetManager.createPool(vulkanContext.getDevice());
    uniformBufferManager.createBuffers(resourceSystem, vulkanContext.getDevice());
    outputUniformBufferManager.createBuffers(resourceSystem, vulkanContext.getDevice());
    logStep("descriptors + uniform buffers");

    // Cargar estado antes del vídeo para que selectedVideoFolder sea correcto
    JsonSerializer::load(visualControlsPath, parameterRegistry);
    logStep("load visual_controls.json");
    ControlState::load(
        controlStatePath,
        previewSlots[0].randomizer,
        previewSlots[1].randomizer,
        previewSlots[2].randomizer,
        allowDimensionChangeRecreation,
        oscSystem,
        previewSlots[0].selectedVideoAsset,
        previewSlots[1].selectedVideoAsset,
        previewSlots[2].selectedVideoAsset,
        previewSlots[0].sourcePath,
        previewSlots[1].sourcePath,
        previewSlots[2].sourcePath,
        videoAssetsRoot
    );

    // Sync loaded preview state to output so the final renderer starts
    // with the same configuration as the saved session.
    outputVisualControls = visualControls;
    outputSlots[0].sourcePath = previewSlots[0].sourcePath;
    outputSlots[1].sourcePath = previewSlots[1].sourcePath;
    outputSlots[2].sourcePath = previewSlots[2].sourcePath;
    outputSlots[0].selectedVideoAsset = previewSlots[0].selectedVideoAsset;
    outputSlots[1].selectedVideoAsset = previewSlots[1].selectedVideoAsset;
    outputSlots[2].selectedVideoAsset = previewSlots[2].selectedVideoAsset;

    logStep("load controls_state");
    initVideo();                logStep("initVideo (library scan + decoders)");
    initOutputVideo();          logStep("initOutputVideo");

    descriptorSetManager.createSets(vulkanContext.getDevice());
    outputDescriptorSetManager.createSets(vulkanContext.getDevice());
    const bool initVid1Ready = previewSlots[0].texture.isReady();
    const bool initOutVid1Ready = outputSlots[0].texture.isReady();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            uniformBufferManager.getBuffers()[i],
            initVid1Ready ? const_cast<VkDescriptorImageInfo*>(&previewSlots[0].texture.getDescriptorInfo()) : nullptr,
            initVid1Ready ? const_cast<VkDescriptorImageInfo*>(&previewSlots[0].texture.getPrevDescriptorInfo()) : nullptr
        );
        outputDescriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            outputUniformBufferManager.getBuffers()[i],
            initOutVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputSlots[0].texture.getDescriptorInfo()) : nullptr,
            initOutVid1Ready ? const_cast<VkDescriptorImageInfo*>(&outputSlots[0].texture.getPrevDescriptorInfo()) : nullptr
        );
    }

    initPipelines();            logStep("initPipelines (graphics + compute passes)");
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
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    previewPresenter.init(vulkanContext.getInstance(), vulkanContext.getPhysicalDevice(),
                          vulkanContext.getDevice(), window.getMainWindow(), 854, 480);
    const auto t1 = Clock::now();
    outputPresenter.init(vulkanContext.getInstance(), vulkanContext.getPhysicalDevice(),
                         vulkanContext.getDevice(), window.getOutputWindow(), 1920, 1080);
    const auto t2 = Clock::now();
    std::cout << "[Startup]   preview swapchain: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms, output swapchain: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << " ms" << std::endl;
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

void Application::initPipelines() {
    createFullscreenDescriptorSets();
    createFullscreenPipeline();
    createSwapchainSampler();
}

void Application::createFullscreenDescriptorSets() {
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
}

void Application::createFullscreenPipeline() {
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
}

void Application::createSwapchainSampler() {
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
    (void)screenSize;
    glm::ivec2 previewSize(static_cast<int>(previewRenderExtent.width), static_cast<int>(previewRenderExtent.height));

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

    std::cout << "[initVideo] Before resolve: V1 path=" << previewSlots[0].sourcePath
              << " idx=" << previewSlots[0].selectedVideoAsset << " folder="
              << visualControls.playback.selectedVideoFolder << "\n";
    std::cout << "[initVideo] Before resolve: V2 path=" << previewSlots[1].sourcePath
              << " idx=" << previewSlots[1].selectedVideoAsset << " folder="
              << visualControls.playback.selectedVideo2Folder << "\n";
    std::cout << "[initVideo] Before resolve: V3 path=" << previewSlots[2].sourcePath
              << " idx=" << previewSlots[2].selectedVideoAsset << " folder="
              << visualControls.playback.selectedVideo3Folder << "\n";

    resolvePath(visualControls.playback.selectedVideoFolder,
                previewSlots[0].selectedVideoAsset, previewSlots[0].sourcePath, "V1");
    resolvePath(visualControls.playback.selectedVideo2Folder,
                previewSlots[1].selectedVideoAsset, previewSlots[1].sourcePath, "V2");
    resolvePath(visualControls.playback.selectedVideo3Folder,
                previewSlots[2].selectedVideoAsset, previewSlots[2].sourcePath, "V3");

    std::cout << "[initVideo] After resolve:  V1 path=" << previewSlots[0].sourcePath
              << " idx=" << previewSlots[0].selectedVideoAsset << "\n";
    std::cout << "[initVideo] After resolve:  V2 path=" << previewSlots[1].sourcePath
              << " idx=" << previewSlots[1].selectedVideoAsset << "\n";
    std::cout << "[initVideo] After resolve:  V3 path=" << previewSlots[2].sourcePath
              << " idx=" << previewSlots[2].selectedVideoAsset << "\n";
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
        std::cout << "[initVideo] V1 initializing: '" << previewSlots[0].sourcePath << "'\n";
        return previewSlots[0].player.initialize(previewSlots[0].sourcePath, previewSize.x, previewSize.y);
    });

    std::future<bool> future1;
    std::future<bool> future2;
    if (initExtraSlots) {
        future1 = std::async(std::launch::async, [&]() {
            std::cout << "[initVideo] V2 initializing: '" << previewSlots[1].sourcePath << "'\n";
            return previewSlots[1].player.initialize(previewSlots[1].sourcePath, previewSize.x, previewSize.y);
        });
        future2 = std::async(std::launch::async, [&]() {
            std::cout << "[initVideo] V3 initializing: '" << previewSlots[2].sourcePath << "'\n";
            return previewSlots[2].player.initialize(previewSlots[2].sourcePath, previewSize.x, previewSize.y);
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
        std::cout << "[initVideo] Slot0 loaded: " << previewSlots[0].sourcePath
                  << " | decode=" << previewSlots[0].player.width() << "x" << previewSlots[0].player.height()
                  << " | duration=" << previewSlots[0].player.durationSeconds() << "s\n";
    } else {
        std::cerr << "[initVideo] Slot0 FAILED: " << previewSlots[0].sourcePath << "\n";
    }
    if (ok1) {
        std::cout << "[initVideo] Slot1 loaded: " << previewSlots[1].sourcePath
                  << " | decode=" << previewSlots[1].player.width() << "x" << previewSlots[1].player.height()
                  << " | duration=" << previewSlots[1].player.durationSeconds() << "s\n";
    } else if (initExtraSlots) {
        std::cerr << "[initVideo] Slot1 FAILED: " << previewSlots[1].sourcePath << "\n";
    }
    if (ok2) {
        std::cout << "[initVideo] Slot2 loaded: " << previewSlots[2].sourcePath
                  << " | decode=" << previewSlots[2].player.width() << "x" << previewSlots[2].player.height()
                  << " | duration=" << previewSlots[2].player.durationSeconds() << "s\n";
    } else if (initExtraSlots) {
        std::cerr << "[initVideo] Slot2 FAILED: " << previewSlots[2].sourcePath << "\n";
    }

    if (!ok0) {
        std::cerr << "[Application] Failed to initialize video player 1, aborting initVideo()\n";
        return;
    }
    std::cout << "[initVideo] V1 ok, configuring autoScale=" << visualControls.playback.autoScaleVideo << "\n";
    previewSlots[0].player.setAutoScale(visualControls.playback.autoScaleVideo);

    // ── Vulkan resources (must be on main thread, but faster) ───────────────
    auto tVk = Clock::now();

    previewSlots[0].framePool.resize(previewSlots[0].player.width(), previewSlots[0].player.height(), previewSlots[0].player.width() * 4);
    previewSlots[0].texture.createResources(resourceSystem, vulkanContext.getDevice(),
                                 vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                 previewSlots[0].player.width(), previewSlots[0].player.height());
    previewSlots[0].initialized = previewSlots[0].texture.isReady();
    if (previewSlots[0].initialized)
        previewSlots[0].renderer = std::make_unique<VideoRenderer>(previewSlots[0].player, previewSlots[0].texture, previewSlots[0].framePool);

    if (initExtraSlots && ok1) {
        std::cout << "[initVideo] Setting up V2 Vulkan resources (" << previewSlots[1].player.width() << "x" << previewSlots[1].player.height() << ")\n";
        previewSlots[1].player.setAutoScale(visualControls.playback.autoScaleVideo);
        previewSlots[1].framePool.resize(previewSlots[1].player.width(), previewSlots[1].player.height(), previewSlots[1].player.width() * 4);
        previewSlots[1].texture.createResources(resourceSystem, vulkanContext.getDevice(),
                                      vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                      previewSlots[1].player.width(), previewSlots[1].player.height());
        previewSlots[1].initialized = previewSlots[1].texture.isReady();
        std::cout << "[initVideo] V2 texture ready=" << previewSlots[1].initialized << "\n";
        if (previewSlots[1].initialized)
            previewSlots[1].renderer = std::make_unique<VideoRenderer>(previewSlots[1].player, previewSlots[1].texture, previewSlots[1].framePool);
    } else {
        std::cout << "[initVideo] V2 skipped: initExtraSlots=" << initExtraSlots << " ok1=" << ok1 << "\n";
    }

    if (initExtraSlots && ok2) {
        std::cout << "[initVideo] Setting up V3 Vulkan resources (" << previewSlots[2].player.width() << "x" << previewSlots[2].player.height() << ")\n";
        previewSlots[2].player.setAutoScale(visualControls.playback.autoScaleVideo);
        previewSlots[2].framePool.resize(previewSlots[2].player.width(), previewSlots[2].player.height(), previewSlots[2].player.width() * 4);
        previewSlots[2].texture.createResources(resourceSystem, vulkanContext.getDevice(),
                                      vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                      previewSlots[2].player.width(), previewSlots[2].player.height());
        previewSlots[2].initialized = previewSlots[2].texture.isReady();
        std::cout << "[initVideo] V3 texture ready=" << previewSlots[2].initialized << "\n";
        if (previewSlots[2].initialized)
            previewSlots[2].renderer = std::make_unique<VideoRenderer>(previewSlots[2].player, previewSlots[2].texture, previewSlots[2].framePool);
    } else {
        std::cout << "[initVideo] V3 skipped: initExtraSlots=" << initExtraSlots << " ok2=" << ok2 << "\n";
    }

    auto tVkDone = Clock::now();
    std::cout << "[Application] Vulkan resources: "
              << std::chrono::duration<double>(tVkDone - tVk).count() << "s\n";
    std::cout << "[initVideo] Textures ready: v1=" << previewSlots[0].initialized
              << " v2=" << previewSlots[1].initialized
              << " v3=" << previewSlots[2].initialized << "\n";

    auto t1 = Clock::now();
    std::cout << "[Application] initVideo() DONE — total="
              << std::chrono::duration<double>(t1 - t0).count()
              << "s v1=" << previewSlots[0].initialized
              << " v2=" << previewSlots[1].initialized
              << " v3=" << previewSlots[2].initialized << '\n';
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
    outputSlots[0].sourcePath = previewSlots[0].sourcePath;
    outputSlots[1].sourcePath = previewSlots[1].sourcePath;
    outputSlots[2].sourcePath = previewSlots[2].sourcePath;
    outputSlots[0].selectedVideoAsset = previewSlots[0].selectedVideoAsset;
    outputSlots[1].selectedVideoAsset = previewSlots[1].selectedVideoAsset;
    outputSlots[2].selectedVideoAsset = previewSlots[2].selectedVideoAsset;

    bool initExtraSlots = visualControls.playback.enableDualVideo;

    auto future0 = std::async(std::launch::async, [&]() {
        return outputSlots[0].player.initialize(outputSlots[0].sourcePath, screenSize.x, screenSize.y);
    });

    std::future<bool> future1;
    std::future<bool> future2;
    if (initExtraSlots) {
        future1 = std::async(std::launch::async, [&]() {
            return outputSlots[1].player.initialize(outputSlots[1].sourcePath, screenSize.x, screenSize.y);
        });
        future2 = std::async(std::launch::async, [&]() {
            return outputSlots[2].player.initialize(outputSlots[2].sourcePath, screenSize.x, screenSize.y);
        });
    }

    bool ok0 = future0.get();
    bool ok1 = initExtraSlots ? future1.get() : false;
    bool ok2 = initExtraSlots ? future2.get() : false;

    if (!ok0) {
        std::cerr << "[Application] Failed to initialize output video player 1\n";
        return;
    }
    outputSlots[0].player.setAutoScale(visualControls.playback.autoScaleVideo);

    outputSlots[0].framePool.resize(outputSlots[0].player.width(), outputSlots[0].player.height(), outputSlots[0].player.width() * 4);
    outputSlots[0].texture.createResources(resourceSystem, vulkanContext.getDevice(),
                                       vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                       outputSlots[0].player.width(), outputSlots[0].player.height());
    outputSlots[0].initialized = outputSlots[0].texture.isReady();
    if (outputSlots[0].initialized)
        outputSlots[0].renderer = std::make_unique<VideoRenderer>(outputSlots[0].player, outputSlots[0].texture, outputSlots[0].framePool);

    if (initExtraSlots && ok1) {
        outputSlots[1].player.setAutoScale(visualControls.playback.autoScaleVideo);
        outputSlots[1].framePool.resize(outputSlots[1].player.width(), outputSlots[1].player.height(), outputSlots[1].player.width() * 4);
        outputSlots[1].texture.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                             outputSlots[1].player.width(), outputSlots[1].player.height());
        outputSlots[1].initialized = outputSlots[1].texture.isReady();
        if (outputSlots[1].initialized)
            outputSlots[1].renderer = std::make_unique<VideoRenderer>(outputSlots[1].player, outputSlots[1].texture, outputSlots[1].framePool);
    }

    if (initExtraSlots && ok2) {
        outputSlots[2].player.setAutoScale(outputVisualControls.playback.autoScaleVideo);
        outputSlots[2].framePool.resize(outputSlots[2].player.width(), outputSlots[2].player.height(), outputSlots[2].player.width() * 4);
        outputSlots[2].texture.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                             outputSlots[2].player.width(), outputSlots[2].player.height());
        outputSlots[2].initialized = outputSlots[2].texture.isReady();
        if (outputSlots[2].initialized)
            outputSlots[2].renderer = std::make_unique<VideoRenderer>(outputSlots[2].player, outputSlots[2].texture, outputSlots[2].framePool);
    }

    auto t1 = Clock::now();
    std::cout << "[Application] initOutputVideo() done — total="
              << std::chrono::duration<double>(t1 - t0).count()
              << "s v1=" << outputSlots[0].initialized
              << " v2=" << outputSlots[1].initialized
              << " v3=" << outputSlots[2].initialized << '\n';
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem init
// ─────────────────────────────────────────────────────────────────────────────

void Application::initUI() {
    const auto& qf = vulkanContext.getQueueFamilyIndices();
    if (!uiSystem.initializeVulkan(window.getMainWindow(), vulkanContext.getInstance(),
                                   vulkanContext.getPhysicalDevice(), vulkanContext.getDevice(),
                                   qf.graphicsFamily, vulkanContext.getGraphicsQueue(), renderPass,
                                   static_cast<uint32_t>(previewPresenter.getImageCount()), window.getDpiScale()))
        throw std::runtime_error("failed to initialize UI system");
    uiSystem.setPreviewAspect(static_cast<float>(previewRenderExtent.width) / static_cast<float>(previewRenderExtent.height));
    cachedCallbacks = buildUICallbacks();
}

void Application::initNLE() {
    g_project_state.active_file = previewSlots[0].sourcePath;
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
        handleOscTrigger(action, -1);
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
    oscSystem.setTriggerCallback([this](const std::string& action, int arg) {
        handleOscTrigger(action, arg);
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
    if (previewSlots[0].initialized) {
        const bool has2 = previewSlots[1].texture.isReady();
        const bool has3 = previewSlots[2].texture.isReady();
        if (!multiPassPipeline.initialize(
                vulkanContext.getPhysicalDevice(), vulkanContext.getDevice(),
                vulkanContext.getGraphicsQueue(), queueFamily,
                previewRenderExtent, previewPresenter.getFormat(),
                previewSlots[0].texture.getSampler(), previewSlots[0].texture.getSampler(),
                previewSlots[0].texture.getImageView(), previewSlots[0].texture.getImageView(),
                has2 ? previewSlots[1].texture.getSampler()   : previewSlots[0].texture.getSampler(),
                has2 ? previewSlots[1].texture.getImageView() : previewSlots[0].texture.getImageView(),
                has3 ? previewSlots[2].texture.getSampler()   : previewSlots[0].texture.getSampler(),
                has3 ? previewSlots[2].texture.getImageView() : previewSlots[0].texture.getImageView(),
                uniformBufferManager.getBuffers(), UniformBufferManager::getBufferSize()))
        {
            std::cerr << "[Application] Failed to initialize MultiPassPipeline\n";
        } else {
            multiPassPipeline.updateDescriptorSets(
                uniformBufferManager.getBuffers(),
                previewSlots[0].texture.getImageView(), previewSlots[0].texture.getPrevImageView(),
                previewSlots[0].texture.getSampler(),   previewSlots[0].texture.getSampler(),
                has2 ? previewSlots[1].texture.getImageView() : previewSlots[0].texture.getImageView(),
                has2 ? previewSlots[1].texture.getSampler()   : previewSlots[0].texture.getSampler(),
                has3 ? previewSlots[2].texture.getImageView() : previewSlots[0].texture.getImageView(),
                has3 ? previewSlots[2].texture.getSampler()   : previewSlots[0].texture.getSampler()
            );
            std::cout << "[Application] Preview MultiPassPipeline initialized\n";
        }
    } else {
        std::cout << "[Application] Skipping preview MultiPassPipeline — video not initialized\n";
    }

    // ── Output pipeline (independent) ───────────────────────────────────────
    if (outputSlots[0].initialized) {
        const bool outputHas2 = outputSlots[1].texture.isReady();
        const bool outputHas3 = outputSlots[2].texture.isReady();
        if (!outputMultiPassPipeline.initialize(
                vulkanContext.getPhysicalDevice(), vulkanContext.getDevice(),
                vulkanContext.getGraphicsQueue(), queueFamily,
                outputPresenter.getExtent(), outputPresenter.getFormat(),
                outputSlots[0].texture.getSampler(), outputSlots[0].texture.getSampler(),
                outputSlots[0].texture.getImageView(), outputSlots[0].texture.getImageView(),
                outputHas2 ? outputSlots[1].texture.getSampler()   : outputSlots[0].texture.getSampler(),
                outputHas2 ? outputSlots[1].texture.getImageView() : outputSlots[0].texture.getImageView(),
                outputHas3 ? outputSlots[2].texture.getSampler()   : outputSlots[0].texture.getSampler(),
                outputHas3 ? outputSlots[2].texture.getImageView() : outputSlots[0].texture.getImageView(),
                outputUniformBufferManager.getBuffers(), UniformBufferManager::getBufferSize()))
        {
            std::cerr << "[Application] Failed to initialize output MultiPassPipeline\n";
        } else {
            outputMultiPassPipeline.updateDescriptorSets(
                outputUniformBufferManager.getBuffers(),
                outputSlots[0].texture.getImageView(), outputSlots[0].texture.getPrevImageView(),
                outputSlots[0].texture.getSampler(),   outputSlots[0].texture.getSampler(),
                outputHas2 ? outputSlots[1].texture.getImageView() : outputSlots[0].texture.getImageView(),
                outputHas2 ? outputSlots[1].texture.getSampler()   : outputSlots[0].texture.getSampler(),
                outputHas3 ? outputSlots[2].texture.getImageView() : outputSlots[0].texture.getImageView(),
                outputHas3 ? outputSlots[2].texture.getSampler()   : outputSlots[0].texture.getSampler()
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
    return pickNextIndex(assets, previewSlots[0].randomizer, previewSlots[0].selectedVideoAsset);
}

int Application::pickNextVideoIndex2(const std::vector<VideoAsset>& assets) {
    return pickNextIndex(assets, previewSlots[1].randomizer, previewSlots[1].selectedVideoAsset);
}

int Application::pickNextVideoIndex3(const std::vector<VideoAsset>& assets) {
    return pickNextIndex(assets, previewSlots[2].randomizer, previewSlots[2].selectedVideoAsset);
}

bool Application::reloadVideoAtIndex(int newIndex, const std::vector<VideoAsset>& assets) {
    previewSlots[0].selectedVideoAsset = newIndex;
    transitionActive   = false;
    return reloadVideoSlot(0, assets[newIndex].metadata.path);
}

bool Application::reloadVideoAtIndex2(int newIndex, const std::vector<VideoAsset>& assets) {
    previewSlots[1].selectedVideoAsset = newIndex;
    transitionActive    = false;
    return reloadVideoSlot(1, assets[newIndex].metadata.path);
}

bool Application::reloadVideoAtIndex3(int newIndex, const std::vector<VideoAsset>& assets) {
    previewSlots[2].selectedVideoAsset = newIndex;
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
        previewSlots[0].renderer.reset();
        previewSlots[0].player.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(0, previewSlots[0].sourcePath);
        std::cout << "[Render] Auto-reloaded video 1: " << previewSlots[0].sourcePath << '\n';
    } else if (slot == 1) {
        previewSlots[1].renderer.reset();
        previewSlots[1].player.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(1, previewSlots[1].sourcePath);
        std::cout << "[Render] Auto-reloaded video 2: " << previewSlots[1].sourcePath << '\n';
    } else {
        previewSlots[2].renderer.reset();
        previewSlots[2].player.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(2, previewSlots[2].sourcePath);
        std::cout << "[Render] Auto-reloaded video 3: " << previewSlots[2].sourcePath << '\n';
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Video control helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Application::canChangeVideo() const {
    return !previewSlots[0].isReloading && !previewSlots[1].isReloading && !previewSlots[2].isReloading && !transitionActive;
}

// ─────────────────────────────────────────────────────────────────────────────
// OSC / Trigger dispatcher
// ─────────────────────────────────────────────────────────────────────────────

void Application::handleOscTrigger(const std::string& action, int arg) {
    if (action == "randomizeVideo") {
        std::cout << "[OSC] randomizeVideo triggered\n";
        if (!canChangeVideo()) {
            std::cout << "[OSC] randomizeVideo blocked: canChangeVideo() == false\n";
            return;
        }
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        std::cout << "[OSC] randomizeVideo: folder=" << visualControls.playback.selectedVideoFolder << " assets=" << assets.size() << "\n";
        if (assets.size() > 1) {
            int idx = pickNextVideoIndex(assets);
            std::cout << "[OSC] randomizeVideo: picked index " << idx << " -> " << assets[idx].metadata.filename << "\n";
            reloadVideoAtIndex(idx, assets);
        } else {
            std::cout << "[OSC] randomizeVideo: not enough assets (need >1)\n";
        }
        previewSlots[0].isReloading = false;

    } else if (action == "randomizeVideo2") {
        std::cout << "[OSC] randomizeVideo2 triggered\n";
        if (!canChangeVideo()) {
            std::cout << "[OSC] randomizeVideo2 blocked: canChangeVideo() == false\n";
            return;
        }
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        std::cout << "[OSC] randomizeVideo2: folder=" << visualControls.playback.selectedVideo2Folder << " assets=" << assets.size() << "\n";
        if (assets.size() > 1) {
            int idx = pickNextVideoIndex2(assets);
            std::cout << "[OSC] randomizeVideo2: picked index " << idx << " -> " << assets[idx].metadata.filename << "\n";
            reloadVideoAtIndex2(idx, assets);
        } else {
            std::cout << "[OSC] randomizeVideo2: not enough assets (need >1)\n";
        }
        previewSlots[0].isReloading = false;

    } else if (action == "randomizeVideo3") {
        std::cout << "[OSC] randomizeVideo3 triggered\n";
        if (!canChangeVideo()) {
            std::cout << "[OSC] randomizeVideo3 blocked: canChangeVideo() == false\n";
            return;
        }
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        std::cout << "[OSC] randomizeVideo3: folder=" << visualControls.playback.selectedVideo3Folder << " assets=" << assets.size() << "\n";
        if (assets.size() > 1) {
            int idx = pickNextVideoIndex3(assets);
            std::cout << "[OSC] randomizeVideo3: picked index " << idx << " -> " << assets[idx].metadata.filename << "\n";
            reloadVideoAtIndex3(idx, assets);
        } else {
            std::cout << "[OSC] randomizeVideo3: not enough assets (need >1)\n";
        }
        previewSlots[0].isReloading = false;

    } else if (action == "randomizeAllVideos") {
        if (!canChangeVideo()) return;
        const auto& assets1 = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        const auto& assets2 = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        const auto& assets3 = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        if (assets1.size() > 1) reloadVideoAtIndex(pickNextVideoIndex(assets1), assets1);
        if (assets2.size() > 1) reloadVideoAtIndex2(pickNextVideoIndex2(assets2), assets2);
        if (assets3.size() > 1) reloadVideoAtIndex3(pickNextVideoIndex3(assets3), assets3);
        previewSlots[0].isReloading = false;

    } else if (action == "randomizePreviewVideo1") {
        uiSystem.forcePreviewShuffle(0);

    } else if (action == "randomizePreviewVideo2") {
        uiSystem.forcePreviewShuffle(1);

    } else if (action == "randomizePreviewVideo3") {
        uiSystem.forcePreviewShuffle(2);

    } else if (action == "jumpRandom") {
        if (!previewSlots[0].initialized) return;
        const double dur = previewSlots[0].player.durationSeconds();
        if (dur > 0) previewSlots[0].player.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));

    } else if (action == "jumpRandom2") {
        if (!previewSlots[1].initialized) return;
        const double dur = previewSlots[1].player.durationSeconds();
        if (dur > 0) previewSlots[1].player.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));

    } else if (action == "jumpRandom3") {
        if (!previewSlots[2].initialized) return;
        const double dur = previewSlots[2].player.durationSeconds();
        if (dur > 0) previewSlots[2].player.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));

    } else if (action == "folderChanged") {
        if (!canChangeVideo()) return;
        previewSlots[0].randomizer.shuffleQueue.clear();  previewSlots[0].randomizer.currentShuffleIndex  = 0;
        previewSlots[1].randomizer.shuffleQueue.clear(); previewSlots[1].randomizer.currentShuffleIndex = 0;
        previewSlots[2].randomizer.shuffleQueue.clear(); previewSlots[2].randomizer.currentShuffleIndex = 0;
        // Reload all 3 preview slots from their respective folders
        const auto& assets1 = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        const auto& assets2 = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        const auto& assets3 = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        if (!assets1.empty()) {
            previewSlots[0].selectedVideoAsset = 0;
            reloadVideoSlot(0, assets1[0].metadata.path);
        }
        if (!assets2.empty()) {
            previewSlots[1].selectedVideoAsset = 0;
            reloadVideoSlot(1, assets2[0].metadata.path);
        }
        if (!assets3.empty()) {
            previewSlots[2].selectedVideoAsset = 0;
            reloadVideoSlot(2, assets3[0].metadata.path);
        }
        previewSlots[0].isReloading = false;

    } else if (action == "applyChanges") {
        controlsDirty = true;

    } else if (action == "selectVideo1") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        if (arg >= 0 && arg < static_cast<int>(assets.size())) {
            reloadVideoAtIndex(arg, assets);
            previewSlots[0].isReloading = false;
        }

    } else if (action == "selectVideo2") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        if (arg >= 0 && arg < static_cast<int>(assets.size())) {
            reloadVideoAtIndex2(arg, assets);
            previewSlots[0].isReloading = false;
        }

    } else if (action == "selectVideo3") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        if (arg >= 0 && arg < static_cast<int>(assets.size())) {
            reloadVideoAtIndex3(arg, assets);
            previewSlots[0].isReloading = false;
        }

    } else if (action == "sendPreviewToOutput") {
        commitPreviewToOutput();

    } else if (action == "cyclePostEffect") {
        visualControls.post.postEffectName = uiSystem.cyclePostEffect(visualControls.post.postEffectName, 1);
        controlsDirty = true;
        std::cout << "[Post-Effect] OSC cycle: " << visualControls.post.postEffectName << std::endl;

    } else if (action == "selectPostEffect") {
        const auto& names = uiSystem.getPostEffectNames();
        // arg 0 = none, 1..N = effect index
        if (arg == 0) {
            visualControls.post.postEffectName.clear();
            controlsDirty = true;
            std::cout << "[Post-Effect] OSC select: (none)" << std::endl;
        } else if (arg >= 1 && arg <= static_cast<int>(names.size())) {
            visualControls.post.postEffectName = names[arg - 1];
            controlsDirty = true;
            std::cout << "[Post-Effect] OSC select: " << names[arg - 1] << std::endl;
        }

    } else if (action == "randomizePostFx") {
        uiSystem.randomizePostFx(visualControls);
        controlsDirty = true;
        std::cout << "[OSC] Randomized Post FX (preview)" << std::endl;

    } else if (action == "randomizeVJayBasics") {
        uiSystem.randomizeVJayBasics(visualControls);
        controlsDirty = true;
        std::cout << "[OSC] Randomized VJAY Basics (preview)" << std::endl;

    } else if (action == "playPause") {
        previewPaused = !previewPaused;
        std::cout << "[Preview] " << (previewPaused ? "PAUSED" : "RESUMED") << "\n";

    } else if (action == "nudgeVideo1") {
        if (previewSlots[0].initialized) {
            double cur = previewSlots[0].player.getCurrentFramePTS();
            double dur = previewSlots[0].player.durationSeconds();
            double target = cur + arg;
            if (dur > 0) target = std::clamp(target, 0.0, dur);
            previewSlots[0].player.seekSeconds(target);
        }

    } else if (action == "nudgeVideo2") {
        if (previewSlots[1].initialized) {
            double cur = previewSlots[1].player.getCurrentFramePTS();
            double dur = previewSlots[1].player.durationSeconds();
            double target = cur + arg;
            if (dur > 0) target = std::clamp(target, 0.0, dur);
            previewSlots[1].player.seekSeconds(target);
        }

    } else if (action == "nudgeVideo3") {
        if (previewSlots[2].initialized) {
            double cur = previewSlots[2].player.getCurrentFramePTS();
            double dur = previewSlots[2].player.durationSeconds();
            double target = cur + arg;
            if (dur > 0) target = std::clamp(target, 0.0, dur);
            previewSlots[2].player.seekSeconds(target);
        }

    } else if (action == "tapTempo") {
        // Simple tap tempo: set animationTargetSeconds based on time between taps
        static auto lastTap = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTap).count();
        if (elapsed > 0.1 && elapsed < 3.0) {
            visualControls.playback.animationTargetSeconds = static_cast<float>(elapsed);
            controlsDirty = true;
            std::cout << "[TapTempo] " << elapsed << "s -> " << (60.0 / elapsed) << " BPM\n";
        }
        lastTap = now;

    } else if (action == "resetControls") {
        visualControls = VisualControls{};
        controlsDirty = true;
        std::cout << "[Controls] Reset to defaults\n";

    } else if (action == "loadPreset") {
        // arg is 1-based index into sorted preset list
        auto presets = listPresets();
        if (arg >= 1 && arg <= static_cast<int>(presets.size())) {
            loadPreset(presets[arg - 1]);
            std::cout << "[Preset] Loaded: " << presets[arg - 1] << "\n";
        }

    } else if (action == "savePreset") {
        // arg is used as preset name index (1-based), or save to "osc_preset_N"
        auto presets = listPresets();
        if (arg >= 1 && arg <= static_cast<int>(presets.size())) {
            savePreset(presets[arg - 1]);
            std::cout << "[Preset] Saved: " << presets[arg - 1] << "\n";
        } else {
            std::string name = "osc_preset_" + std::to_string(arg);
            savePreset(name);
            std::cout << "[Preset] Saved: " << name << "\n";
        }
    }
}

void Application::commitPreviewToOutput() {
    // La GPU puede estar usando descriptor sets / texturas de frames en vuelo;
    // esperar antes de reescribirlos o recrear texturas de video.
    vkDeviceWaitIdle(vulkanContext.getDevice());

    outputVisualControls = visualControls;
    std::string oldOutputVideoSourcePath = outputSlots[0].sourcePath;
    std::string oldOutputVideoSourcePath2 = outputSlots[1].sourcePath;
    std::string oldOutputVideoSourcePath3 = outputSlots[2].sourcePath;
    outputSlots[0].sourcePath = previewSlots[0].sourcePath;
    outputSlots[1].sourcePath = previewSlots[1].sourcePath;
    outputSlots[2].sourcePath = previewSlots[2].sourcePath;
    outputSlots[0].selectedVideoAsset = previewSlots[0].selectedVideoAsset;
    outputSlots[1].selectedVideoAsset = previewSlots[1].selectedVideoAsset;
    outputSlots[2].selectedVideoAsset = previewSlots[2].selectedVideoAsset;

    // Sync animation state so time-based effects (LFO, wobble, etc.) match
    outputAnim = previewAnim;

    // Sync audio smoothing state so audio-reactive values match
    outputPrevAudioSmooth = prevAudioSmooth;

    if (!outputSlots[0].sourcePath.empty() && outputSlots[0].sourcePath != oldOutputVideoSourcePath)
        reloadOutputVideoSlot(0, outputSlots[0].sourcePath);
    if (!outputSlots[1].sourcePath.empty() && outputSlots[1].sourcePath != oldOutputVideoSourcePath2)
        reloadOutputVideoSlot(1, outputSlots[1].sourcePath);
    if (!outputSlots[2].sourcePath.empty() && outputSlots[2].sourcePath != oldOutputVideoSourcePath3)
        reloadOutputVideoSlot(2, outputSlots[2].sourcePath);

    updateAllDescriptorSets();
    std::cout << "[Output] Controls + videos committed from preview\n";
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
    previewFrameSystem.waitForAllFences();
    previewFrameSystem.cleanup();

    // Recreate preview swapchain with actual window size
    previewPresenter.recreate(width, height, renderPass);

    // The preview image has a fixed internal resolution: resizing the window / panels only recreates the swapchain.

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
    outputFrameSystem.waitForAllFences();
    outputFrameSystem.cleanup();

    // Recreate output swapchain with actual window size
    outputPresenter.recreate(width, height, renderPass);
    std::cout << "[Resize] Output swapchain extent: " << outputPresenter.getExtent().width
              << "x" << outputPresenter.getExtent().height << "\n";

    if (outputSlots[0].initialized && outputMultiPassPipeline.isInitialized()) {
        outputMultiPassPipeline.recreate(outputPresenter.getExtent());
        std::cout << "[Resize] Output multipass pipeline recreated\n";
    } else {
        std::cout << "[Resize] Output multipass pipeline NOT recreated (vidInit="
                  << outputSlots[0].initialized << " pipelineInit="
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
    // handleWindowResize/handleOutputWindowResize wait only for the affected
    // frame fences, avoiding a device-wide stall during the modal loop. Instead,
    // let tryRenderPreview/tryRenderOutput skip the out-of-date
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
        float step = deltaTime / previewSlots[0].transitionDuration;
        transitionProgress += step;
        if (transitionProgress >= 1.0f) {
            transitionProgress = 1.0f;
            transitionActive = false;
            previewSlots[0].texture.setFreezePrev(false);
            previewSlots[1].texture.setFreezePrev(false);
        }
    }

    // ── Auto-randomize colors ────────────────────────────────────────────
    float quickEnergy = std::min(audioSystem.getRMS() * visualControls.audio.inputGain * 3.0f, 1.0f);
    tickAutoColors(deltaTime, quickEnergy);

    // ── Auto-randomize videos ────────────────────────────────────────────
    tickAutoRandomize(deltaTime, now);

    // ── Auto random jump ─────────────────────────────────────────────────
    if (visualControls.playback.enableRandomJumpInterval &&
        visualControls.playback.randomVideoStart && previewSlots[0].initialized)
    {
        float elapsed = std::chrono::duration<float>(now - lastRandomJumpTime).count();
        if (elapsed >= visualControls.playback.randomJumpInterval) {
            const double dur = previewSlots[0].player.durationSeconds();
            if (dur > 0) {
                previewSlots[0].player.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));
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
    if (previewSlots[0].initialized)  previewSlots[0].player.setPlaybackRate(visualControls.playback.videoPlaybackRate);
    if (previewSlots[1].initialized) previewSlots[1].player.setPlaybackRate(visualControls.playback.video2PlaybackRate);
    if (previewSlots[2].initialized) previewSlots[2].player.setPlaybackRate(visualControls.playback.video3PlaybackRate);
    if (outputSlots[0].initialized)  outputSlots[0].player.setPlaybackRate(outputVisualControls.playback.videoPlaybackRate);
    if (outputSlots[1].initialized) outputSlots[1].player.setPlaybackRate(outputVisualControls.playback.video2PlaybackRate);
    if (outputSlots[2].initialized) outputSlots[2].player.setPlaybackRate(outputVisualControls.playback.video3PlaybackRate);

    if (visualControls.playback.videoPlaybackRate != previewSlots[0].lastPlaybackRate) {
        previewSlots[0].lastPlaybackRate = visualControls.playback.videoPlaybackRate;
        videoSpeedCache[previewSlots[0].sourcePath] = previewSlots[0].lastPlaybackRate;
    }
    if (visualControls.playback.video2PlaybackRate != previewSlots[1].lastPlaybackRate) {
        previewSlots[1].lastPlaybackRate = visualControls.playback.video2PlaybackRate;
        videoSpeedCache[previewSlots[1].sourcePath] = previewSlots[1].lastPlaybackRate;
    }
    if (visualControls.playback.video3PlaybackRate != previewSlots[2].lastPlaybackRate) {
        previewSlots[2].lastPlaybackRate = visualControls.playback.video3PlaybackRate;
        videoSpeedCache[previewSlots[2].sourcePath] = previewSlots[2].lastPlaybackRate;
    }

    // ── UI ───────────────────────────────────────────────────────────────
    UIDiagnostics diag;
    diag.lastFrameFrameIndex       = previewFrameSystem.currentFrameIndex();
    diag.lastFrameImageIndex       = 0;
    diag.swapchainWidth            = previewPresenter.getExtent().width;
    diag.swapchainHeight           = previewPresenter.getExtent().height;
    diag.currentMode               = visualControls.playback.activeMode;
    diag.videoReady                = previewSlots[0].initialized && previewSlots[0].texture.isReady();
    diag.videoWidth                = previewSlots[0].texture.getWidth();
    diag.videoHeight               = previewSlots[0].texture.getHeight();
    diag.animationTime             = debugAnimationTime;
    diag.animationDelta            = debugAnimationDelta;
    diag.animationElapsedSeconds   = debugAnimationElapsedSeconds;
    diag.gpuPassTimes              = multiPassPipeline.lastGpuPassTimes;
    diag.gpuTotalTime              = multiPassPipeline.lastGpuTotalTime;
    diag.appFps                    = currentFps;

    uiSystem.render(visualControls, previewSlots[0].randomizer, previewSlots[1].randomizer, previewSlots[2].randomizer,
                    previewSlots[0].player, previewSlots[1].player, previewSlots[2].player, videoRegistry,
                    previewSlots[0].selectedVideoAsset, previewSlots[1].selectedVideoAsset, previewSlots[2].selectedVideoAsset,
                    previewSlots[0].transitionDuration, previewSlots[1].transitionDuration, previewSlots[2].transitionDuration,
                    allowDimensionChangeRecreation, controlsDirty,
                    rng, diag, cachedCallbacks, midiSystem, oscSystem, audioSystem,
                    previewSlots[0].sourcePath, previewSlots[1].sourcePath, previewSlots[2].sourcePath,
                    videoAssetsRoot);

    // ── Render preview and output independently ──────────────────────────
    // Output is only updated from preview via the ENTER key commit,
    // not every frame — they are independent windows.
    tryRenderPreview();
    tryRenderOutput();
}

// ─────────────────────────────────────────────────────────────────────────────
// submitAndPresent — shared submit+present logic for both windows
// ─────────────────────────────────────────────────────────────────────────────

bool Application::submitAndPresent(FrameSystem& frameSystem, VulkanPresenter& presenter,
                                    FrameContext* frame, uint32_t imageIndex,
                                    VkCommandBuffer cmdBuffer,
                                    std::function<void()> onResizeNeeded) {
    auto renderFinished = frameSystem.getRenderFinishedSemaphore(imageIndex).value_or(VK_NULL_HANDLE);
    if (renderFinished == VK_NULL_HANDLE)
        throw std::runtime_error("invalid renderFinished semaphore");

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &frame->imageAvailableSemaphore;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmdBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &renderFinished;

    if (vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &submit, frame->inFlightFence) != VK_SUCCESS) {
        std::cerr << "[submitAndPresent] vkQueueSubmit failed — device may be lost\n";
        VkSubmitInfo emptySubmit{};
        emptySubmit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &emptySubmit, frame->inFlightFence);
        frameSystem.endFrame();
        return false;
    }

    VkSwapchainKHR sc = presenter.getSwapchain();
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &renderFinished;
    present.swapchainCount     = 1;
    present.pSwapchains        = &sc;
    present.pImageIndices      = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(vulkanContext.getPresentQueue(), &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        onResizeNeeded();
    }

    frameSystem.endFrame();
    return true;
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

    updateUniformBuffer(previewFrame->frameIndex, visualControls,
                        uniformBufferManager, previewPresenter, previewAnim,
                        previewSlots[0].texture, previewSlots[1].texture, previewSlots[2].texture,
                        previewSlots[0].initialized, previewSlots[1].initialized, previewSlots[2].initialized);

    // Update preview video renderers
    if (!uiSystem.isRendererPaused() && !previewPaused) {
        if (previewSlots[0].renderer)  previewSlots[0].renderer->update(currentDeltaTime, previewFrame->frameIndex);
        if (previewSlots[1].renderer) previewSlots[1].renderer->update(currentDeltaTime, previewFrame->frameIndex);
        if (previewSlots[2].renderer) previewSlots[2].renderer->update(currentDeltaTime, previewFrame->frameIndex);
    }

    // Record command buffer
    recordPreviewCommandBuffer(previewCommandBuffers[previewFrame->frameIndex],
                               *previewFrame, previewImageIndex);

    auto onResize = [&] {
        uint32_t w, h;
        window.getDrawableSize(w, h);
        if (w > 0 && h > 0) {
            pendingResizeW = w;
            pendingResizeH = h;
            resizePending = true;
            auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
        }
    };

    return submitAndPresent(previewFrameSystem, previewPresenter,
                            previewFrame, previewImageIndex,
                            previewCommandBuffers[previewFrame->frameIndex],
                            onResize);
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
                        outputSlots[0].texture, outputSlots[1].texture, outputSlots[2].texture,
                        outputSlots[0].initialized, outputSlots[1].initialized, outputSlots[2].initialized,
                        true);

    // Update output video renderers with output frame index
    if (outputSlots[0].renderer)  outputSlots[0].renderer->update(currentDeltaTime, outputFrame->frameIndex);
    if (outputSlots[1].renderer) outputSlots[1].renderer->update(currentDeltaTime, outputFrame->frameIndex);
    if (outputSlots[2].renderer) outputSlots[2].renderer->update(currentDeltaTime, outputFrame->frameIndex);

    // Record command buffer
    recordOutputCommandBuffer(outputCommandBuffers[outputFrame->frameIndex],
                              *outputFrame, outputImageIndex);

    auto onResize = [&] {
        int ow = 0, oh = 0;
        SDL_Vulkan_GetDrawableSize(window.getOutputWindow(), &ow, &oh);
        if (ow > 0 && oh > 0) {
            pendingOutputResizeW = static_cast<uint32_t>(ow);
            pendingOutputResizeH = static_cast<uint32_t>(oh);
            outputResizePending = true;
            auto newDebounce = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            resizeDebounceTime = std::max(resizeDebounceTime, newDebounce);
        }
    };

    return submitAndPresent(outputFrameSystem, outputPresenter,
                            outputFrame, outputImageIndex,
                            outputCommandBuffers[outputFrame->frameIndex],
                            onResize);
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
        processSDLEvents();
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

        frameLimit();
    }
}

void Application::processSDLEvents() {
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

        // Only the preview window hosts the UI: do not feed it mouse/keyboard events of other windows.
        bool consumed = false;
        {
            Uint32 evWindow = 0;
            switch (event.type) {
                case SDL_MOUSEMOTION:                          evWindow = event.motion.windowID; break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:                        evWindow = event.button.windowID; break;
                case SDL_MOUSEWHEEL:                           evWindow = event.wheel.windowID;  break;
                case SDL_KEYDOWN:
                case SDL_KEYUP:                                evWindow = event.key.windowID;    break;
                case SDL_TEXTINPUT:                            evWindow = event.text.windowID;   break;
                case SDL_TEXTEDITING:                          evWindow = event.edit.windowID;   break;
                default: break;
            }
            if (evWindow == 0 || evWindow == SDL_GetWindowID(window.getMainWindow())) {
                uiSystem.processEvent(event);
                // ImGui "handles" every key of its own window; the hotkeys must only yield while a text field is active.
                const ImGuiIO& uio = ImGui::GetIO();
                if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT || event.type == SDL_TEXTEDITING)
                    consumed = uio.WantTextInput;
            }
        }

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
                    commitPreviewToOutput();
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
}

void Application::frameLimit() {
    // ── Frame rate limiter (high-precision) ────────────────────────────
    // std::this_thread::sleep_for has ~15ms resolution on Windows which
    // makes 60fps impossible. Use a hybrid: sleep for most of the wait,
    // then spin-wait the last ~2ms for precision.
    constexpr float targetFrameTime = 1.0f / 60.0f;
    const auto frameEnd = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(frameEnd - lastFrameTimestamp).count();
    if (elapsed < targetFrameTime) {
        const auto targetTime = lastFrameTimestamp + std::chrono::microseconds(
            static_cast<int64_t>(targetFrameTime * 1e6f));
        const auto sleepUntil = targetTime - std::chrono::milliseconds(2);
        if (sleepUntil > frameEnd)
            std::this_thread::sleep_until(sleepUntil);
        // Spin-wait the remaining time for precision
        while (std::chrono::steady_clock::now() < targetTime) {
            std::this_thread::yield();
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
        float speed = 30.0f + energy * 200.0f; // 30-230 deg/sec based on energy
        autoColorHue += speed * dt;
        if (autoColorHue >= 360.0f) autoColorHue -= 360.0f;

        float sat = 0.5f + energy * 0.5f;  // 0.5-1.0
        float val = 0.6f + energy * 0.4f;  // 0.6-1.0

        c.primaryColorTarget   = glm::vec4(hsvToRgb(autoColorHue, sat, val), 1.f);
        c.secondaryColorTarget = glm::vec4(hsvToRgb(std::fmod(autoColorHue + 180.f, 360.f), sat, val), 1.f);
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
        previewSlots[0].isReloading = false;
    };

    tick(previewSlots[0].randomizer.autoRandomize,  previewSlots[0].initialized,
         previewSlots[0].randomizer,  0, visualControls.playback.selectedVideoFolder);

    tick(previewSlots[1].randomizer.autoRandomize && visualControls.playback.enableDualVideo,
         previewSlots[0].initialized,
         previewSlots[1].randomizer, 1, visualControls.playback.selectedVideo2Folder);

    tick(previewSlots[2].randomizer.autoRandomize && visualControls.playback.enableDualVideo,
         previewSlots[0].initialized,
         previewSlots[2].randomizer, 2, visualControls.playback.selectedVideo3Folder);
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
        std::string path = previewSlots[0].sourcePath;
        if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_2) { slot = 1; path = previewSlots[1].sourcePath; }
        else if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_3) { slot = 2; path = previewSlots[2].sourcePath; }
        reloadVideoSlot(slot, path);
    };

    cb.onFolderChanged = [this]() {
        previewSlots[0].randomizer.shuffleQueue.clear();  previewSlots[0].randomizer.currentShuffleIndex  = 0;
        previewSlots[1].randomizer.shuffleQueue.clear(); previewSlots[1].randomizer.currentShuffleIndex = 0;
        previewSlots[2].randomizer.shuffleQueue.clear(); previewSlots[2].randomizer.currentShuffleIndex = 0;
    };

    cb.onFolderChanged2 = [this]() {
        previewSlots[0].randomizer.shuffleQueue.clear();  previewSlots[0].randomizer.currentShuffleIndex  = 0;
        previewSlots[1].randomizer.shuffleQueue.clear(); previewSlots[1].randomizer.currentShuffleIndex = 0;
        previewSlots[2].randomizer.shuffleQueue.clear(); previewSlots[2].randomizer.currentShuffleIndex = 0;
    };

    cb.onFolderChanged3 = [this]() {
        previewSlots[0].randomizer.shuffleQueue.clear();  previewSlots[0].randomizer.currentShuffleIndex  = 0;
        previewSlots[1].randomizer.shuffleQueue.clear(); previewSlots[1].randomizer.currentShuffleIndex = 0;
        previewSlots[2].randomizer.shuffleQueue.clear(); previewSlots[2].randomizer.currentShuffleIndex = 0;
    };

    cb.onRandomizeVideo = [this]() {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        if (assets.size() > 1) reloadVideoAtIndex(pickNextVideoIndex(assets), assets);
        previewSlots[0].isReloading = false;
    };

    cb.onRandomizeVideo2 = [this]() {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo2Folder);
        if (assets.size() > 1) reloadVideoAtIndex2(pickNextVideoIndex2(assets), assets);
        previewSlots[0].isReloading = false;
    };

    cb.onRandomizeVideo3 = [this]() {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideo3Folder);
        if (assets.size() > 1) reloadVideoAtIndex3(pickNextVideoIndex3(assets), assets);
        previewSlots[0].isReloading = false;
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
        if (!previewSlots[0].initialized) return;
        const double dur = previewSlots[0].player.durationSeconds();
        if (dur > 0) previewSlots[0].player.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));
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
        previewSlots[0].sourcePath.clear();
        previewSlots[1].sourcePath.clear();
        previewSlots[2].sourcePath.clear();
        previewSlots[0].selectedVideoAsset = 0;
        previewSlots[1].selectedVideoAsset = 0;
        previewSlots[2].selectedVideoAsset = 0;

        // Reinitialize video players from the new root
        initVideo();
        initOutputVideo();

        // If MultiPassPipeline wasn't initialized (e.g. app started with no video),
        // initialize it now that video textures are available.
        if (previewSlots[0].initialized && !multiPassPipeline.isInitialized()) {
            initMultiPassPipeline();
        }

        updateAllDescriptorSets();
        saveState();
    };

    cb.onListPresets = [this]() { return listPresets(); };
    cb.onSavePreset = [this](const std::string& name) { return savePreset(name); };
    cb.onSavePresetFromPreview = [this](const std::string& name) { return savePresetFromPreview(name); };
    cb.onSavePresetFromOutput = [this](const std::string& name) { return savePresetFromOutput(name); };
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
    // One tracker per target: preview and output call this every frame and may
    // legitimately hold different modes (output only changes on "commit").
    static int lastLoggedMode[2] = {-1, -1};
    int& lastLogged = lastLoggedMode[forOutput ? 1 : 0];
    if (controls.playback.activeMode != lastLogged) {
        lastLogged = controls.playback.activeMode;
        const int mode = controls.playback.activeMode;
        const bool compiled = mode <= 12 || mode == 13 || mode == 14 || mode == 15 ||
                              mode == 16 || mode == 17 || mode == 18 || mode == 19 || mode == 20 ||
                              mode == 40 || mode == 76 || mode == 77 ||
                              mode == 84 || mode == 85 || mode == 90 || mode == 91 ||
                              mode == 78 || mode == 79 || mode == 82 || mode == 83 || mode == 87 ||
                              mode == 89 || mode == 92 || mode == 93 || mode == 95 ||
                              (mode >= 96 && mode <= 110);
        std::cerr << "[Procedural] " << (forOutput ? "OUTPUT" : "PREVIEW") << " activeMode=" << mode
                  << (compiled ? " compiled in pass_a_base" : " NOT compiled in pass_a_base")
                  << '\n';
    }

    GlobalParamsUBO ubo{};
    const VkExtent2D renderExtent = forOutput ? presenter.getExtent() : previewRenderExtent;

    // Calculate time delta and accumulate (similar to your system)
    auto currentTime = std::chrono::high_resolution_clock::now();
    if (!anim.timeInitialized) {
        anim.lastGlobalTime = currentTime;
        anim.accumulatedTime = 0.0f;
        anim.timeInitialized = true;
    }

    float globalDeltaTime = std::chrono::duration<float>(currentTime - anim.lastGlobalTime).count();
    anim.lastGlobalTime = currentTime;
    globalDeltaTime = std::clamp(globalDeltaTime, 0.0f, 0.1f);

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
        // Clamp to minimum 1.0x speed (Shadertoy standard) and maximum 5.0x
        tempoValue = std::clamp(tempoValue, 1.0f, 5.0f);
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
    // Procedural layers must always move and NOT depend on audio:
    // audio-reactive mode -> constant 1.0x; manual mode -> manual tempo (+LFO).
    anim.shaderTime += globalDeltaTime * speed * (controls.system.enableAudioReactive ? 1.0f : tempoValue);

    auto& reactive = controls.runtime.audioReactive;
    reactive.enabled = controls.system.enableAudioReactive;
    reactive.energy = liveEnergy;
    reactive.bass   = liveBass;
    reactive.mid    = liveMid;
    reactive.high   = liveHigh;

    // Apply user-controlled temporal smoothing to audio values
    // smoothAmount: 0 = raw (no extra smoothing), 1 = very smooth
    // Use separate smoothing state for preview vs output so they don't interfere
    AudioSmoothState& smooth = forOutput ? outputPrevAudioSmooth : prevAudioSmooth;
    if (controls.audio.smoothAmount > 0.001f) {
        float smoothRate = 1.0f - std::pow(1.0f - controls.audio.smoothAmount, globalDeltaTime * 60.0f);
        reactive.energy = glm::mix(reactive.energy, smooth.energy, smoothRate);
        reactive.bass   = glm::mix(reactive.bass,   smooth.bass,   smoothRate);
        reactive.mid    = glm::mix(reactive.mid,    smooth.mid,    smoothRate);
        reactive.high   = glm::mix(reactive.high,   smooth.high,   smoothRate);
    }
    smooth.energy = reactive.energy;
    smooth.bass   = reactive.bass;
    smooth.mid    = reactive.mid;
    smooth.high   = reactive.high;

    if (controls.system.enableAudioReactive) {
        controls.audio.energy = reactive.energy;
        controls.audio.bass   = reactive.bass;
        controls.audio.mid    = reactive.mid;
        controls.audio.high   = reactive.high;
    }

    // Set basic UBO values
    ubo.model = glm::mat4(1.0f);
    ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0), glm::vec3(0.0, 1.0, 0.0));
    float extW = static_cast<float>(renderExtent.width);
    float extH = static_cast<float>(renderExtent.height);
    float aspect = (extH > 0.0f) ? (extW / extH) : 1.0f;
    ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1.0f;
    ubo.resolution = glm::vec2(static_cast<float>(renderExtent.width),
                               static_cast<float>(renderExtent.height));
    ubo.videoResolution = glm::vec2(static_cast<float>(vid1.getWidth()),
                                    static_cast<float>(vid1.getHeight()));
    ubo.time = anim.shaderTime;
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
    ubo.video1Rotation = controls.playback.video1Rotation;
    ubo.video2Rotation = controls.playback.video2Rotation;
    ubo.video3Rotation = controls.playback.video3Rotation;

    // Set visual control values
    ubo.primaryColor = controls.color.primaryColor;
    ubo.secondaryColor = controls.color.secondaryColor;
    ubo.colorBlend = controls.color.colorBlend;

    // Audio-driven RGB color modulation: bass→R, mid→G, high→B
    if (controls.color.enableAudioColorRGB) {
        float amt = controls.color.audioColorRGBAmount;
        float bMod = 1.0f + liveBass * amt;
        float gMod = 1.0f + liveMid * amt;
        float rMod = 1.0f + liveHigh * amt;
        ubo.primaryColor.r   *= rMod;
        ubo.primaryColor.g   *= gMod;
        ubo.primaryColor.b   *= bMod;
        ubo.secondaryColor.r *= rMod;
        ubo.secondaryColor.g *= gMod;
        ubo.secondaryColor.b *= bMod;
    }
    ubo.tempo = tempoValue;
    // Procedural layers: feed slow synthetic signals instead of the real audio so
    // every layer renders and animates on its own (silence no longer blanks them).
    // Audio tab > "Procedural layers react to audio" switches back to the live audio.
    if (!controls.audio.proceduralReactsToAudio) {
        const float ts = anim.shaderTime;
        ubo.energy = 0.55f  + 0.25f * std::sin(ts * 0.7f);
        ubo.bass   = 0.575f + 0.325f * std::sin(ts * 1.3f);
        ubo.mid    = 0.55f  + 0.30f * std::sin(ts * 0.9f + 1.7f);
        ubo.high   = 0.50f  + 0.30f * std::sin(ts * 1.7f + 3.1f);
    } else {
        ubo.energy = reactive.energy;
        ubo.bass = reactive.bass;
        ubo.mid = reactive.mid;
        ubo.high = reactive.high;
    }
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
    // "Frame accumulation" is shown inside the Feedback section, so it must work with that toggle alone
    ubo.enableTemporal = (controls.temporal.enableTemporal || controls.temporal.enableFeedback) ? 1 : 0;

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
    float envClamped  = std::clamp(reactive.energy, 0.0f, 1.0f);
    float bassClamped = std::clamp(reactive.bass,   0.0f, 1.0f);
    float midClamped  = std::clamp(reactive.mid,    0.0f, 1.0f);
    float highClamped = std::clamp(reactive.high,   0.0f, 1.0f);

    float warpGain     = std::max(0.0f, controls.audio.warpResponse);
    float feedbackGain = std::max(0.0f, controls.audio.feedbackResponse);
    float blurGain     = std::max(0.0f, controls.audio.blurResponse);
    float colorGain    = std::max(0.0f, controls.audio.colorResponse);
    float glitchGain   = std::max(0.0f, controls.audio.glitchResponse);

    if (controls.system.enableAudioReactive) {
        // Spatial distortion (Pass B) — only boost effects the user has enabled (>0)
        if (ubo.uvWarpStrength    > 0.0001f) ubo.uvWarpStrength    = std::max(ubo.uvWarpStrength,    envClamped  * 0.15f * warpGain);
        if (ubo.rippleStrength    > 0.0001f) ubo.rippleStrength    = std::max(ubo.rippleStrength,    bassClamped * 0.30f * warpGain);
        if (ubo.swirlStrength      > 0.0001f) ubo.swirlStrength     = std::max(ubo.swirlStrength,     bassClamped * 0.20f * warpGain);
        if (ubo.displacementAmount> 0.0001f) ubo.displacementAmount= std::max(ubo.displacementAmount,envClamped  * 0.12f * warpGain);
        if (ubo.bendAmount        > 0.0001f) ubo.bendAmount        = std::max(ubo.bendAmount,        envClamped  * 0.10f * warpGain);

        // Temporal / feedback (Pass D)
        if (ubo.feedbackAmount    > 0.0001f) ubo.feedbackAmount    = std::max(ubo.feedbackAmount,    envClamped  * 0.25f * feedbackGain);
        if (ubo.trailStrength     > 0.0001f) ubo.trailStrength     = std::max(ubo.trailStrength,     bassClamped * 0.20f * feedbackGain);

        // Degradation / glitch (Pass E)
        if (ubo.glitchJitter      > 0.0001f) ubo.glitchJitter      = std::max(ubo.glitchJitter,      envClamped  * 0.15f * glitchGain);
        if (ubo.glitchRGBSplit    > 0.0001f) ubo.glitchRGBSplit    = std::max(ubo.glitchRGBSplit,    bassClamped * 0.10f * glitchGain);
        if (ubo.glitchTearing     > 0.0001f) ubo.glitchTearing     = std::max(ubo.glitchTearing,     midClamped  * 0.08f * glitchGain);
        if (ubo.grainStrength     > 0.0001f) ubo.grainStrength     = std::max(ubo.grainStrength,     envClamped  * 0.20f * blurGain);

        // Output extras (Pass G)
        if (ubo.zoomPulseAmount   > 0.0001f) ubo.zoomPulseAmount   = std::max(ubo.zoomPulseAmount,   envClamped  * 0.25f * colorGain);
        if (ubo.slowZoomAmount    > 0.0001f) ubo.slowZoomAmount    = std::max(ubo.slowZoomAmount,    envClamped  * 0.30f * blurGain);
        if (ubo.strobeSpeed       > 0.0001f) ubo.strobeSpeed       = std::max(ubo.strobeSpeed,       bassClamped * 3.0f * colorGain);
        if (ubo.rgbShiftAmount    > 0.0001f) ubo.rgbShiftAmount    = std::max(ubo.rgbShiftAmount,    highClamped * 0.03f * colorGain);

        // Camera movement (2D layer camera)
        if (controls.camera.enableMovement) {
            // Strong zoom pulse driven by bass (max 20% closer)
            float zoomPulse = 1.0f + (bassClamped * 0.20f + envClamped * 0.06f) * warpGain;
            // Multiplicative so the Zoom slider stays authoritative (was max(): any zoom < 1 was ignored)
            ubo.cameraZoom = ubo.cameraZoom * zoomPulse;

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

    // Pass 1 (B-spatial): skip if no spatial distortion is active
    const bool spatialPassActive =
        ubo.uvWarpStrength > 0.0001f || ubo.rippleStrength > 0.0001f ||
        abs(ubo.swirlStrength) > 0.0001f || ubo.displacementAmount > 0.0001f ||
        ubo.tunnelDepth > 0.0001f || ubo.bendAmount > 0.0001f ||
        ubo.enablePixelate == 1 || ubo.enableSlowZoom == 1 ||
        ubo.enableMirror == 1 || ubo.enableZoomPulse == 1 ||
        ubo.enablePostBend == 1;
    pipeline.setPassEnabled(1, spatialPassActive);

    const bool detailPassActive =
        ubo.gaussianBlur != 0.0f || ubo.directionalBlur != 0.0f || ubo.zoomBlur != 0.0f ||
        ubo.motionBlur != 0.0f || ubo.temporalBlur != 0.0f ||
        ubo.unsharpMask != 0.0f || ubo.casAmount != 0.0f || ubo.localContrast != 0.0f;
    pipeline.setPassEnabled(2, detailPassActive);

    // Pass 3 (D-temporal): skip if no feedback/temporal effect is active
    const bool temporalPassActive =
        ubo.enableFeedback == 1 || ubo.enableTemporal == 1;
    pipeline.setPassEnabled(3, temporalPassActive);

    const bool degradationPassActive =
        ubo.glitchAmount != 0.0f || ubo.glitchDatamosh != 0.0f || ubo.glitchRGBSplit != 0.0f ||
        ubo.glitchScanlineBreak != 0.0f || ubo.glitchJitter != 0.0f || ubo.glitchTearing != 0.0f ||
        ubo.glitchPixelSort != 0.0f || ubo.glitchBufferCorruption != 0.0f;
    pipeline.setPassEnabled(4, degradationPassActive);

    pipeline.setPassEnabled(5, ubo.enableColorGrading != 0);

    // Pass 6 (G-output): skip if no output effect is active
    const bool outputPassActive =
        ubo.enableFXAA == 1 || ubo.enableEdgeDetect == 1 ||
        ubo.crtCurvature > 0.0001f || ubo.crtScanlineIntensity > 0.0001f ||
        ubo.crtMaskIntensity > 0.0001f || ubo.crtVignette > 0.0001f ||
        ubo.crtFishEye > 0.0001f || ubo.bloomIntensity > 0.0001f ||
        ubo.aberrationAmount > 0.0001f || ubo.grainStrength > 0.0001f ||
        ubo.enableGrid == 1 || ubo.enableCameraMovement == 1 ||
        ubo.enableRgbOverlay == 1 || ubo.masterBrightness != 1.0f ||
        !controls.post.postEffectName.empty();
    pipeline.setPassEnabled(6, outputPassActive);

    // Selectable post-effect slot parameters
    ubo.postEffectStrength = controls.post.postEffectStrength;
    ubo.postEffectIntensity = controls.post.postEffectIntensity;
    ubo.postEffectMode = controls.post.postEffectMode;
    ubo.postEffectBass = reactive.bass;
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

    const VkRect2D previewRect = uiSystem.getPreviewRect();
    const std::function<void(VkCommandBuffer)> uiDraw = [this](VkCommandBuffer c) { uiSystem.recordDrawData(c); };

    previewSlots[0].texture.recordPendingUpload(cmd, previewFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (previewSlots[1].initialized && previewSlots[1].texture.isReady())
        previewSlots[1].texture.recordPendingUpload(cmd, previewFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (previewSlots[2].initialized && previewSlots[2].texture.isReady())
        previewSlots[2].texture.recordPendingUpload(cmd, previewFrame.frameIndex, vulkanContext.getGraphicsQueue());

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
        if (previewRect.extent.width > 0 && previewRect.extent.height > 0) {
            sc = previewRect;
            vp = { (float)previewRect.offset.x, (float)previewRect.offset.y,
                   (float)previewRect.extent.width, (float)previewRect.extent.height, 0, 1 };
        }
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        {
            VkViewport full{ 0, 0, (float)ext.width, (float)ext.height, 0, 1 };
            VkRect2D   fullSc{ {0,0}, ext };
            vkCmdSetViewport(cmd, 0, 1, &full);
            vkCmdSetScissor(cmd, 0, 1, &fullSc);
            uiDraw(cmd);
        }
        vkCmdEndRenderPass(cmd);
    } else if (previewSlots[0].initialized && multiPassPipeline.isInitialized()) {
        multiPassPipeline.execute(
            cmd, previewFrame.frameIndex,
            descriptorSetManager.getSet(previewFrame.frameIndex),
            renderPass, previewPresenter.getFramebuffers(), previewImageIndex,
            fullscreenPipeline, pipelineLayout,
            fullscreenDescriptorSets[previewFrame.frameIndex],
            previewPresenter.getExtent(), swapchainSampler,
            1, // previewOverlay
            &previewRect, &uiDraw
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
        if (previewRect.extent.width > 0 && previewRect.extent.height > 0) {
            sc = previewRect;
            vp = { (float)previewRect.offset.x, (float)previewRect.offset.y,
                   (float)previewRect.extent.width, (float)previewRect.extent.height, 0, 1 };
        }
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        {
            VkViewport full{ 0, 0, (float)ext.width, (float)ext.height, 0, 1 };
            VkRect2D   fullSc{ {0,0}, ext };
            vkCmdSetViewport(cmd, 0, 1, &full);
            vkCmdSetScissor(cmd, 0, 1, &fullSc);
            uiDraw(cmd);
        }
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

    if (outputSlots[0].initialized && outputSlots[0].texture.isReady())
        outputSlots[0].texture.recordPendingUpload(cmd, outputFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (outputSlots[1].initialized && outputSlots[1].texture.isReady())
        outputSlots[1].texture.recordPendingUpload(cmd, outputFrame.frameIndex, vulkanContext.getGraphicsQueue());
    if (outputSlots[2].initialized && outputSlots[2].texture.isReady())
        outputSlots[2].texture.recordPendingUpload(cmd, outputFrame.frameIndex, vulkanContext.getGraphicsQueue());

    if (outputSlots[0].initialized && outputMultiPassPipeline.isInitialized()) {
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
    previewSlots[0].renderer.reset();
    previewSlots[1].renderer.reset();
    previewSlots[2].renderer.reset();
    outputSlots[0].renderer.reset();
    outputSlots[1].renderer.reset();
    outputSlots[2].renderer.reset();

    previewSlots[0].player.shutdown();
    previewSlots[0].texture.destroy(resourceSystem, vulkanContext.getDevice());
    previewSlots[0].texture.cleanup(resourceSystem);
    previewSlots[1].player.shutdown();
    previewSlots[1].texture.destroy(resourceSystem, vulkanContext.getDevice());
    previewSlots[1].texture.cleanup(resourceSystem);
    previewSlots[2].player.shutdown();
    previewSlots[2].texture.destroy(resourceSystem, vulkanContext.getDevice());
    previewSlots[2].texture.cleanup(resourceSystem);
    outputSlots[0].player.shutdown();
    outputSlots[0].texture.destroy(resourceSystem, vulkanContext.getDevice());
    outputSlots[0].texture.cleanup(resourceSystem);
    outputSlots[1].player.shutdown();
    outputSlots[1].texture.destroy(resourceSystem, vulkanContext.getDevice());
    outputSlots[1].texture.cleanup(resourceSystem);
    outputSlots[2].player.shutdown();
    outputSlots[2].texture.destroy(resourceSystem, vulkanContext.getDevice());
    outputSlots[2].texture.cleanup(resourceSystem);

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
