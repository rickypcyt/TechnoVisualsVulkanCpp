// Application.cpp — refactorizado completo

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
#include <filesystem>
#include <nlohmann/json.hpp>
#include <SDL2/SDL.h>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers internos
// ─────────────────────────────────────────────────────────────────────────────

glm::ivec2 Application::getScreenSize() const {
    auto ext = vulkanContext.getSwapchainExtent();
    return glm::ivec2(static_cast<int>(ext.width), static_cast<int>(ext.height));
}

// Sincroniza todos los descriptor sets con el estado actual de las texturas.
void Application::updateAllDescriptorSets() {
    // Descriptor sets principales (multipass)
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            uniformBufferManager.getBuffers()[i],
            const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
            const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
        );
    }
    updateFullscreenDescriptorSets();

    // Multipass pipeline usa las dos fuentes de vídeo
    const bool has2 = videoTexture2.isReady();
    multiPassPipeline.updateDescriptorSets(
        uniformBufferManager.getBuffers(),
        videoTexture.getImageView(),
        videoTexture.getPrevImageView(),
        videoTexture.getSampler(),
        videoTexture.getSampler(),
        has2 ? videoTexture2.getImageView() : videoTexture.getImageView(),
        has2 ? videoTexture2.getSampler()   : videoTexture.getSampler()
    );
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
    } else {
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
        } else {
            visualControls.playback.video2PlaybackRate = speed;
            lastPlaybackRate2 = speed;
        }
    };

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
    } else {
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
    }

    restoreSpeed(slot, path);

    isReloadingVideo = false;
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
        allowDimensionChangeRecreation,
        oscSystem,
        selectedVideoAsset,
        selectedVideoAsset2,
        videoSourcePath,
        videoSourcePath2
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
    video["selectedVideoAsset"] = selectedVideoAsset;
    video["selectedVideoAsset2"] = selectedVideoAsset2;
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
        if (v.contains("selectedVideoAsset")) selectedVideoAsset = v["selectedVideoAsset"];
        if (v.contains("selectedVideoAsset2")) selectedVideoAsset2 = v["selectedVideoAsset2"];
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
    }

    // Reload videos to match preset state
    if (canChangeVideo()) {
        if (!videoSourcePath.empty()) reloadVideoSlot(0, videoSourcePath);
        if (!videoSourcePath2.empty()) reloadVideoSlot(1, videoSourcePath2);
    }

    controlsDirty = true;
    return true;
}

bool Application::deletePreset(const std::string& name) {
    std::string path = presetsDir + "/" + name + ".json";
    if (!std::filesystem::exists(path)) return false;
    return std::filesystem::remove(path);
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
    window.createMainWindow("Vulkan", WIDTH, HEIGHT);
    window.createUiWindow("Controls", 420, 420);

    initVulkan();
    initSwapchain();
    initRenderPass();

    descriptorSetManager.createLayout(vulkanContext.getDevice());
    descriptorSetManager.createPool(vulkanContext.getDevice());
    uniformBufferManager.createBuffers(resourceSystem, vulkanContext.getDevice());

    // Cargar estado antes del vídeo para que selectedVideoFolder sea correcto
    JsonSerializer::load(visualControlsPath, parameterRegistry);
    ControlState::load(
        controlStatePath,
        videoRandomizer,
        videoRandomizer2,
        allowDimensionChangeRecreation,
        oscSystem,
        selectedVideoAsset,
        selectedVideoAsset2,
        videoSourcePath,
        videoSourcePath2
    );

    initVideo();

    descriptorSetManager.createSets(vulkanContext.getDevice());
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(
            vulkanContext.getDevice(), i,
            uniformBufferManager.getBuffers()[i],
            const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
            const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
        );
    }

    initPipelines();
    updateFullscreenDescriptorSets();
    initFramebuffers();
    initUI();
    initNLE();
    initMultiPassPipeline();
    initMidi();
    initOsc();
    initAudio();
    initCommandBuffers();

    frameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT,
                     vulkanContext.getSwapchainImageCount());
    std::cout << "[Application] FrameSystem initialized\n";

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
    vulkanContext.createSurface(window.getMainWindow());
    vulkanContext.createCommandPool();
    resourceSystem.init(vulkanContext.getDevice(), vulkanContext.getPhysicalDevice());
}

void Application::initSwapchain() {
    uint32_t w, h;
    window.getDrawableSize(w, h);
    vulkanContext.createSwapchain(w, h);
}

