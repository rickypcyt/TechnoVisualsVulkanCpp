#include "Application.h"
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <random>

Application::Application() = default;

Application::~Application() {
    cleanup();
}

void Application::run() {
    // Initialize SDL
    window.initSDL();
    window.createMainWindow("Vulkan", WIDTH, HEIGHT);
    window.createUiWindow("Controls", 420, 420);

    // Initialize Vulkan
    initVulkan();
    
    // Initialize swapchain and rendering
    initSwapchain();
    initRenderPass();
    
    // Initialize descriptor sets (must be before pipelines)
    descriptorSetManager.createLayout(vulkanContext.getDevice());
    descriptorSetManager.createPool(vulkanContext.getDevice());
    
    // Initialize buffers and resources
    uniformBufferManager.createBuffers(resourceSystem, vulkanContext.getDevice());

    // Load control state before video init so selectedVideoFolder is respected
    ControlState::load(controlStatePath, visualControls, videoRandomizer, videoRandomizer2,
                       allowDimensionChangeRecreation, midiSystem, oscSystem,
                       selectedVideoAsset, selectedVideoAsset2);

    // Initialize video
    initVideo();

    // Initialize descriptor sets (after video texture is ready)
    descriptorSetManager.createSets(vulkanContext.getDevice());

    // Update descriptor sets with video texture and uniform buffers
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        descriptorSetManager.updateSet(vulkanContext.getDevice(), i,
                                       uniformBufferManager.getBuffers()[i],
                                       const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
                                       const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo()));
    }
    
    // Initialize pipelines (after descriptor sets are ready)
    initPipelines();
    initFramebuffers();
    
    // Initialize UI
    initUI();

    // Initialize NLE
    initNLE();

    // Initialize multi-pass pipeline (after video is ready)
    initMultiPassPipeline();

    // Initialize MIDI
    initMidi();

    // Initialize OSC
    initOsc();

    // Initialize Audio
    initAudio();

    // Initialize command buffers
    initCommandBuffers();

    // Initialize frame system
    frameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT, vulkanContext.getSwapchainImageCount());
    std::cout << "[Application] FrameSystem initialized successfully" << std::endl;
    
    // Set start time
    startTime = std::chrono::steady_clock::now();
    lastControlSaveTime = startTime;
    lastFrameTimestamp = startTime;
    lastRandomJumpTime = startTime;
    initializationComplete = true;

    // Run main loop
    mainLoop();
}

void Application::initVulkan() {
#ifdef NDEBUG
    bool enableValidation = false;
#else
    bool enableValidation = true;
#endif
    
    vulkanContext.init(window.getMainWindow(), enableValidation);
    vulkanContext.createSurface(window.getMainWindow());
    vulkanContext.createCommandPool();
    
    resourceSystem.init(vulkanContext.getDevice(), vulkanContext.getPhysicalDevice());
}

void Application::initSwapchain() {
    uint32_t width, height;
    window.getDrawableSize(width, height);
    vulkanContext.createSwapchain(width, height);
}

void Application::initRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = vulkanContext.getSwapchainImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(vulkanContext.getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass");
    }
}

void Application::initPipelines() {
    // Create pipeline layout
    VkDescriptorSetLayout layout = descriptorSetManager.getLayout();
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &layout;

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

void Application::initFramebuffers() {
    const auto& swapchainImageViews = vulkanContext.getSwapchainImageViews();
    swapchainFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = vulkanContext.getSwapchainExtent().width;
        framebufferInfo.height = vulkanContext.getSwapchainExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(vulkanContext.getDevice(), &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void Application::initCommandBuffers() {
    // Allocate command buffers for each frame
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkanContext.getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(vulkanContext.getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers");
    }
    
    // Assign command buffers to FrameSystem's frame contexts
    for (size_t i = 0; i < commandBuffers.size(); ++i) {
        // FrameSystem will manage these internally
    }
}

void Application::initVideo() {
    std::cout << "[Application] initVideo() called" << std::endl;
    videoRegistry.scan(videoAssetsRoot, visualControls.selectedVideoFolder);
    const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideoFolder);
    if (!assets.empty()) {
        if (selectedVideoAsset < 0 || selectedVideoAsset >= static_cast<int>(assets.size())) {
            selectedVideoAsset = 0;
        }
        videoSourcePath = assets[selectedVideoAsset].metadata.path;
    }
    
    // Initialize video player
    int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
    int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
    
    if (!videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
        std::cerr << "[Application] Failed to initialize video player" << std::endl;
        return;
    }
    videoPlayer.setAutoScale(visualControls.autoScaleVideo);
    
    // Initialize CPU frame pool with initial video resolution
    cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()), 
                       static_cast<uint32_t>(videoPlayer.height()),
                       static_cast<uint32_t>(videoPlayer.width()) * 4);
    
    // Create video texture resources
    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                 vulkanContext.getCommandPool(),
                                 vulkanContext.getGraphicsQueue(),
                                 static_cast<uint32_t>(videoPlayer.width()),
                                 static_cast<uint32_t>(videoPlayer.height()));

    videoSubsystemInitialized = videoTexture.isReady();

    // Initialize video renderer with CPU frame pool
    if (videoSubsystemInitialized) {
        videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);
    }

    // Initialize video 2 (dual source)
    const auto& assets2 = videoRegistry.getFilteredAssets(visualControls.selectedVideo2Folder);
    if (!assets2.empty()) {
        if (selectedVideoAsset2 < 0 || selectedVideoAsset2 >= static_cast<int>(assets2.size())) {
            selectedVideoAsset2 = 0;
        }
        videoSourcePath2 = assets2[selectedVideoAsset2].metadata.path;
    } else if (!assets.empty()) {
        // Fallback: use same folder as video 1 but different asset if possible
        selectedVideoAsset2 = (assets.size() > 1) ? 1 : 0;
        videoSourcePath2 = assets[selectedVideoAsset2].metadata.path;
    }

    std::cout << "[Application] About to initialize video player 2 with: " << videoSourcePath2 << std::endl;
    if (!videoPlayer2.initialize(videoSourcePath2, screenW, screenH)) {
        std::cerr << "[Application] ERROR: Failed to initialize video player 2 with: " << videoSourcePath2 << std::endl;
        return;
    }
    std::cout << "[Application] Video player 2 initialized: " << videoSourcePath2 << ", isReady=" << videoPlayer2.isReady() << std::endl;
    videoPlayer2.setAutoScale(visualControls.autoScaleVideo);

    cpuFramePool2.resize(static_cast<uint32_t>(videoPlayer2.width()),
                         static_cast<uint32_t>(videoPlayer2.height()),
                         static_cast<uint32_t>(videoPlayer2.width()) * 4);

    videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                  vulkanContext.getCommandPool(),
                                  vulkanContext.getGraphicsQueue(),
                                  static_cast<uint32_t>(videoPlayer2.width()),
                                  static_cast<uint32_t>(videoPlayer2.height()));

    videoSubsystemInitialized2 = videoTexture2.isReady();

    if (videoSubsystemInitialized2) {
        videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
    }
}

void Application::initUI() {
    if (!uiSystem.initialize(window.getUiWindow(), window.getUiRenderer())) {
        throw std::runtime_error("failed to initialize UI system");
    }
}