void Application::initRenderPass() {
    VkAttachmentDescription color{};
    color.format         = vulkanContext.getSwapchainImageFormat();
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
    const auto& views = vulkanContext.getSwapchainImageViews();
    swapchainFramebuffers.resize(views.size());
    const auto ext = vulkanContext.getSwapchainExtent();

    for (size_t i = 0; i < views.size(); ++i) {
        VkFramebufferCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass      = renderPass;
        info.attachmentCount = 1;
        info.pAttachments    = &views[i];
        info.width           = ext.width;
        info.height          = ext.height;
        info.layers          = 1;

        if (vkCreateFramebuffer(vulkanContext.getDevice(), &info, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create framebuffer");
    }
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

    // Create pipeline layout for fullscreen using the fullscreen descriptor set layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &fullscreenDescriptorSetLayout;

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
    viewport.width = static_cast<float>(vulkanContext.getSwapchainExtent().width);
    viewport.height = static_cast<float>(vulkanContext.getSwapchainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vulkanContext.getSwapchainExtent();

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

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBufferManager.getBuffers()[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalParamsUBO);

        VkWriteDescriptorSet uboWrite{};
        uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboWrite.dstSet = fullscreenDescriptorSets[i];
        uboWrite.dstBinding = 0;
        uboWrite.dstArrayElement = 0;
        uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboWrite.descriptorCount = 1;
        uboWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(vulkanContext.getDevice(), 1, &uboWrite, 0, nullptr);
        // Binding 1 (inputTexture) lo actualiza execute() cada frame
        // apuntando al output final del multipass pipeline
    }
}

void Application::initCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = vulkanContext.getCommandPool();
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanContext.getDevice(), &info, commandBuffers.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate command buffers");
}

// ─────────────────────────────────────────────────────────────────────────────
// Video init
// ─────────────────────────────────────────────────────────────────────────────

void Application::initVideo() {
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    std::cout << "[Application] initVideo()\n";
    videoRegistry.scan(videoAssetsRoot);
    glm::ivec2 screenSize = getScreenSize();

    // ── Resolve paths ───────────────────────────────────────────────────────
    // Prefer saved paths; validate against the whole registry, then locate
    // inside the current folder.  If the path exists globally but not in the
    // selected folder we keep the path (video will load) and reset the index.
    auto resolvePath = [&](const std::string& folder,
                          int& selAsset,
                          std::string& path)
    {
        bool pathValid = false;
        if (!path.empty()) {
            for (const auto& asset : videoRegistry.getAssets()) {
                if (asset.metadata.path == path) { pathValid = true; break; }
            }
        }

        const auto& assets = videoRegistry.getFilteredAssets(folder);
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
            std::cout << "[initVideo] Folder '" << folder << "' is empty\n";
        }
    };

    std::cout << "[initVideo] Before resolve: V1 path=" << videoSourcePath
              << " idx=" << selectedVideoAsset << " folder="
              << visualControls.playback.selectedVideoFolder << "\n";
    std::cout << "[initVideo] Before resolve: V2 path=" << videoSourcePath2
              << " idx=" << selectedVideoAsset2 << " folder="
              << visualControls.playback.selectedVideo2Folder << "\n";

    resolvePath(visualControls.playback.selectedVideoFolder,
                selectedVideoAsset, videoSourcePath);
    resolvePath(visualControls.playback.selectedVideo2Folder,
                selectedVideoAsset2, videoSourcePath2);

    std::cout << "[initVideo] After resolve:  V1 path=" << videoSourcePath
              << " idx=" << selectedVideoAsset << "\n";
    std::cout << "[initVideo] After resolve:  V2 path=" << videoSourcePath2
              << " idx=" << selectedVideoAsset2 << "\n";

    // ── Lazy init: only init slot 1 when dual video is enabled ────────────
    bool initSlot1 = visualControls.playback.enableDualVideo;

    // ── Parallel player initialization (heaviest part: disk + codecs) ──────
    auto tPlayer = Clock::now();

    auto future0 = std::async(std::launch::async, [&]() {
        return videoPlayer.initialize(videoSourcePath, screenSize.x, screenSize.y);
    });

    std::future<bool> future1;
    if (initSlot1) {
        future1 = std::async(std::launch::async, [&]() {
            return videoPlayer2.initialize(videoSourcePath2, screenSize.x, screenSize.y);
        });
    }

    bool ok0 = future0.get();
    bool ok1 = initSlot1 ? future1.get() : false;

    auto tPlayerDone = Clock::now();
    std::cout << "[Application] Player init: "
              << std::chrono::duration<double>(tPlayerDone - tPlayer).count()
              << "s (slot0=" << ok0 << " slot1=" << ok1 << ")\n";

    if (!ok0) {
        std::cerr << "[Application] Failed to initialize video player 1\n";
        return;
    }
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

    if (initSlot1 && ok1) {
        videoPlayer2.setAutoScale(visualControls.playback.autoScaleVideo);
        cpuFramePool2.resize(videoPlayer2.width(), videoPlayer2.height(), videoPlayer2.width() * 4);
        videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                      vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                      videoPlayer2.width(), videoPlayer2.height());
        videoSubsystemInitialized2 = videoTexture2.isReady();
        if (videoSubsystemInitialized2)
            videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
    }

    auto tVkDone = Clock::now();
    std::cout << "[Application] Vulkan resources: "
              << std::chrono::duration<double>(tVkDone - tVk).count() << "s\n";

    auto t1 = Clock::now();
    std::cout << "[Application] initVideo() done — total="
              << std::chrono::duration<double>(t1 - t0).count()
              << "s v1=" << videoSubsystemInitialized
              << " v2=" << videoSubsystemInitialized2 << '\n';
}

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem init
// ─────────────────────────────────────────────────────────────────────────────

void Application::initUI() {
    if (!uiSystem.initialize(window.getUiWindow(), window.getUiRenderer()))
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
    if (!videoSubsystemInitialized) {
        std::cout << "[Application] Skipping MultiPassPipeline — video not initialized\n";
        return;
    }

    const auto  ext         = vulkanContext.getSwapchainExtent();
    const bool  has2        = videoTexture2.isReady();
    uint32_t    queueFamily = 0; // TODO: obtener de VulkanContext

    if (!multiPassPipeline.initialize(
            vulkanContext.getPhysicalDevice(), vulkanContext.getDevice(),
            vulkanContext.getGraphicsQueue(), queueFamily,
            ext, vulkanContext.getSwapchainImageFormat(),
            videoTexture.getSampler(), videoTexture.getSampler(),
            videoTexture.getImageView(), videoTexture.getImageView(),
            has2 ? videoTexture2.getSampler()   : videoTexture.getSampler(),
            has2 ? videoTexture2.getImageView() : videoTexture.getImageView(),
            uniformBufferManager.getBuffers(), UniformBufferManager::getBufferSize()))
    {
        std::cerr << "[Application] Failed to initialize MultiPassPipeline\n";
        return;
    }

    // Sincronizar con vistas correctas (prev ≠ current)
    multiPassPipeline.updateDescriptorSets(
        uniformBufferManager.getBuffers(),
        videoTexture.getImageView(), videoTexture.getPrevImageView(),
        videoTexture.getSampler(),   videoTexture.getSampler(),
        has2 ? videoTexture2.getImageView() : videoTexture.getImageView(),
        has2 ? videoTexture2.getSampler()   : videoTexture.getSampler()
    );

    std::cout << "[Application] MultiPassPipeline initialized\n";
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

// ─────────────────────────────────────────────────────────────────────────────
// Render job completion
// ─────────────────────────────────────────────────────────────────────────────

void Application::handleCompletedRenderJob(const std::shared_ptr<RenderJob>& job) {
    if (!job || !renderWorker) return;

    const int slot = (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) ? 0 : 1;
    if (slot == 0) {
        videoRenderer.reset();
        videoPlayer.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(0, videoSourcePath);
        std::cout << "[Render] Auto-reloaded video 1: " << videoSourcePath << '\n';
    } else {
        videoRenderer2.reset();
        videoPlayer2.shutdown();
        renderWorker->perform_atomic_swap(job);
        reloadVideoSlot(1, videoSourcePath2);
        std::cout << "[Render] Auto-reloaded video 2: " << videoSourcePath2 << '\n';
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Video control helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Application::canChangeVideo() const {
    return !isReloadingVideo && !isReloadingVideo2 && !transitionActive;
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

    } else if (action == "randomizePreviewVideo1") {
        uiSystem.forcePreviewShuffle(0);

    } else if (action == "randomizePreviewVideo2") {
        uiSystem.forcePreviewShuffle(1);

    } else if (action == "jumpRandom") {
        if (!videoSubsystemInitialized) return;
        const double dur = videoPlayer.durationSeconds();
        if (dur > 0) videoPlayer.seekSeconds(std::uniform_real_distribution<double>(0.0, dur)(rng));

    } else if (action == "folderChanged") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.playback.selectedVideoFolder);
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
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
    vkDeviceWaitIdle(vulkanContext.getDevice());
    frameSystem.cleanup();

    for (auto fb : swapchainFramebuffers)
        vkDestroyFramebuffer(vulkanContext.getDevice(), fb, nullptr);
    swapchainFramebuffers.clear();

    // Destroy slot textures
    if (videoSubsystemInitialized) {
        videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture.cleanup(resourceSystem);
    }
    if (videoSubsystemInitialized2) {
        videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture2.cleanup(resourceSystem);
    }

    vulkanContext.recreateSwapchain(width, height);

    // Recreate slot textures
    if (videoSubsystemInitialized) {
        videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                    vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                    videoPlayer.width(), videoPlayer.height());
        if (videoRenderer) videoRenderer->reset();
    }
    if (videoSubsystemInitialized2) {
        videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                     vulkanContext.getCommandPool(), vulkanContext.getGraphicsQueue(),
                                     videoPlayer2.width(), videoPlayer2.height());
        if (videoRenderer2) videoRenderer2->reset();
    }

    updateAllDescriptorSets();
    initFramebuffers();

    frameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT,
                     vulkanContext.getSwapchainImageCount());
    frameSystem.resetCurrentFrame();
    frameSystem.waitForAllFences();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────