void Application::initNLE() {
    // Initialize NLE components
    g_project_state.active_file = videoSourcePath;

    renderWorker = std::make_unique<RenderWorker>();
    renderWorker->on_render_complete = [this](std::shared_ptr<RenderJob> job) {
        std::cout << "[Render] Completed, output at: " << job->output_file << std::endl;
        playbackClock.resume();

        // Set flag to trigger reload on main thread (Vulkan ops must be on main thread)
        if (job->do_swap) {
            pendingNLEReload = true;
            std::cout << "[Render] Pending NLE reload flagged" << std::endl;
        }
    };
}

void Application::initMidi() {
    if (!midiSystem.initialize()) {
        std::cerr << "[Application] Failed to initialize MIDI system" << std::endl;
        return;
    }

    // Set up MIDI event callback to apply to visual controls
    midiSystem.setEventCallback([this](const MidiMessage& msg) {
        midiSystem.applyToVisualControls(msg, visualControls);
    });

    midiSystem.setTriggerCallback([this](const std::string& action) {
        handleOscTrigger(action);
    });

    // Try to open the first available MIDI port
    unsigned int portCount = midiSystem.getPortCount();
    if (portCount > 0) {
        std::cout << "[Application] Found " << portCount << " MIDI port(s)" << std::endl;
        for (unsigned int i = 0; i < portCount; ++i) {
            std::cout << "[Application]   Port " << i << ": " << midiSystem.getAvailablePorts()[i] << std::endl;
        }
        // Open first port by default (can be changed via UI)
        if (midiSystem.openPort(0)) {
            std::cout << "[Application] MIDI port 0 opened successfully" << std::endl;
        }
    } else {
        std::cout << "[Application] No MIDI ports detected" << std::endl;
    }
}

void Application::initOsc() {
    if (!oscSystem.initialize(9000)) {
        std::cerr << "[Application] Failed to initialize OSC system" << std::endl;
        return;
    }

    // Set up OSC event callback to apply to visual controls
    oscSystem.setEventCallback([this](const OscMessage& msg) {
        oscSystem.applyToVisualControls(msg, visualControls);
    });

    // Set up OSC trigger callback for button actions
    oscSystem.setTriggerCallback([this](const std::string& action) {
        handleOscTrigger(action);
    });

    // Auto-save control state whenever OSC mappings change
    oscSystem.onMappingsChanged = [this]() {
        ControlState::save(controlStatePath, visualControls, videoRandomizer, videoRandomizer2,
                           allowDimensionChangeRecreation, midiSystem, oscSystem,
                           selectedVideoAsset, selectedVideoAsset2);
    };

    // Start OSC listener
    if (oscSystem.start()) {
        std::cout << "[Application] OSC system started on port 9000" << std::endl;
    } else {
        std::cerr << "[Application] Failed to start OSC listener" << std::endl;
    }
}

void Application::initAudio() {
    if (!audioSystem.initialize()) {
        std::cerr << "[Application] Failed to initialize Audio system" << std::endl;
        return;
    }

    // Start audio stream
    if (audioSystem.startStream()) {
        std::cout << "[Application] Audio system started successfully" << std::endl;
    } else {
        std::cerr << "[Application] Failed to start audio stream" << std::endl;
    }
}

void Application::handleOscTrigger(const std::string& action) {
    if (action == "randomizeVideo") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideoFolder);
        if (assets.size() > 1) {
            int newIndex = pickNextVideoIndex(assets);
            reloadVideoAtIndex(newIndex, assets);
        }
        isReloadingVideo = false;
    } else if (action == "randomizeVideo2") {
        if (!canChangeVideo()) return;
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideo2Folder);
        if (assets.size() > 1) {
            int newIndex = pickNextVideoIndex2(assets);
            reloadVideoAtIndex2(newIndex, assets);
        }
        isReloadingVideo = false;
    } else if (action == "jumpRandom") {
        if (videoSubsystemInitialized) {
            double duration = videoPlayer.durationSeconds();
            if (duration > 0) {
                std::uniform_real_distribution<double> dist(0.0, duration);
                videoPlayer.seekSeconds(dist(rng));
            }
        }
    } else if (action == "folderChanged") {
        if (!canChangeVideo()) return;
        videoRegistry.scan(videoAssetsRoot, visualControls.selectedVideoFolder);
        const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideoFolder);
        videoRandomizer.shuffleQueue.clear();
        videoRandomizer.currentShuffleIndex = 0;
        videoRandomizer2.shuffleQueue.clear();
        videoRandomizer2.currentShuffleIndex = 0;
        if (!assets.empty()) {
            selectedVideoAsset = 0;
            videoSourcePath = assets[0].metadata.path;
            videoRenderer.reset();
            videoPlayer.shutdown();
            int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
            int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
            if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                videoPlayer.setAutoScale(visualControls.autoScaleVideo);
                vkDeviceWaitIdle(vulkanContext.getDevice());
                videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                videoTexture.cleanup(resourceSystem);
                videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(),
                                             vulkanContext.getGraphicsQueue(),
                                             static_cast<uint32_t>(videoPlayer.width()),
                                             static_cast<uint32_t>(videoPlayer.height()));
                cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()),
                                   static_cast<uint32_t>(videoPlayer.height()),
                                   static_cast<uint32_t>(videoPlayer.width()) * 4);
                multiPassPipeline.updateDescriptorSets(
                    uniformBufferManager.getBuffers(),
                    videoTexture.getImageView(),
                    videoTexture.getPrevImageView(),
                    videoTexture.getSampler(),
                    videoTexture.getSampler(),
                    videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                    videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler()
                );
            }
        }
        isReloadingVideo = false;
    } else if (action == "applyChanges") {
        // Visual controls are applied through uniform buffer updates
        controlsDirty = true;
    }
}

void Application::initMultiPassPipeline() {
    if (!videoSubsystemInitialized) {
        std::cout << "[Application] Skipping MultiPassPipeline - video not initialized" << std::endl;
        return;
    }

    auto extent = vulkanContext.getSwapchainExtent();
    VkFormat colorFormat = vulkanContext.getSwapchainImageFormat();

    // Get queue family index from VulkanContext
    uint32_t queueFamilyIndex = 0; // TODO: Get from VulkanContext properly

    if (!multiPassPipeline.initialize(
        vulkanContext.getPhysicalDevice(),
        vulkanContext.getDevice(),
        vulkanContext.getGraphicsQueue(),
        queueFamilyIndex,
        extent,
        colorFormat,
        videoTexture.getSampler(),
        videoTexture.getSampler(), // Use same sampler for prev
        videoTexture.getImageView(),
        videoTexture.getImageView(), // Use same view for prev
        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler(),
        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
        uniformBufferManager.getBuffers(),
        UniformBufferManager::getBufferSize() // Use consistent size from UniformBufferManager
    )) {
        std::cerr << "[Application] Failed to initialize MultiPassPipeline" << std::endl;
        return;
    }

    // Update descriptor sets with correct texture bindings
    multiPassPipeline.updateDescriptorSets(
        uniformBufferManager.getBuffers(),
        videoTexture.getImageView(),
        videoTexture.getImageView(), // Use same for prev
        videoTexture.getSampler(),
        videoTexture.getSampler(),   // Use same for prev
        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler()
    );

    std::cout << "[Application] MultiPassPipeline initialized successfully" << std::endl;
}

int Application::pickNextVideoIndex(const std::vector<VideoAsset>& assets) {
    if (assets.size() <= 1) return 0;

    if (videoRandomizer.useShuffleMode) {
        if (videoRandomizer.shuffleQueue.empty() ||
            videoRandomizer.currentShuffleIndex >= static_cast<int>(videoRandomizer.shuffleQueue.size())) {
            videoRandomizer.shuffleQueue.clear();
            for (size_t i = 0; i < assets.size(); ++i) {
                videoRandomizer.shuffleQueue.push_back(static_cast<int>(i));
            }
            std::shuffle(videoRandomizer.shuffleQueue.begin(), videoRandomizer.shuffleQueue.end(), rng);
            videoRandomizer.currentShuffleIndex = 0;
        }
        int newIndex = videoRandomizer.shuffleQueue[videoRandomizer.currentShuffleIndex];
        videoRandomizer.currentShuffleIndex++;
        return newIndex;
    } else {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(assets.size()) - 1);
        int newIndex;
        do {
            newIndex = dist(rng);
        } while (newIndex == selectedVideoAsset);
        return newIndex;
    }
}

int Application::pickNextVideoIndex2(const std::vector<VideoAsset>& assets) {
    if (assets.size() <= 1) return 0;

    if (videoRandomizer2.useShuffleMode) {
        if (videoRandomizer2.shuffleQueue.empty() ||
            videoRandomizer2.currentShuffleIndex >= static_cast<int>(videoRandomizer2.shuffleQueue.size())) {
            videoRandomizer2.shuffleQueue.clear();
            for (size_t i = 0; i < assets.size(); ++i) {
                videoRandomizer2.shuffleQueue.push_back(static_cast<int>(i));
            }
            std::shuffle(videoRandomizer2.shuffleQueue.begin(), videoRandomizer2.shuffleQueue.end(), rng);
            videoRandomizer2.currentShuffleIndex = 0;
        }
        int newIndex = videoRandomizer2.shuffleQueue[videoRandomizer2.currentShuffleIndex];
        videoRandomizer2.currentShuffleIndex++;
        return newIndex;
    } else {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(assets.size()) - 1);
        int newIndex;
        do {
            newIndex = dist(rng);
        } while (newIndex == selectedVideoAsset2);
        return newIndex;
    }
}

bool Application::reloadVideoAtIndex(int newIndex, const std::vector<VideoAsset>& assets) {
    selectedVideoAsset = newIndex;
    videoSourcePath = assets[newIndex].metadata.path;

    transitionActive = false;

    videoRenderer.reset();
    videoPlayer.shutdown();
    int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
    int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
    if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
        videoPlayer.setAutoScale(visualControls.autoScaleVideo);
        vkDeviceWaitIdle(vulkanContext.getDevice());
        videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture.cleanup(resourceSystem);
        videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                     vulkanContext.getCommandPool(),
                                     vulkanContext.getGraphicsQueue(),
                                     static_cast<uint32_t>(videoPlayer.width()),
                                     static_cast<uint32_t>(videoPlayer.height()));

        cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()),
                           static_cast<uint32_t>(videoPlayer.height()),
                           static_cast<uint32_t>(videoPlayer.width()) * 4);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            descriptorSetManager.updateSet(
                vulkanContext.getDevice(), i,
                uniformBufferManager.getBuffers()[i],
                const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
                const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
            );
        }

        multiPassPipeline.updateDescriptorSets(
            uniformBufferManager.getBuffers(),
            videoTexture.getImageView(),
            videoTexture.getPrevImageView(),
            videoTexture.getSampler(),
            videoTexture.getSampler(),
            videoTexture2.getImageView(),
            videoTexture2.getSampler()
        );

        videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);
        videoRandomizer.elapsedSeconds = 0.0f;
        videoRandomizer.currentVideoDuration = videoPlayer.durationSeconds();
        isReloadingVideo = false;
        return true;
    }
    isReloadingVideo = false;
    return false;
}

bool Application::reloadVideoAtIndex2(int newIndex, const std::vector<VideoAsset>& assets) {
    selectedVideoAsset2 = newIndex;
    videoSourcePath2 = assets[newIndex].metadata.path;

    transitionActive = false;

    videoRenderer2.reset();
    videoPlayer2.shutdown();
    int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
    int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
    if (videoPlayer2.initialize(videoSourcePath2, screenW, screenH)) {
        videoPlayer2.setAutoScale(visualControls.autoScaleVideo);
        vkDeviceWaitIdle(vulkanContext.getDevice());
        videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
        videoTexture2.cleanup(resourceSystem);
        videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                     vulkanContext.getCommandPool(),
                                     vulkanContext.getGraphicsQueue(),
                                     static_cast<uint32_t>(videoPlayer2.width()),
                                     static_cast<uint32_t>(videoPlayer2.height()));

        cpuFramePool2.resize(static_cast<uint32_t>(videoPlayer2.width()),
                           static_cast<uint32_t>(videoPlayer2.height()),
                           static_cast<uint32_t>(videoPlayer2.width()) * 4);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            descriptorSetManager.updateSet(
                vulkanContext.getDevice(), i,
                uniformBufferManager.getBuffers()[i],
                const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
                const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
            );
        }

        multiPassPipeline.updateDescriptorSets(
            uniformBufferManager.getBuffers(),
            videoTexture.getImageView(),
            videoTexture.getPrevImageView(),
            videoTexture.getSampler(),
            videoTexture.getSampler(),
            videoTexture2.getImageView(),
            videoTexture2.getSampler()
        );

        videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
        videoRandomizer2.elapsedSeconds = 0.0f;
        videoRandomizer2.currentVideoDuration = videoPlayer2.durationSeconds();
        isReloadingVideo = false;
        return true;
    }
    isReloadingVideo = false;
    return false;
}