void Application::mainLoop() {
    while (running) {
        // ── Eventos SDL ─────────────────────────────────────────────────────
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            uiSystem.processEvent(event);

            if (event.type == SDL_QUIT) { running = false; break; }

            if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                switch (event.key.keysym.sym) {
                    case SDLK_1: case SDLK_KP_1: handleOscTrigger("randomizePreviewVideo1");  break;
                    case SDLK_2: case SDLK_KP_2: handleOscTrigger("randomizePreviewVideo2"); break;
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
                }
            }

            if (event.type == SDL_WINDOWEVENT) {
                SDL_Window* src = SDL_GetWindowFromID(event.window.windowID);
                const auto  ev  = event.window.event;

                if (ev == SDL_WINDOWEVENT_CLOSE) { running = false; break; }

                if (src == window.getMainWindow() && initializationComplete &&
                    (ev == SDL_WINDOWEVENT_RESIZED || ev == SDL_WINDOWEVENT_SIZE_CHANGED))
                {
                    window.resetResizeFlag();
                    uint32_t w, h;
                    window.getDrawableSize(w, h);
                    pendingResizeW = w;
                    pendingResizeH = h;
                    resizePending = true;
                    resizeDebounceTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
                }
            }
        }
        if (!running) break;

        // ── Process debounced resize ──────────────────────────────────────────
        if (resizePending) {
            if (std::chrono::steady_clock::now() >= resizeDebounceTime) {
                resizePending = false;
                handleWindowResize(pendingResizeW, pendingResizeH);
            }
        }

        // ── Begin frame ─────────────────────────────────────────────────────
        uint32_t    imageIndex;
        VkResult    result;
        FrameContext* frame = frameSystem.beginFrame(vulkanContext.getSwapchain(), imageIndex, result);

        if (!frame) {
            if (result == VK_ERROR_OUT_OF_DATE_KHR) continue;
            throw std::runtime_error("failed to acquire swapchain image");
        }

        // ── Delta time ──────────────────────────────────────────────────────
        const auto now       = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastFrameTimestamp).count();
        lastFrameTimestamp   = now;

        // ── FPS counter + window titles ─────────────────────────────────────
        frameCount++;
        fpsAccumTime += deltaTime;
        if (fpsAccumTime >= 0.5f) {
            currentFps = frameCount / fpsAccumTime;
            frameCount = 0;
            fpsAccumTime = 0.0f;
            char title[128];
            snprintf(title, sizeof(title), "Vulkan Renderer — %.1f FPS", currentFps);
            SDL_SetWindowTitle(window.getMainWindow(), title);
            snprintf(title, sizeof(title), "Controls — %.1f FPS", currentFps);
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

        transitionActive = false;

        // ── Auto-randomize colors ────────────────────────────────────────────
        // Quick energy read for color palette (linear RMS -> 0..1)
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
        updateUniformBuffer(frame->frameIndex);

        if (videoSubsystemInitialized)  videoPlayer.setPlaybackRate(visualControls.playback.videoPlaybackRate);
        if (videoSubsystemInitialized2) videoPlayer2.setPlaybackRate(visualControls.playback.video2PlaybackRate);

        // Detect playback speed changes and cache them
        if (visualControls.playback.videoPlaybackRate != lastPlaybackRate) {
            lastPlaybackRate = visualControls.playback.videoPlaybackRate;
            videoSpeedCache[videoSourcePath] = lastPlaybackRate;
        }
        if (visualControls.playback.video2PlaybackRate != lastPlaybackRate2) {
            lastPlaybackRate2 = visualControls.playback.video2PlaybackRate;
            videoSpeedCache[videoSourcePath2] = lastPlaybackRate2;
        }

        if (videoRenderer)  videoRenderer->update(deltaTime, frame->frameIndex);
        if (videoRenderer2) videoRenderer2->update(deltaTime, frame->frameIndex);

        // ── UI ───────────────────────────────────────────────────────────────
        UIDiagnostics diag;
        diag.lastFrameFrameIndex       = frame->frameIndex;
        diag.lastFrameImageIndex       = frame->swapchainImageIndex;
        diag.swapchainWidth            = vulkanContext.getSwapchainExtent().width;
        diag.swapchainHeight           = vulkanContext.getSwapchainExtent().height;
        diag.currentMode               = visualControls.playback.activeMode;
        diag.videoReady                = videoSubsystemInitialized && videoTexture.isReady();
        diag.videoWidth                = videoTexture.getWidth();
        diag.videoHeight               = videoTexture.getHeight();
        diag.animationTime             = debugAnimationTime;
        diag.animationDelta            = debugAnimationDelta;
        diag.animationElapsedSeconds   = debugAnimationElapsedSeconds;

        UICallbacks callbacks = buildUICallbacks();
        uiSystem.render(visualControls, videoRandomizer, videoRandomizer2,
                        videoPlayer, videoPlayer2, videoRegistry,
                        selectedVideoAsset, selectedVideoAsset2,
                        transitionDuration, transitionDuration2,
                        allowDimensionChangeRecreation, controlsDirty,
                        rng, diag, callbacks, midiSystem, oscSystem, audioSystem,
                        videoSourcePath, videoSourcePath2);

        // ── Command buffer ───────────────────────────────────────────────────
        recordCommandBuffer(commandBuffers[frame->frameIndex], *frame);

        // ── Submit ───────────────────────────────────────────────────────────
        auto renderFinished = frameSystem.getRenderFinishedSemaphore(frame->swapchainImageIndex)
                                         .value_or(VK_NULL_HANDLE);
        if (renderFinished == VK_NULL_HANDLE)
            throw std::runtime_error("invalid renderFinished semaphore");

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &frame->imageAvailableSemaphore;
        submit.pWaitDstStageMask    = &waitStage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &commandBuffers[frame->frameIndex];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &renderFinished;

        if (vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &submit, frame->inFlightFence) != VK_SUCCESS)
            throw std::runtime_error("failed to submit draw command buffer");

        // ── Present ──────────────────────────────────────────────────────────
        VkSwapchainKHR sc = vulkanContext.getSwapchain();
        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &renderFinished;
        present.swapchainCount     = 1;
        present.pSwapchains        = &sc;
        present.pImageIndices      = &frame->swapchainImageIndex;

        const VkResult pr = vkQueuePresentKHR(vulkanContext.getPresentQueue(), &present);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
            window.resetResizeFlag();

        frameSystem.endFrame();
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
        }
        isReloadingVideo = false;
    };

    tick(videoRandomizer.autoRandomize,  videoSubsystemInitialized,
         videoRandomizer,  0, visualControls.playback.selectedVideoFolder);

    tick(videoRandomizer2.autoRandomize && visualControls.playback.enableDualVideo,
         videoSubsystemInitialized,
         videoRandomizer2, 1, visualControls.playback.selectedVideo2Folder);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI callbacks — centralizado
// ─────────────────────────────────────────────────────────────────────────────

UICallbacks Application::buildUICallbacks() {
    UICallbacks cb;

    cb.onControlsChanged = [this]() { controlsDirty = true; };

    cb.onApplyChanges = [this]() {
        if (!canChangeVideo()) return;
        const int slot = (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) ? 0 : 1;
        reloadVideoSlot(slot, (slot == 0) ? videoSourcePath : videoSourcePath2);
    };

    cb.onFolderChanged = [this]() {
        // Preview folder changes do NOT reload the renderer.
        // Only explicit "Enviar a escena" / Enter updates the renderer.
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
    };

    cb.onFolderChanged2 = [this]() {
        videoRandomizer.shuffleQueue.clear();  videoRandomizer.currentShuffleIndex  = 0;
        videoRandomizer2.shuffleQueue.clear(); videoRandomizer2.currentShuffleIndex = 0;
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

    cb.onRandomizePreviewVideo1 = [this]() {
        uiSystem.forcePreviewShuffle(0);
    };

    cb.onRandomizePreviewVideo2 = [this]() {
        uiSystem.forcePreviewShuffle(1);
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

    cb.onGetVideoSpeed = [this](const std::string& path) -> float {
        auto it = videoSpeedCache.find(path);
        return (it != videoSpeedCache.end()) ? it->second : 1.0f;
    };
    cb.onSetVideoSpeed = [this](const std::string& path, float speed) {
        videoSpeedCache[path] = speed;
    };

    cb.onListPresets = [this]() { return listPresets(); };
    cb.onSavePreset = [this](const std::string& name) { return savePreset(name); };
    cb.onLoadPreset = [this](const std::string& name) { return loadPreset(name); };
    cb.onDeletePreset = [this](const std::string& name) { return deletePreset(name); };

    return cb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform buffer
// ─────────────────────────────────────────────────────────────────────────────

void Application::updateUniformBuffer(uint32_t frameIndex) {
    GlobalParamsUBO ubo{};

    // Calculate time delta and accumulate (similar to your system)
    auto currentTime = std::chrono::high_resolution_clock::now();
    if (!animationTimeInitialized) {
        lastGlobalTime = currentTime;
        accumulatedTime = 0.0f;
        animationTimeInitialized = true;
    }

    float globalDeltaTime = std::chrono::duration<float>(currentTime - lastGlobalTime).count();
    lastGlobalTime = currentTime;

    // Apply animation speed to delta
    float speed = std::max(0.01f, visualControls.playback.animationSpeed);
    accumulatedTime += globalDeltaTime * speed;

    debugAnimationElapsedSeconds = accumulatedTime;
    debugAnimationDelta = globalDeltaTime;
    float time = accumulatedTime;

    constexpr float kTwoPi = 6.28318530718f;
    if (visualControls.playback.enableTempoLfo) {
        float lfoSpeed = std::max(0.01f, visualControls.playback.tempoLfoSpeed);
        float phaseAdvance = globalDeltaTime * lfoSpeed * kTwoPi;
        visualControls.playback.tempoLfoPhase = std::fmod(visualControls.playback.tempoLfoPhase + phaseAdvance, kTwoPi);
        if (visualControls.playback.tempoLfoPhase < 0.0f) visualControls.playback.tempoLfoPhase += kTwoPi;
    }

    // Update audio values from AudioSystem (normalized + smoothed)
    auto normalizeAudioLevel = [](float rawValue, float gain, float gamma) {
        float scaled = std::clamp(rawValue * gain, 0.0f, 1.0f);
        return (gamma != 1.0f) ? std::pow(scaled, gamma) : scaled;
    };

    float inputGain = visualControls.audio.inputGain;
    float liveEnergy = normalizeAudioLevel(audioSystem.getRMS() * inputGain,           1.0f, 0.85f);
    float liveBass   = normalizeAudioLevel(audioSystem.getSmoothedBass() * inputGain,  2.0f, 1.10f);
    float liveMid    = normalizeAudioLevel(audioSystem.getSmoothedMid() * inputGain,   2.0f, 1.00f);
    float liveHigh   = normalizeAudioLevel(audioSystem.getSmoothedHigh() * inputGain,   2.0f, 1.00f);

    // Per-band EQ gains
    liveBass = std::clamp(liveBass * visualControls.audio.bassGain, 0.0f, 1.0f);
    liveMid  = std::clamp(liveMid  * visualControls.audio.midGain,  0.0f, 1.0f);
    liveHigh = std::clamp(liveHigh * visualControls.audio.highGain, 0.0f, 1.0f);

    float tempoValue;
    if (visualControls.system.enableAudioReactive) {
        // Auto-tempo driven by audio energy: 0x (silence) to 5x (loud)
        // Power curve makes it harder to reach 5 (needs very high energy)
        float drive = visualControls.audio.reactiveDrive;
        float curvedEnergy = std::pow(liveEnergy, 1.5f);
        tempoValue = curvedEnergy * 5.0f * drive;
        tempoValue = std::clamp(tempoValue, 0.0f, 5.0f);
        // Sync playback.tempo so the UI slider moves too
        visualControls.playback.tempo = tempoValue;
        // Note: video playback rate is NOT synced to tempo — videos keep their manual speed
    } else {
        tempoValue = visualControls.playback.tempo;
        if (visualControls.playback.enableTempoLfo) {
            float lfoValue = std::sin(visualControls.playback.tempoLfoPhase);
            tempoValue += visualControls.playback.tempoLfoDepth * lfoValue;
        }
        tempoValue = std::clamp(tempoValue, 0.05f, 8.0f);
    }

    // Shader time: accumulates at tempo speed so procedural layers
    // (Layer 0, Layer 1, Anaglyph) animate with audio energy
    static float shaderTime = 0.0f;
    if (visualControls.system.enableAudioReactive) {
        shaderTime += globalDeltaTime * tempoValue;
    } else {
        shaderTime += globalDeltaTime * std::max(0.01f, visualControls.playback.animationSpeed);
    }

    auto& reactive = visualControls.runtime.audioReactive;
    reactive.enabled = visualControls.system.enableAudioReactive;
    reactive.energy = liveEnergy;
    reactive.bass   = liveBass;
    reactive.mid    = liveMid;
    reactive.high   = liveHigh;

    if (visualControls.system.enableAudioReactive) {
        visualControls.audio.energy = liveEnergy;
        visualControls.audio.bass   = liveBass;
        visualControls.audio.mid    = liveMid;
        visualControls.audio.high   = liveHigh;
    }

    // Set basic UBO values
    ubo.model = glm::mat4(1.0f);
    ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0), glm::vec3(0.0, 1.0, 0.0));
    ubo.proj = glm::perspective(glm::radians(45.0f),
                                vulkanContext.getSwapchainExtent().width / static_cast<float>(vulkanContext.getSwapchainExtent().height),
                                0.1f, 10.0f);
    ubo.proj[1][1] *= -1.0f;
    ubo.resolution = glm::vec2(static_cast<float>(vulkanContext.getSwapchainExtent().width),
                               static_cast<float>(vulkanContext.getSwapchainExtent().height));
    ubo.videoResolution = glm::vec2(static_cast<float>(videoTexture.getWidth()),
                                    static_cast<float>(videoTexture.getHeight()));
    ubo.time = visualControls.system.enableAudioReactive ? shaderTime : time;
    ubo.mode = visualControls.playback.activeMode;
    ubo.videoMix = visualControls.playback.videoMix;
    ubo.videoAvailable = (videoSubsystemInitialized && videoTexture.isReady()) ? 1.0f : 0.0f;
    ubo.video2Mix = visualControls.playback.video2Mix;
    ubo.video2Available = (visualControls.playback.enableDualVideo && videoSubsystemInitialized2 && videoTexture2.isReady()) ? 1.0f : 0.0f;
    ubo.video2BlendMode = visualControls.playback.video2BlendMode;

    // Set visual control values
    ubo.primaryColor = visualControls.color.primaryColor;
    ubo.secondaryColor = visualControls.color.secondaryColor;
    ubo.colorBlend = visualControls.color.colorBlend;
    ubo.tempo = tempoValue;
    ubo.energy = visualControls.audio.energy;
    ubo.bass = visualControls.audio.bass;
    ubo.mid = visualControls.audio.mid;
    ubo.high = visualControls.audio.high;
    ubo.audioReactiveDrive = visualControls.audio.reactiveDrive;
    
    // Audio reactivity parameters
    ubo.audioWarpResponse = visualControls.audio.warpResponse;
    ubo.audioFeedbackResponse = visualControls.audio.feedbackResponse;
    ubo.audioBlurResponse = visualControls.audio.blurResponse;
    ubo.audioColorResponse = visualControls.audio.colorResponse;
    ubo.audioGlitchResponse = visualControls.audio.glitchResponse;
    ubo.audioBeatSync = visualControls.audio.beatSync;
    ubo.audioLfoRate = visualControls.audio.lfoRate;
    ubo.enableAudioReactive = visualControls.system.enableAudioReactive ? 1 : 0;
    
    // Post FX basicos
    ubo.grayscaleAmount = visualControls.playback.grayscaleAmount;
    ubo.sharpenAmount = visualControls.playback.sharpenAmount;
    ubo.upscaleEnabled = visualControls.playback.upscaleEnabled ? 1.0f : 0.0f;

    // Enable/Disable flags for post FX
    ubo.enablePostCrtCurvature = visualControls.post.enablePostCrtCurvature ? 1 : 0;
    ubo.enablePostScanMask = visualControls.post.enablePostScanMask ? 1 : 0;
    ubo.enablePostVignette = visualControls.post.enablePostVignette ? 1 : 0;
    ubo.enablePostFishEye = visualControls.post.enablePostFishEye ? 1 : 0;
    ubo.enablePostBloom = visualControls.post.enablePostBloom ? 1 : 0;
    ubo.enablePostAberration = visualControls.post.enablePostAberration ? 1 : 0;
    ubo.enablePostGrain = visualControls.post.enablePostGrain ? 1 : 0;
    ubo.enablePostBend = visualControls.post.enablePostBend ? 1 : 0;
    ubo.enablePostGlitch = visualControls.post.enablePostGlitch ? 1 : 0;
    ubo.enablePostColorBalance = visualControls.post.enablePostColorBalance ? 1 : 0;

    // Enable/Disable flags for VJAY BASICS
    ubo.enableColorGrading = visualControls.color.enableColorGrading ? 1 : 0;
    ubo.enableFeedback = visualControls.temporal.enableFeedback ? 1 : 0;
    ubo.enableDistortion = visualControls.fx.enableDistortion ? 1 : 0;
    ubo.enableBlurMotion = visualControls.fx.enableBlurMotion ? 1 : 0;
    ubo.enableSharpen = visualControls.fx.enableSharpen ? 1 : 0;
    ubo.enablePostGlitch = visualControls.fx.enableGlitch ? 1 : 0;
    ubo.enableBlending = visualControls.blending.enableBlending ? 1 : 0;
    ubo.enableAnalog = visualControls.post.enableAnalog ? 1 : 0;
    ubo.enableAudioReactive = visualControls.system.enableAudioReactive ? 1 : 0;
    ubo.enableTemporal = visualControls.temporal.enableTemporal ? 1 : 0;

    // Enable/Disable flags for VJAY EXTRA
    ubo.enablePixelate = visualControls.fx.enablePixelate ? 1 : 0;
    ubo.enableStrobe = visualControls.fx.enableStrobe ? 1 : 0;
    ubo.enableThreshold = visualControls.fx.enableThreshold ? 1 : 0;
    ubo.enableSlowZoom = visualControls.fx.enableSlowZoom ? 1 : 0;
    ubo.enableMirror = visualControls.fx.enableMirror ? 1 : 0;
    ubo.enableInvert = visualControls.fx.enableInvert ? 1 : 0;
    ubo.enablePosterize = visualControls.fx.enablePosterize ? 1 : 0;
    ubo.enableInfrared = visualControls.fx.enableInfrared ? 1 : 0;
    ubo.enableZoomPulse = visualControls.fx.enableZoomPulse ? 1 : 0;
    ubo.enableRGBShift = visualControls.fx.enableRGBShift ? 1 : 0;

    // CRT
    ubo.crtCurvature = visualControls.post.crtCurvature;
    ubo.crtHorizontalCurvature = visualControls.post.crtHorizontalCurvature;
    ubo.crtScanlineIntensity = visualControls.post.crtScanlineIntensity;
    ubo.crtMaskIntensity = visualControls.post.crtMaskIntensity;
    ubo.crtVignette = visualControls.post.crtVignette;
    ubo.crtFishEye = visualControls.post.crtFishEye;
    
    // Bloom
    ubo.bloomIntensity = visualControls.post.bloomIntensity;
    ubo.bloomThreshold = visualControls.post.bloomThreshold;
    
    // Aberracion / grano / bend / glitch
    ubo.aberrationAmount = visualControls.post.aberrationAmount;
    ubo.grainStrength = visualControls.post.grainStrength;
    ubo.bendAmount = visualControls.fx.bendAmount;
    ubo.glitchAmount = visualControls.fx.glitchAmount;
    
    // Color grading
    ubo.colorBalance = visualControls.color.colorBalance;
    ubo.gradeBrightness = visualControls.color.gradeBrightness;
    ubo.gradeContrast = visualControls.color.gradeContrast;
    ubo.gradeSaturation = visualControls.color.gradeSaturation;
    ubo.gradeHueShift = visualControls.color.gradeHueShift;
    ubo.gradeGamma = visualControls.color.gradeGamma;
    ubo.colorLUTIndex = visualControls.color.colorLUTIndex;
    ubo.splitToneBalance = visualControls.color.splitToneBalance;
    ubo.splitToneShadows = visualControls.color.splitToneShadows;
    ubo.splitToneHighlights = visualControls.color.splitToneHighlights;
    
    // Feedback temporal
    ubo.feedbackAmount = visualControls.temporal.feedbackAmount;
    ubo.trailStrength = visualControls.temporal.trailStrength;
    ubo.temporalAccumulation = visualControls.temporal.temporalAccumulation;
    ubo.feedbackDecay = visualControls.temporal.feedbackDecay;
    ubo.recursiveBlend = visualControls.temporal.recursiveBlend;
    
    // Distorsion espacial
    ubo.uvWarpStrength = visualControls.fx.uvWarpStrength;
    ubo.rippleStrength = visualControls.fx.rippleStrength;
    ubo.rippleFrequency = visualControls.fx.rippleFrequency;
    ubo.swirlStrength = visualControls.fx.swirlStrength;
    ubo.displacementAmount = visualControls.fx.displacementAmount;
    ubo.kaleidoSegments = visualControls.fx.kaleidoSegments;
    ubo.tunnelDepth = visualControls.fx.tunnelDepth;
    ubo.tunnelCurvature = visualControls.fx.tunnelCurvature;
    
    // Blur y motion
    ubo.gaussianBlur = visualControls.fx.gaussianBlur;
    ubo.directionalBlur = visualControls.fx.directionalBlur;
    ubo.directionalBlurAngle = visualControls.fx.directionalBlurAngle;
    ubo.zoomBlur = visualControls.fx.zoomBlur;
    ubo.motionBlur = visualControls.fx.motionBlur;
    ubo.temporalBlur = visualControls.fx.temporalBlur;
    
    // Sharpening
    ubo.unsharpMask = visualControls.fx.unsharpMask;
    ubo.casAmount = visualControls.fx.casAmount;
    ubo.localContrast = visualControls.fx.localContrast;
    
    // Glitch detallado
    ubo.glitchDatamosh = visualControls.fx.glitchDatamosh;
    ubo.glitchRGBSplit = visualControls.fx.glitchRGBSplit;
    ubo.glitchScanlineBreak = visualControls.fx.glitchScanlineBreak;
    ubo.glitchJitter = visualControls.fx.glitchJitter;
    ubo.glitchTearing = visualControls.fx.glitchTearing;
    ubo.glitchPixelSort = visualControls.fx.glitchPixelSort;
    ubo.glitchBufferCorruption = visualControls.fx.glitchBufferCorruption;
    
    // Blending / compositing
    ubo.blendModeProcedural = visualControls.blending.blendModeProcedural;
    ubo.blendModeVideo = visualControls.blending.blendModeVideo;
    ubo.blendModeFeedback = visualControls.blending.blendModeFeedback;
    ubo.blendProceduralMix = visualControls.blending.blendProceduralMix;
    ubo.blendVideoMix = visualControls.blending.blendVideoMix;
    ubo.blendFeedbackMix = visualControls.blending.blendFeedbackMix;
    
    // Analog / CRT avanzado
    ubo.analogScanlineFocus = visualControls.post.analogScanlineFocus;
    ubo.analogMaskBalance = visualControls.post.analogMaskBalance;
    
    // Temporal
    ubo.frameAccumulation = visualControls.playback.frameAccumulation;
    ubo.slowMotionFactor = visualControls.playback.slowMotionFactor;
    ubo.temporalInterpolation = visualControls.playback.temporalInterpolation;
    
    // Efectos extra (VJAY EXTRA)
    ubo.pixelateAmount = visualControls.fx.pixelateAmount;
    ubo.strobeSpeed = visualControls.fx.strobeSpeed;
    ubo.thresholdLevel = visualControls.fx.thresholdLevel;
    ubo.slowZoomAmount = visualControls.fx.slowZoomAmount;
    ubo.enableEdgeDetect = visualControls.fx.enableEdgeDetect ? 1 : 0;
    ubo.edgeStrength = visualControls.fx.edgeStrength;
    ubo.edgeThreshold = visualControls.fx.edgeThreshold;
    ubo.edgeBlend = visualControls.fx.edgeBlend;
    ubo.edgeColor = visualControls.fx.edgeColor;
    ubo.mirrorAmount = visualControls.fx.mirrorAmount;
    ubo.posterizeLevels = visualControls.fx.posterizeLevels;
    ubo.zoomPulseAmount = visualControls.fx.zoomPulseAmount;
    ubo.rgbShiftAmount = visualControls.fx.rgbShiftAmount;
    
    // FXAA
    ubo.enableFXAA = visualControls.system.enableFXAA ? 1 : 0;
    ubo.fxaaQualitySubpix = visualControls.system.fxaaQualitySubpix;
    ubo.fxaaQualityEdgeThreshold = visualControls.system.fxaaQualityEdgeThreshold;
    ubo.fxaaQualityEdgeThresholdMin = visualControls.system.fxaaQualityEdgeThresholdMin;

    // Grid / Mirroring
    ubo.enableGrid = visualControls.grid.enabled ? 1 : 0;
    ubo.gridMode = visualControls.grid.mode;
    ubo.gridCount = visualControls.grid.count;
    ubo.gridRows = visualControls.grid.rows;
    ubo.gridColumns = visualControls.grid.columns;
    ubo.gridMirrorCells = visualControls.grid.mirrorCells ? 1 : 0;
    ubo.gridShowLines = visualControls.grid.showLines ? 1 : 0;
    ubo.gridLineWidth = visualControls.grid.lineWidth;
    ubo.gridLineIntensity = visualControls.grid.lineIntensity;
    ubo.gridLineColor = visualControls.grid.lineColor;

    // Camera movement
    ubo.cameraZoom = visualControls.camera.zoom;
    ubo.cameraPanX = visualControls.camera.panX;
    ubo.cameraPanY = visualControls.camera.panY;
    ubo.cameraRotation = visualControls.camera.rotation;
    ubo.enableCameraMovement = visualControls.camera.enableMovement ? 1 : 0;

    // Final RGB overlay
    ubo.rgbOverlay = visualControls.color.rgbOverlay;
    ubo.enableRgbOverlay = visualControls.color.enableRgbOverlay ? 1 : 0;

    // ------------------------------------------------------------------
    // AUDIO REACTIVITY AUTO-MODULATION
    // When enabled, audio levels automatically drive effect intensities
    // so the layers animate without manual slider tweaking.
    // ------------------------------------------------------------------
    float envClamped  = std::clamp(visualControls.audio.energy, 0.0f, 1.0f);
    float bassClamped = std::clamp(visualControls.audio.bass,   0.0f, 1.0f);
    float midClamped  = std::clamp(visualControls.audio.mid,    0.0f, 1.0f);
    float highClamped = std::clamp(visualControls.audio.high,   0.0f, 1.0f);

    float warpGain     = std::max(0.0f, visualControls.audio.warpResponse);
    float feedbackGain = std::max(0.0f, visualControls.audio.feedbackResponse);
    float blurGain     = std::max(0.0f, visualControls.audio.blurResponse);
    float colorGain    = std::max(0.0f, visualControls.audio.colorResponse);
    float glitchGain   = std::max(0.0f, visualControls.audio.glitchResponse);

    if (visualControls.system.enableAudioReactive) {
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
        if (visualControls.camera.enableMovement) {
            // Very subtle zoom pulse driven by bass (max 6% closer)
            float zoomPulse = 1.0f + (bassClamped * 0.06f + envClamped * 0.02f) * warpGain;
            ubo.cameraZoom = std::max(ubo.cameraZoom, zoomPulse);

            // Slow orbit rotation driven by energy
            ubo.cameraRotation += envClamped * 0.3f * globalDeltaTime * warpGain;

            // Gentle pan driven by mid/high (Lissajous-like motion)
            ubo.cameraPanX = std::max(ubo.cameraPanX, midClamped  * 0.05f * std::sin(ubo.time * 1.5f) * warpGain);
            ubo.cameraPanY = std::max(ubo.cameraPanY, highClamped * 0.04f * std::cos(ubo.time * 1.2f) * warpGain);
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

    uniformBufferManager.update(frameIndex, ubo, vulkanContext.getDevice());
}

// ─────────────────────────────────────────────────────────────────────────────
// Record command buffer
// ─────────────────────────────────────────────────────────────────────────────

void Application::recordCommandBuffer(VkCommandBuffer cmd, FrameContext& frame) {
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
        throw std::runtime_error("failed to begin command buffer");

    videoTexture.recordPendingUpload(cmd, frame.frameIndex, vulkanContext.getGraphicsQueue());
    if (videoSubsystemInitialized2 && videoTexture2.isReady())
        videoTexture2.recordPendingUpload(cmd, frame.frameIndex, vulkanContext.getGraphicsQueue());

    if (videoSubsystemInitialized) {
        multiPassPipeline.execute(
            cmd, frame.frameIndex,
            descriptorSetManager.getSet(frame.frameIndex),
            renderPass, swapchainFramebuffers, frame.swapchainImageIndex,
            fullscreenPipeline, pipelineLayout,
            fullscreenDescriptorSets[frame.frameIndex],
            vulkanContext.getSwapchainExtent(), swapchainSampler
        );
    } else {
        // Fallback negro cuando no hay vídeo
        VkClearValue clear{ .color = {0.f, 0.f, 0.f, 1.f} };
        const auto ext = vulkanContext.getSwapchainExtent();
        VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rp.renderPass        = renderPass;
        rp.framebuffer       = swapchainFramebuffers[frame.swapchainImageIndex];
        rp.renderArea.extent = ext;
        rp.clearValueCount   = 1;
        rp.pClearValues      = &clear;

        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fullscreenPipeline);
        VkDescriptorSet ds = fullscreenDescriptorSets[frame.frameIndex];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);

        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0, 1 };
        VkRect2D   sc{ {0,0}, ext };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to end command buffer");
}

// ─────────────────────────────────────────────────────────────────────────────
// Cleanup
// ─────────────────────────────────────────────────────────────────────────────

void Application::cleanup() {
    if (!initializationComplete) return;
    vkDeviceWaitIdle(vulkanContext.getDevice());

    saveState();
    uiSystem.shutdown();
    frameSystem.cleanup();

    videoPlayer.shutdown();
    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture.cleanup(resourceSystem);
    videoPlayer2.shutdown();
    videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture2.cleanup(resourceSystem);

    uniformBufferManager.destroy(resourceSystem, vulkanContext.getDevice());
    descriptorSetManager.destroy(vulkanContext.getDevice());
    multiPassPipeline.cleanup();

    // Shutdown audio/MIDI/OSC systems before ResourceSystem cleanup
    audioSystem.shutdown();
    midiSystem.shutdown();
    oscSystem.shutdown();

    if (fullscreenPipeline          != VK_NULL_HANDLE) vkDestroyPipeline           (vulkanContext.getDevice(), fullscreenPipeline,          nullptr);
    if (pipelineLayout              != VK_NULL_HANDLE) vkDestroyPipelineLayout      (vulkanContext.getDevice(), pipelineLayout,              nullptr);
    if (fullscreenDescriptorPool    != VK_NULL_HANDLE) vkDestroyDescriptorPool      (vulkanContext.getDevice(), fullscreenDescriptorPool,    nullptr);
    if (fullscreenDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vulkanContext.getDevice(), fullscreenDescriptorSetLayout, nullptr);
    if (swapchainSampler            != VK_NULL_HANDLE) vkDestroySampler             (vulkanContext.getDevice(), swapchainSampler,            nullptr);
    if (renderPass                  != VK_NULL_HANDLE) vkDestroyRenderPass          (vulkanContext.getDevice(), renderPass,                  nullptr);

    for (auto fb : swapchainFramebuffers)
        vkDestroyFramebuffer(vulkanContext.getDevice(), fb, nullptr);

    if (vertexBufferHandle.type != ResourceType::Unknown)
        resourceSystem.destroy(vertexBufferHandle);

    resourceSystem.cleanup();
}