void Application::mainLoop() {
    while (running) {
        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Pass event to UI system
            uiSystem.processEvent(event);

            if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                const SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_1 || key == SDLK_KP_1) {
                    handleOscTrigger("randomizeVideo");
                } else if (key == SDLK_2 || key == SDLK_KP_2) {
                    handleOscTrigger("randomizeVideo2");
                }
            }

            // Handle window close (both SDL_QUIT and window close event)
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }

            // Handle window resize
            if (event.type == SDL_WINDOWEVENT) {
                SDL_Window* sourceWindow = SDL_GetWindowFromID(event.window.windowID);
                if (sourceWindow == window.getMainWindow() &&
                    event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    running = false;
                    break;
                }
                if (sourceWindow == window.getUiWindow() &&
                    event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    running = false;
                    break;
                }
                if (sourceWindow == window.getMainWindow() &&
                    (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) &&
                    initializationComplete) {
                    window.resetResizeFlag();
                    uint32_t width, height;
                    window.getDrawableSize(width, height);
                    vkDeviceWaitIdle(vulkanContext.getDevice());

                    // 1. Stop frame system first
                    frameSystem.cleanup();

                    // 2. Destroy swapchain-dependent GPU resources
                    for (auto framebuffer : swapchainFramebuffers) {
                        vkDestroyFramebuffer(vulkanContext.getDevice(), framebuffer, nullptr);
                    }
                    swapchainFramebuffers.clear();

                    if (videoSubsystemInitialized) {
                        vkDeviceWaitIdle(vulkanContext.getDevice());
                        videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                        videoTexture.cleanup(resourceSystem);  // Destroy staging ring
                    }
                    if (videoSubsystemInitialized2) {
                        vkDeviceWaitIdle(vulkanContext.getDevice());
                        videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
                        videoTexture2.cleanup(resourceSystem);
                    }

                    // 3. Recreate swapchain
                    vulkanContext.recreateSwapchain(width, height);

                    // 4. Recreate video texture resources
                    if (videoSubsystemInitialized) {
                        videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                                     vulkanContext.getCommandPool(),
                                                     vulkanContext.getGraphicsQueue(),
                                                     static_cast<uint32_t>(videoPlayer.width()),
                                                     static_cast<uint32_t>(videoPlayer.height()));
                    }
                    if (videoSubsystemInitialized2) {
                        videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                                    vulkanContext.getCommandPool(),
                                                    vulkanContext.getGraphicsQueue(),
                                                    static_cast<uint32_t>(videoPlayer2.width()),
                                                    static_cast<uint32_t>(videoPlayer2.height()));
                    }

                    // Update multipass descriptor sets with new video texture handles
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler()
                    );

                    // 5. Recreate framebuffers
                    initFramebuffers();

                    // 6. Update descriptor sets with video texture and uniform buffers
                    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                        descriptorSetManager.updateSet(vulkanContext.getDevice(), i,
                                                       uniformBufferManager.getBuffers()[i],
                                                       videoSubsystemInitialized ? const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()) : nullptr,
                                                       videoSubsystemInitialized ? const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo()) : nullptr);
                    }

                    // 7. Restart frame system last
                    frameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT, vulkanContext.getSwapchainImageCount());
                    frameSystem.resetCurrentFrame();

                    // Wait for all fences to be signaled before resuming
                    frameSystem.waitForAllFences();
                }
            }
        }

        if (!running) {
            break;
        }

        // Begin frame
        uint32_t imageIndex;
        VkResult result;
        FrameContext& frame = frameSystem.beginFrame(vulkanContext.getSwapchain(), imageIndex, result);
        
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                continue;
            } else {
                throw std::runtime_error("failed to acquire swapchain image");
            }
        }
        
        // Calculate delta time
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrameTimestamp).count();
        lastFrameTimestamp = now;

        // Check for pending NLE reload (from background thread)
        if (pendingNLEReload) {
            pendingNLEReload = false;
            if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) {
                // Reload video 1 directly by path
                videoRenderer.reset();
                videoPlayer.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                    videoPlayer.setAutoScale(visualControls.autoScaleVideo);
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture.cleanup(resourceSystem);
                    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer.width()),
                                                 static_cast<uint32_t>(videoPlayer.height()));
                    cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()),
                                        static_cast<uint32_t>(videoPlayer.height()),
                                        static_cast<uint32_t>(videoPlayer.width()) * 4);
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture2.getSampler());
                    videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);
                    std::cout << "[Render] Auto-reloaded Video 1: " << videoSourcePath << std::endl;
                }
            } else {
                // Reload video 2 directly by path
                videoRenderer2.reset();
                videoPlayer2.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer2.initialize(videoSourcePath2, screenW, screenH)) {
                    videoPlayer2.setAutoScale(visualControls.autoScaleVideo);
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture2.cleanup(resourceSystem);
                    videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer2.width()),
                                                 static_cast<uint32_t>(videoPlayer2.height()));
                    cpuFramePool2.resize(static_cast<uint32_t>(videoPlayer2.width()),
                                         static_cast<uint32_t>(videoPlayer2.height()),
                                         static_cast<uint32_t>(videoPlayer2.width()) * 4);
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture2.getSampler());
                    videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
                    std::cout << "[Render] Auto-reloaded Video 2: " << videoSourcePath2 << std::endl;
                }
            }
        }

        transitionActive = false;

        // Update auto-randomize colors with smooth interpolation
        if (visualControls.autoRandomizeColors) {
            visualControls.colorRandomizeElapsed += deltaTime;
            
            if (visualControls.colorRandomizeElapsed >= visualControls.colorRandomizeInterval) {
                // Generate new target colors
                std::uniform_real_distribution<float> hueDist(0.0f, 360.0f);
                std::uniform_real_distribution<float> satDist(0.6f, 1.0f);
                std::uniform_real_distribution<float> valDist(0.7f, 1.0f);
                
                float primaryHue = hueDist(rng);
                float primarySat = satDist(rng);
                float primaryVal = valDist(rng);
                float secondaryHue = fmod(primaryHue + 180.0f, 360.0f);
                
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
                
                visualControls.primaryColorTarget = glm::vec4(hsvToRgb(primaryHue, primarySat, primaryVal), 1.0f);
                visualControls.secondaryColorTarget = glm::vec4(hsvToRgb(secondaryHue, primarySat, primaryVal), 1.0f);
                visualControls.colorRandomizeElapsed = 0.0f;
            }
            
            // Smooth interpolation towards target colors
            float lerpSpeed = 2.0f * deltaTime; // Adjust speed factor as needed
            visualControls.primaryColor = glm::mix(visualControls.primaryColor, visualControls.primaryColorTarget, lerpSpeed);
            visualControls.secondaryColor = glm::mix(visualControls.secondaryColor, visualControls.secondaryColorTarget, lerpSpeed);
        }

        // Update auto-randomize
        if (videoRandomizer.autoRandomize && videoSubsystemInitialized) {
            videoRandomizer.elapsedSeconds += deltaTime;
            float targetInterval = (videoRandomizer.useVideoDuration && videoRandomizer.currentVideoDuration > 0.0f)
                ? videoRandomizer.currentVideoDuration
                : videoRandomizer.intervalSeconds;

            if (videoRandomizer.elapsedSeconds >= targetInterval) {
                if (!canChangeVideo()) {
                    videoRandomizer.elapsedSeconds = 0.0f;
                } else {
                    // Trigger random video change
                    const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideoFolder);
                    if (assets.size() > 1) {
                        int newIndex = pickNextVideoIndex(assets);
                        reloadVideoAtIndex(newIndex, assets);
                    }
                    isReloadingVideo = false;
                }
            }
        }

        // Update auto-randomize for Video 2
        if (videoRandomizer2.autoRandomize && videoSubsystemInitialized && visualControls.enableDualVideo) {
            videoRandomizer2.elapsedSeconds += deltaTime;
            float targetInterval = (videoRandomizer2.useVideoDuration && videoRandomizer2.currentVideoDuration > 0.0f)
                ? videoRandomizer2.currentVideoDuration
                : videoRandomizer2.intervalSeconds;

            if (videoRandomizer2.elapsedSeconds >= targetInterval) {
                if (!canChangeVideo()) {
                    videoRandomizer2.elapsedSeconds = 0.0f;
                } else {
                    // Trigger random video change for Video 2
                    const auto& assets2 = videoRegistry.getFilteredAssets(visualControls.selectedVideo2Folder);
                    if (assets2.size() > 1) {
                        int newIndex = pickNextVideoIndex2(assets2);
                        reloadVideoAtIndex2(newIndex, assets2);
                    }
                    isReloadingVideo = false;
                }
            }
        }

        // Update auto random jump interval
        if (visualControls.enableRandomJumpInterval && visualControls.randomVideoStart && videoSubsystemInitialized) {
            auto timeSinceLastJump = std::chrono::duration<float>(now - lastRandomJumpTime).count();
            if (timeSinceLastJump >= visualControls.randomJumpInterval) {
                // Perform random jump within full video range (0% to 100%)
                double duration = videoPlayer.durationSeconds();
                if (duration > 0) {
                    std::uniform_real_distribution<double> dist(0.0, duration);
                    videoPlayer.seekSeconds(dist(rng));
                    lastRandomJumpTime = now;
                }
            }
        }

        // Update MIDI system
        midiSystem.update();

        // Update OSC system
        oscSystem.update();

        // Update uniform buffer
        updateUniformBuffer(frame.frameIndex);

        // Update video playback rate
        if (videoSubsystemInitialized) {
            videoPlayer.setPlaybackRate(visualControls.videoPlaybackRate);
        }
        if (videoSubsystemInitialized2) {
            videoPlayer2.setPlaybackRate(visualControls.video2PlaybackRate);
        }

        // Update video texture
        if (videoRenderer) {
            videoRenderer->update(deltaTime, frame.frameIndex);
        }
        if (videoRenderer2) {
            videoRenderer2->update(deltaTime, frame.frameIndex);
        }

        // Render UI
        UIDiagnostics diag;
        diag.lastFrameFrameIndex = frame.frameIndex;
        diag.lastFrameImageIndex = frame.swapchainImageIndex;
        diag.swapchainWidth = vulkanContext.getSwapchainExtent().width;
        diag.swapchainHeight = vulkanContext.getSwapchainExtent().height;
        diag.currentMode = visualControls.activeMode;
        diag.videoReady = videoSubsystemInitialized && videoTexture.isReady();
        diag.videoWidth = videoTexture.getWidth();
        diag.videoHeight = videoTexture.getHeight();
        diag.animationTime = debugAnimationTime;
        diag.animationDelta = debugAnimationDelta;
        diag.animationElapsedSeconds = debugAnimationElapsedSeconds;
        
        UICallbacks callbacks;
        callbacks.onControlsChanged = [this]() { controlsDirty = true; };
        callbacks.onApplyChanges = [this]() {
            if (!canChangeVideo()) return;
            // Reload the video that was modified based on NLE Editor selection
            if (g_project_state.nleVideoSource == NLEVideoSource::VIDEO_1) {
                // Reload video 1 directly by path
                videoRenderer.reset();
                videoPlayer.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                    videoPlayer.setAutoScale(visualControls.autoScaleVideo);
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture.cleanup(resourceSystem);
                    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer.width()),
                                                 static_cast<uint32_t>(videoPlayer.height()));
                    cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()),
                                        static_cast<uint32_t>(videoPlayer.height()),
                                        static_cast<uint32_t>(videoPlayer.width()) * 4);
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture2.getSampler());
                    videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);
                }
            } else {
                // Reload video 2 directly by path
                videoRenderer2.reset();
                videoPlayer2.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer2.initialize(videoSourcePath2, screenW, screenH)) {
                    videoPlayer2.setAutoScale(visualControls.autoScaleVideo);
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture2.cleanup(resourceSystem);
                    videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer2.width()),
                                                 static_cast<uint32_t>(videoPlayer2.height()));
                    cpuFramePool2.resize(static_cast<uint32_t>(videoPlayer2.width()),
                                         static_cast<uint32_t>(videoPlayer2.height()),
                                         static_cast<uint32_t>(videoPlayer2.width()) * 4);
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture2.getSampler());
                    videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
                }
            }
            isReloadingVideo = false;
        };
        callbacks.onFolderChanged = [this]() {
            if (!canChangeVideo()) return;
            videoRegistry.scan(videoAssetsRoot, visualControls.selectedVideoFolder);
            const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideoFolder);
            videoRandomizer.shuffleQueue.clear();
            videoRandomizer.currentShuffleIndex = 0;
            videoRandomizer2.shuffleQueue.clear();
            videoRandomizer2.currentShuffleIndex = 0;
            if (!assets.empty()) {
                selectedVideoAsset = 0;
                videoSourcePath = assets[0].metadata.path;
                videoRenderer.reset();
                videoPlayer.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                    videoPlayer.setAutoScale(visualControls.autoScaleVideo);
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture.cleanup(resourceSystem);
                    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer.width()),
                                                 static_cast<uint32_t>(videoPlayer.height()));
                    cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()),
                                       static_cast<uint32_t>(videoPlayer.height()),
                                       static_cast<uint32_t>(videoPlayer.width()) * 4);
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler());
                }
            }
            isReloadingVideo = false;
        };
        callbacks.onRandomizeVideo = [this]() {
            if (!canChangeVideo()) return;
            const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideoFolder);
            if (assets.size() > 1) {
                int newIndex = pickNextVideoIndex(assets);
                reloadVideoAtIndex(newIndex, assets);
            }
            isReloadingVideo = false;
        };
        callbacks.onJumpRandom = [this]() {
            if (videoSubsystemInitialized) {
                double duration = videoPlayer.durationSeconds();
                if (duration > 0) {
                    std::uniform_real_distribution<double> dist(0.0, duration);
                    videoPlayer.seekSeconds(dist(rng));
                }
            }
        };
        callbacks.onRandomizeVideo2 = [this]() {
            if (!canChangeVideo()) return;
            const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideo2Folder);
            if (assets.size() > 1) {
                int newIndex = pickNextVideoIndex2(assets);
                reloadVideoAtIndex2(newIndex, assets);
            }
            isReloadingVideo = false;
        };
        callbacks.onReloadVideo = [this](const std::string& path) {
            if (!canChangeVideo()) return;
            videoSourcePath = path;
            videoRenderer.reset();
            videoPlayer.shutdown();
            int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
            int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
            if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                videoPlayer.setAutoScale(visualControls.autoScaleVideo);
                vkDeviceWaitIdle(vulkanContext.getDevice());
                videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                videoTexture.cleanup(resourceSystem);  // Destroy staging ring
                videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(),
                                             vulkanContext.getGraphicsQueue(),
                                             static_cast<uint32_t>(videoPlayer.width()),
                                             static_cast<uint32_t>(videoPlayer.height()));

                // Resize CPU frame pool to new resolution
                cpuFramePool.resize(static_cast<uint32_t>(videoPlayer.width()),
                                   static_cast<uint32_t>(videoPlayer.height()),
                                   static_cast<uint32_t>(videoPlayer.width()) * 4);

                // Update descriptor sets with new textures
                for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                    descriptorSetManager.updateSet(
                        vulkanContext.getDevice(), i,
                        uniformBufferManager.getBuffers()[i],
                        const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
                        const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
                    );
                }

                // Update multipass descriptor sets with new textures
                multiPassPipeline.updateDescriptorSets(
                    uniformBufferManager.getBuffers(),
                    videoTexture.getImageView(),
                    videoTexture.getPrevImageView(),
                    videoTexture.getSampler(),
                    videoTexture.getSampler(),
                    videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                    videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler()
                );

                videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture, cpuFramePool);
                videoRandomizer.elapsedSeconds = 0.0f;
                videoRandomizer.currentVideoDuration = videoPlayer.durationSeconds();
            }
            isReloadingVideo = false;
        };

        callbacks.onFolderChanged2 = [this]() {
            if (!canChangeVideo()) return;
            videoRegistry.scan(videoAssetsRoot, visualControls.selectedVideo2Folder);
            const auto& assets = videoRegistry.getFilteredAssets(visualControls.selectedVideo2Folder);
            videoRandomizer.shuffleQueue.clear();
            videoRandomizer.currentShuffleIndex = 0;
            videoRandomizer2.shuffleQueue.clear();
            videoRandomizer2.currentShuffleIndex = 0;
            if (!assets.empty()) {
                selectedVideoAsset2 = 0;
                videoSourcePath2 = assets[0].metadata.path;
                videoRenderer2.reset();
                videoPlayer2.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer2.initialize(videoSourcePath2, screenW, screenH)) {
                    videoPlayer2.setAutoScale(visualControls.autoScaleVideo);
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture2.cleanup(resourceSystem);
                    videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer2.width()),
                                                 static_cast<uint32_t>(videoPlayer2.height()));
                    cpuFramePool2.resize(static_cast<uint32_t>(videoPlayer2.width()),
                                         static_cast<uint32_t>(videoPlayer2.height()),
                                         static_cast<uint32_t>(videoPlayer2.width()) * 4);
                    multiPassPipeline.updateDescriptorSets(
                        uniformBufferManager.getBuffers(),
                        videoTexture.getImageView(),
                        videoTexture.getPrevImageView(),
                        videoTexture.getSampler(),
                        videoTexture.getSampler(),
                        videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                        videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler());
                }
            }
            isReloadingVideo = false;
        };

        callbacks.onReloadVideo2 = [this](const std::string& path) {
            if (!canChangeVideo()) return;
            std::cout << "[Application] onReloadVideo2 called with: " << path << std::endl;
            videoSourcePath2 = path;
            videoRenderer2.reset();
            videoPlayer2.shutdown();
            int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
            int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
            std::cout << "[Application] Initializing video player 2 with: " << videoSourcePath2 << std::endl;
            if (videoPlayer2.initialize(videoSourcePath2, screenW, screenH)) {
                std::cout << "[Application] Video player 2 initialized successfully, isReady=" << videoPlayer2.isReady() << std::endl;
                videoPlayer2.setAutoScale(visualControls.autoScaleVideo);
                vkDeviceWaitIdle(vulkanContext.getDevice());
                videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
                videoTexture2.cleanup(resourceSystem);
                videoTexture2.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(),
                                             vulkanContext.getGraphicsQueue(),
                                             static_cast<uint32_t>(videoPlayer2.width()),
                                             static_cast<uint32_t>(videoPlayer2.height()));
                cpuFramePool2.resize(static_cast<uint32_t>(videoPlayer2.width()),
                                   static_cast<uint32_t>(videoPlayer2.height()),
                                   static_cast<uint32_t>(videoPlayer2.width()) * 4);
                multiPassPipeline.updateDescriptorSets(
                    uniformBufferManager.getBuffers(),
                    videoTexture.getImageView(),
                    videoTexture.getPrevImageView(),
                    videoTexture.getSampler(),
                    videoTexture.getSampler(),
                    videoTexture2.isReady() ? videoTexture2.getImageView() : videoTexture.getImageView(),
                    videoTexture2.isReady() ? videoTexture2.getSampler() : videoTexture.getSampler()
                );
                videoRenderer2 = std::make_unique<VideoRenderer>(videoPlayer2, videoTexture2, cpuFramePool2);
            }
            isReloadingVideo = false;
        };

        uiSystem.render(visualControls, videoRandomizer, videoRandomizer2, videoPlayer, videoPlayer2, videoRegistry,
                       selectedVideoAsset, selectedVideoAsset2, transitionDuration, transitionDuration2, allowDimensionChangeRecreation,
                       controlsDirty, rng, diag, callbacks, midiSystem, oscSystem, audioSystem, videoSourcePath, videoSourcePath2);
        
        // Record command buffer
        recordCommandBuffer(commandBuffers[frame.frameIndex], frame);

        // Submit command buffer
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore renderFinishedSemaphore = frameSystem.getRenderFinishedSemaphore(frame.swapchainImageIndex);

        // Use frame's imageAvailableSemaphore for waiting (it gets signaled by vkAcquireNextImageKHR)
        VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
        VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[frame.frameIndex];

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(vulkanContext.getDevice(), 1, &frame.inFlightFence);

        if (vkQueueSubmit(vulkanContext.getGraphicsQueue(), 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer");
        }

        // Present swapchain
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

        VkSwapchainKHR swapChains[] = {vulkanContext.getSwapchain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &frame.swapchainImageIndex;

        VkResult presentResult = vkQueuePresentKHR(vulkanContext.getPresentQueue(), &presentInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            // Handle resize
            window.resetResizeFlag();
        }

        // Submit frame
        frameSystem.endFrame();
    }
}

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
    float speed = std::max(0.01f, visualControls.animationSpeed);
    accumulatedTime += globalDeltaTime * speed;

    debugAnimationElapsedSeconds = accumulatedTime;
    debugAnimationDelta = globalDeltaTime;
    float time = accumulatedTime;

    // Update audio values from AudioSystem (normalized + smoothed)
    auto normalizeAudioLevel = [](float rawValue, float gain, float gamma) {
        float scaled = std::clamp(rawValue * gain, 0.0f, 1.0f);
        return (gamma != 1.0f) ? std::pow(scaled, gamma) : scaled;
    };

    float liveEnergy = normalizeAudioLevel(audioSystem.getRMS(),           5.0f, 0.85f);
    float liveBass   = normalizeAudioLevel(audioSystem.getSmoothedBass(),  7.0f, 0.90f);
    float liveMid    = normalizeAudioLevel(audioSystem.getSmoothedMid(),   8.0f, 0.95f);
    float liveHigh   = normalizeAudioLevel(audioSystem.getSmoothedHigh(),  9.0f, 1.00f);

    auto& reactive = visualControls.audioReactiveRuntime;
    reactive.enabled = visualControls.enableAudioReactive;
    reactive.energy = liveEnergy;
    reactive.bass   = liveBass;
    reactive.mid    = liveMid;
    reactive.high   = liveHigh;

    if (visualControls.enableAudioReactive) {
        visualControls.energy = liveEnergy;
        visualControls.bass   = liveBass;
        visualControls.mid    = liveMid;
        visualControls.high   = liveHigh;
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
    ubo.time = time;
    ubo.mode = visualControls.activeMode;
    ubo.videoMix = visualControls.videoMix;
    ubo.videoAvailable = (videoSubsystemInitialized && videoTexture.isReady()) ? 1.0f : 0.0f;
    ubo.video2Mix = visualControls.video2Mix;
    ubo.video2Available = (visualControls.enableDualVideo && videoSubsystemInitialized2 && videoTexture2.isReady()) ? 1.0f : 0.0f;
    ubo.video2BlendMode = visualControls.video2BlendMode;

    // Set visual control values
    ubo.primaryColor = visualControls.primaryColor;
    ubo.secondaryColor = visualControls.secondaryColor;
    ubo.colorBlend = visualControls.colorBlend;
    ubo.tempo = visualControls.tempo;
    ubo.energy = visualControls.energy;
    ubo.bass = visualControls.bass;
    ubo.mid = visualControls.mid;
    ubo.high = visualControls.high;
    
    // Audio reactivity parameters
    ubo.audioWarpResponse = visualControls.audioWarpResponse;
    ubo.audioFeedbackResponse = visualControls.audioFeedbackResponse;
    ubo.audioBlurResponse = visualControls.audioBlurResponse;
    ubo.audioColorResponse = visualControls.audioColorResponse;
    ubo.audioGlitchResponse = visualControls.audioGlitchResponse;
    ubo.audioBeatSync = visualControls.audioBeatSync;
    ubo.audioLfoRate = visualControls.audioLfoRate;
    ubo.enableAudioReactive = visualControls.enableAudioReactive ? 1 : 0;
    
    // Post FX basicos
    ubo.grayscaleAmount = visualControls.grayscaleAmount;
    ubo.sharpenAmount = visualControls.sharpenAmount;
    ubo.upscaleEnabled = visualControls.upscaleEnabled ? 1.0f : 0.0f;

    // Enable/Disable flags for post FX
    ubo.enablePostCrtCurvature = visualControls.enablePostCrtCurvature ? 1 : 0;
    ubo.enablePostScanMask = visualControls.enablePostScanMask ? 1 : 0;
    ubo.enablePostVignette = visualControls.enablePostVignette ? 1 : 0;
    ubo.enablePostFishEye = visualControls.enablePostFishEye ? 1 : 0;
    ubo.enablePostBloom = visualControls.enablePostBloom ? 1 : 0;
    ubo.enablePostAberration = visualControls.enablePostAberration ? 1 : 0;
    ubo.enablePostGrain = visualControls.enablePostGrain ? 1 : 0;
    ubo.enablePostBend = visualControls.enablePostBend ? 1 : 0;
    ubo.enablePostGlitch = visualControls.enablePostGlitch ? 1 : 0;
    ubo.enablePostColorBalance = visualControls.enablePostColorBalance ? 1 : 0;

    // Enable/Disable flags for VJAY BASICS
    ubo.enableColorGrading = visualControls.enableColorGrading ? 1 : 0;
    ubo.enableFeedback = visualControls.enableFeedback ? 1 : 0;
    ubo.enableDistortion = visualControls.enableDistortion ? 1 : 0;
    ubo.enableBlurMotion = visualControls.enableBlurMotion ? 1 : 0;
    ubo.enableSharpen = visualControls.enableSharpen ? 1 : 0;
    ubo.enablePostGlitch = visualControls.enableGlitch ? 1 : 0;
    ubo.enableBlending = visualControls.enableBlending ? 1 : 0;
    ubo.enableAnalog = visualControls.enableAnalog ? 1 : 0;
    ubo.enableAudioReactive = visualControls.enableAudioReactive ? 1 : 0;
    ubo.enableTemporal = visualControls.enableTemporal ? 1 : 0;

    // Enable/Disable flags for VJAY EXTRA
    ubo.enablePixelate = visualControls.enablePixelate ? 1 : 0;
    ubo.enableStrobe = visualControls.enableStrobe ? 1 : 0;
    ubo.enableThreshold = visualControls.enableThreshold ? 1 : 0;
    ubo.enableSlowZoom = visualControls.enableSlowZoom ? 1 : 0;
    ubo.enableMirror = visualControls.enableMirror ? 1 : 0;
    ubo.enableInvert = visualControls.enableInvert ? 1 : 0;
    ubo.enablePosterize = visualControls.enablePosterize ? 1 : 0;
    ubo.enableInfrared = visualControls.enableInfrared ? 1 : 0;
    ubo.enableZoomPulse = visualControls.enableZoomPulse ? 1 : 0;
    ubo.enableRGBShift = visualControls.enableRGBShift ? 1 : 0;

    // CRT
    ubo.crtCurvature = visualControls.crtCurvature;
    ubo.crtHorizontalCurvature = visualControls.crtHorizontalCurvature;
    ubo.crtScanlineIntensity = visualControls.crtScanlineIntensity;
    ubo.crtMaskIntensity = visualControls.crtMaskIntensity;
    ubo.crtVignette = visualControls.crtVignette;
    ubo.crtFishEye = visualControls.crtFishEye;
    
    // Bloom
    ubo.bloomIntensity = visualControls.bloomIntensity;
    ubo.bloomThreshold = visualControls.bloomThreshold;
    
    // Aberracion / grano / bend / glitch
    ubo.aberrationAmount = visualControls.aberrationAmount;
    ubo.grainStrength = visualControls.grainStrength;
    ubo.bendAmount = visualControls.bendAmount;
    ubo.glitchAmount = visualControls.glitchAmount;
    
    // Color grading
    ubo.colorBalance = visualControls.colorBalance;
    ubo.gradeBrightness = visualControls.gradeBrightness;
    ubo.gradeContrast = visualControls.gradeContrast;
    ubo.gradeSaturation = visualControls.gradeSaturation;
    ubo.gradeHueShift = visualControls.gradeHueShift;
    ubo.gradeGamma = visualControls.gradeGamma;
    ubo.colorLUTIndex = visualControls.colorLUTIndex;
    ubo.splitToneBalance = visualControls.splitToneBalance;
    ubo.splitToneShadows = visualControls.splitToneShadows;
    ubo.splitToneHighlights = visualControls.splitToneHighlights;
    
    // Feedback temporal
    ubo.feedbackAmount = visualControls.feedbackAmount;
    ubo.trailStrength = visualControls.trailStrength;
    ubo.temporalAccumulation = visualControls.temporalAccumulation;
    ubo.feedbackDecay = visualControls.feedbackDecay;
    ubo.recursiveBlend = visualControls.recursiveBlend;
    
    // Distorsion espacial
    ubo.uvWarpStrength = visualControls.uvWarpStrength;
    ubo.rippleStrength = visualControls.rippleStrength;
    ubo.rippleFrequency = visualControls.rippleFrequency;
    ubo.swirlStrength = visualControls.swirlStrength;
    ubo.displacementAmount = visualControls.displacementAmount;
    ubo.kaleidoSegments = visualControls.kaleidoSegments;
    ubo.tunnelDepth = visualControls.tunnelDepth;
    ubo.tunnelCurvature = visualControls.tunnelCurvature;
    
    // Blur y motion
    ubo.gaussianBlur = visualControls.gaussianBlur;
    ubo.directionalBlur = visualControls.directionalBlur;
    ubo.directionalBlurAngle = visualControls.directionalBlurAngle;
    ubo.zoomBlur = visualControls.zoomBlur;
    ubo.motionBlur = visualControls.motionBlur;
    ubo.temporalBlur = visualControls.temporalBlur;
    
    // Sharpening
    ubo.unsharpMask = visualControls.unsharpMask;
    ubo.casAmount = visualControls.casAmount;
    ubo.localContrast = visualControls.localContrast;
    
    // Glitch detallado
    ubo.glitchDatamosh = visualControls.glitchDatamosh;
    ubo.glitchRGBSplit = visualControls.glitchRGBSplit;
    ubo.glitchScanlineBreak = visualControls.glitchScanlineBreak;
    ubo.glitchJitter = visualControls.glitchJitter;
    ubo.glitchTearing = visualControls.glitchTearing;
    ubo.glitchPixelSort = visualControls.glitchPixelSort;
    ubo.glitchBufferCorruption = visualControls.glitchBufferCorruption;
    
    // Blending / compositing
    ubo.blendModeProcedural = visualControls.blendModeProcedural;
    ubo.blendModeVideo = visualControls.blendModeVideo;
    ubo.blendModeFeedback = visualControls.blendModeFeedback;
    ubo.blendProceduralMix = visualControls.blendProceduralMix;
    ubo.blendVideoMix = visualControls.blendVideoMix;
    ubo.blendFeedbackMix = visualControls.blendFeedbackMix;
    
    // Analog / CRT avanzado
    ubo.analogScanlineFocus = visualControls.analogScanlineFocus;
    ubo.analogMaskBalance = visualControls.analogMaskBalance;
    
    // Temporal
    ubo.frameAccumulation = visualControls.frameAccumulation;
    ubo.slowMotionFactor = visualControls.slowMotionFactor;
    ubo.temporalInterpolation = visualControls.temporalInterpolation;
    
    // Efectos extra (VJAY EXTRA)
    ubo.pixelateAmount = visualControls.pixelateAmount;
    ubo.strobeSpeed = visualControls.strobeSpeed;
    ubo.thresholdLevel = visualControls.thresholdLevel;
    ubo.slowZoomAmount = visualControls.slowZoomAmount;
    ubo.enableEdgeDetect = visualControls.enableEdgeDetect ? 1 : 0;
    ubo.edgeStrength = visualControls.edgeStrength;
    ubo.edgeThreshold = visualControls.edgeThreshold;
    ubo.edgeBlend = visualControls.edgeBlend;
    ubo.edgeColor = visualControls.edgeColor;
    ubo.mirrorAmount = visualControls.mirrorAmount;
    ubo.posterizeLevels = visualControls.posterizeLevels;
    ubo.zoomPulseAmount = visualControls.zoomPulseAmount;
    ubo.rgbShiftAmount = visualControls.rgbShiftAmount;
    
    // FXAA
    ubo.enableFXAA = visualControls.enableFXAA ? 1 : 0;
    ubo.fxaaQualitySubpix = visualControls.fxaaQualitySubpix;
    ubo.fxaaQualityEdgeThreshold = visualControls.fxaaQualityEdgeThreshold;
    ubo.fxaaQualityEdgeThresholdMin = visualControls.fxaaQualityEdgeThresholdMin;

    // Grid / Mirroring
    ubo.enableGrid = visualControls.enableGrid ? 1 : 0;
    ubo.gridMode = visualControls.gridMode;
    ubo.gridCount = visualControls.gridCount;
    ubo.gridRows = visualControls.gridRows;
    ubo.gridColumns = visualControls.gridColumns;
    ubo.gridMirrorCells = visualControls.gridMirrorCells ? 1 : 0;

    // Camera movement
    ubo.cameraZoom = visualControls.cameraZoom;
    ubo.cameraPanX = visualControls.cameraPanX;
    ubo.cameraPanY = visualControls.cameraPanY;
    ubo.cameraRotation = visualControls.cameraRotation;
    ubo.enableCameraMovement = visualControls.enableCameraMovement ? 1 : 0;

    // Final RGB overlay
    ubo.rgbOverlay = visualControls.rgbOverlay;
    ubo.enableRgbOverlay = visualControls.enableRgbOverlay ? 1 : 0;

    // ------------------------------------------------------------------
    // AUDIO REACTIVITY AUTO-MODULATION
    // When enabled, audio levels automatically drive effect intensities
    // so the layers animate without manual slider tweaking.
    // ------------------------------------------------------------------
    float envClamped  = std::clamp(visualControls.energy, 0.0f, 1.0f);
    float bassClamped = std::clamp(visualControls.bass,   0.0f, 1.0f);
    float midClamped  = std::clamp(visualControls.mid,    0.0f, 1.0f);
    float highClamped = std::clamp(visualControls.high,   0.0f, 1.0f);

    float warpGain     = std::max(0.0f, visualControls.audioWarpResponse);
    float feedbackGain = std::max(0.0f, visualControls.audioFeedbackResponse);
    float blurGain     = std::max(0.0f, visualControls.audioBlurResponse);
    float colorGain    = std::max(0.0f, visualControls.audioColorResponse);
    float glitchGain   = std::max(0.0f, visualControls.audioGlitchResponse);

    if (visualControls.enableAudioReactive) {
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
        if (visualControls.enableCameraMovement) {
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

void Application::recordCommandBuffer(VkCommandBuffer commandBuffer, FrameContext& frame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer");
    }

    // Record video texture uploads
    videoTexture.recordPendingUpload(commandBuffer, frame.frameIndex, vulkanContext.getGraphicsQueue());
    if (videoSubsystemInitialized2 && videoTexture2.isReady()) {
        videoTexture2.recordPendingUpload(commandBuffer, frame.frameIndex, vulkanContext.getGraphicsQueue());
    }

    // Execute multi-pass pipeline
    if (videoSubsystemInitialized) {
        VkDescriptorSet descriptorSet = descriptorSetManager.getSet(frame.frameIndex);
        multiPassPipeline.execute(
            commandBuffer,
            frame.frameIndex,
            descriptorSet,
            renderPass,
            swapchainFramebuffers,
            frame.swapchainImageIndex,
            fullscreenPipeline,
            pipelineLayout,
            descriptorSet,
            vulkanContext.getSwapchainExtent(),
            swapchainSampler
        );
    } else {
        // Fallback to fullscreen pipeline if video not initialized
        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = swapchainFramebuffers[frame.swapchainImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = vulkanContext.getSwapchainExtent();
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fullscreenPipeline);
        VkDescriptorSet descriptorSet = descriptorSetManager.getSet(frame.frameIndex);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                               0, 1, &descriptorSet, 0, nullptr);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(vulkanContext.getSwapchainExtent().width);
        viewport.height = static_cast<float>(vulkanContext.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vulkanContext.getSwapchainExtent();
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to end recording command buffer");
    }
}

void Application::cleanup() {
    if (!initializationComplete) {
        return;
    }

    vkDeviceWaitIdle(vulkanContext.getDevice());

    // Save control state
    ControlState::save(controlStatePath, visualControls, videoRandomizer, videoRandomizer2,
                       allowDimensionChangeRecreation, midiSystem, oscSystem,
                       selectedVideoAsset, selectedVideoAsset2);

    // Shutdown UI
    uiSystem.shutdown();

    // Cleanup frame system (semaphores and fences)
    frameSystem.cleanup();

    // Cleanup video
    videoPlayer.shutdown();
    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture.cleanup(resourceSystem);  // Destroy staging ring

    // Cleanup video 2
    videoPlayer2.shutdown();
    videoTexture2.destroy(resourceSystem, vulkanContext.getDevice());
    videoTexture2.cleanup(resourceSystem);

    // Cleanup uniform buffers
    uniformBufferManager.destroy(resourceSystem, vulkanContext.getDevice());

    // Cleanup descriptor sets
    descriptorSetManager.destroy(vulkanContext.getDevice());

    // Cleanup multipass pipeline
    multiPassPipeline.cleanup();

    // Cleanup pipelines
    if (fullscreenPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanContext.getDevice(), fullscreenPipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanContext.getDevice(), pipelineLayout, nullptr);
    }

    // Cleanup swapchain sampler
    if (swapchainSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkanContext.getDevice(), swapchainSampler, nullptr);
    }

    // Cleanup framebuffers
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(vulkanContext.getDevice(), framebuffer, nullptr);
    }

    // Cleanup render pass
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkanContext.getDevice(), renderPass, nullptr);
    }

    // Cleanup vertex buffer
    if (vertexBufferHandle.type != ResourceType::Unknown) {
        resourceSystem.destroy(vertexBufferHandle);
    }

    // Cleanup resource system (frees MemoryAllocator blocks)
    resourceSystem.cleanup();

    // VulkanContext will clean up its own resources in its destructor
}
