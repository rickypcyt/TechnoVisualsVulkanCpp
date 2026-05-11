#include "Application.h"
#include <stdexcept>
#include <iostream>
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
    
    // Initialize command buffers
    initCommandBuffers();

    // Initialize frame system
    frameSystem.init(vulkanContext.getDevice(), MAX_FRAMES_IN_FLIGHT, vulkanContext.getSwapchainImageCount());
    std::cout << "[Application] FrameSystem initialized successfully" << std::endl;
    
    // Set start time
    startTime = std::chrono::steady_clock::now();
    lastControlSaveTime = startTime;
    lastFrameTimestamp = startTime;
    initializationComplete = true;
    
    // Load control state
    ControlState::load(controlStatePath, visualControls, videoRandomizer, allowDimensionChangeRecreation);
    
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
    videoRegistry.scan(videoAssetsRoot);
    const auto& assets = videoRegistry.getAssets();
    if (!assets.empty()) {
        selectedVideoAsset = 0;
        videoSourcePath = assets[0].metadata.path;
    }
    
    // Initialize video player
    int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
    int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
    
    if (!videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
        std::cerr << "[Application] Failed to initialize video player" << std::endl;
        return;
    }
    
    // Create video texture resources
    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                 vulkanContext.getCommandPool(),
                                 vulkanContext.getGraphicsQueue(),
                                 static_cast<uint32_t>(videoPlayer.width()),
                                 static_cast<uint32_t>(videoPlayer.height()));

    videoSubsystemInitialized = videoTexture.isReady();

    // Initialize video renderer
    if (videoSubsystemInitialized) {
        videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture);
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
    };
}

void Application::mainLoop() {
    while (running) {
        // Handle SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Pass event to UI system
            uiSystem.processEvent(event);

            // Handle window close
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }

            // Handle window resize
            if (event.type == SDL_WINDOWEVENT) {
                SDL_Window* sourceWindow = SDL_GetWindowFromID(event.window.windowID);
                if (sourceWindow == window.getMainWindow() &&
                    (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
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
                        videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
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
        
        // Update uniform buffer
        updateUniformBuffer(frame.frameIndex);

        // Update video texture
        if (videoRenderer) {
            videoRenderer->update(deltaTime, frame.frameIndex);
        }

        // Render UI
        UIDiagnostics diag;
        diag.swapchainWidth = vulkanContext.getSwapchainExtent().width;
        diag.swapchainHeight = vulkanContext.getSwapchainExtent().height;
        diag.videoReady = videoSubsystemInitialized && videoTexture.isReady();
        diag.videoWidth = videoTexture.getWidth();
        diag.videoHeight = videoTexture.getHeight();
        
        UICallbacks callbacks;
        callbacks.onControlsChanged = [this]() { controlsDirty = true; };
        callbacks.onRandomizeVideo = [this]() {
            const auto& assets = videoRegistry.getAssets();
            if (assets.size() > 1) {
                // Select a random video different from current
                std::uniform_int_distribution<int> dist(0, static_cast<int>(assets.size()) - 1);
                int newIndex;
                do {
                    newIndex = dist(rng);
                } while (newIndex == selectedVideoAsset);
                
                selectedVideoAsset = newIndex;
                videoSourcePath = assets[newIndex].metadata.path;
                
                // Reload video
                videoPlayer.shutdown();
                int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
                int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
                if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                    vkDeviceWaitIdle(vulkanContext.getDevice());
                    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                    videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                                 vulkanContext.getCommandPool(),
                                                 vulkanContext.getGraphicsQueue(),
                                                 static_cast<uint32_t>(videoPlayer.width()),
                                                 static_cast<uint32_t>(videoPlayer.height()));
                    
                    // Update descriptor sets with new textures
                    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                        descriptorSetManager.updateSet(
                            vulkanContext.getDevice(), i,
                            uniformBufferManager.getBuffers()[i],
                            const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
                            const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
                        );
                    }
                    
                    videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture);
                    videoRandomizer.elapsedSeconds = 0.0f;
                    videoRandomizer.currentVideoDuration = videoPlayer.durationSeconds();
                }
            }
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
        callbacks.onReloadVideo = [this](const std::string& path) {
            videoSourcePath = path;
            videoPlayer.shutdown();
            int screenW = static_cast<int>(vulkanContext.getSwapchainExtent().width);
            int screenH = static_cast<int>(vulkanContext.getSwapchainExtent().height);
            if (videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
                vkDeviceWaitIdle(vulkanContext.getDevice());
                videoTexture.destroy(resourceSystem, vulkanContext.getDevice());
                videoTexture.createResources(resourceSystem, vulkanContext.getDevice(),
                                             vulkanContext.getCommandPool(),
                                             vulkanContext.getGraphicsQueue(),
                                             static_cast<uint32_t>(videoPlayer.width()),
                                             static_cast<uint32_t>(videoPlayer.height()));
                
                // Update descriptor sets with new textures
                for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                    descriptorSetManager.updateSet(
                        vulkanContext.getDevice(), i,
                        uniformBufferManager.getBuffers()[i],
                        const_cast<VkDescriptorImageInfo*>(&videoTexture.getDescriptorInfo()),
                        const_cast<VkDescriptorImageInfo*>(&videoTexture.getPrevDescriptorInfo())
                    );
                }
                
                videoRenderer = std::make_unique<VideoRenderer>(videoPlayer, videoTexture);
                videoRandomizer.elapsedSeconds = 0.0f;
                videoRandomizer.currentVideoDuration = videoPlayer.durationSeconds();
            }
        };
        
        uiSystem.render(visualControls, videoRandomizer, videoPlayer, videoRegistry,
                       selectedVideoAsset, transitionDuration, allowDimensionChangeRecreation,
                       controlsDirty, rng, diag, callbacks);
        
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
    GlobalUBO ubo{};
    auto currentTime = std::chrono::steady_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();
    
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
    
    // Set visual control values
    ubo.primaryColor = visualControls.primaryColor;
    ubo.secondaryColor = visualControls.secondaryColor;
    ubo.colorBlend = visualControls.colorBlend;
    ubo.tempo = visualControls.tempo;
    ubo.energy = visualControls.energy;
    ubo.bass = visualControls.bass;
    ubo.mid = visualControls.mid;
    ubo.high = visualControls.high;
    
    // Post FX basicos
    ubo.grayscaleAmount = visualControls.grayscaleAmount;
    ubo.sharpenAmount = visualControls.sharpenAmount;
    ubo.upscaleEnabled = visualControls.upscaleEnabled ? 1.0f : 0.0f;
    
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
    
    uniformBufferManager.update(frameIndex, ubo);
}

void Application::recordCommandBuffer(VkCommandBuffer commandBuffer, FrameContext& frame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer");
    }

    // Record video texture upload
    videoTexture.recordPendingUpload(commandBuffer, frame.frameIndex, vulkanContext.getGraphicsQueue());

    // Begin render pass
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
    
    // Bind pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fullscreenPipeline);
    
    // Bind descriptor set
    VkDescriptorSet descriptorSet = descriptorSetManager.getSet(frame.frameIndex);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                           0, 1, &descriptorSet, 0, nullptr);

    // Set viewport and scissor
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

    // Draw fullscreen quad
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer");
    }
}

void Application::cleanup() {
    if (!initializationComplete) {
        return;
    }

    vkDeviceWaitIdle(vulkanContext.getDevice());

    // Save control state
    ControlState::save(controlStatePath, visualControls, videoRandomizer, allowDimensionChangeRecreation);

    // Shutdown UI
    uiSystem.shutdown();

    // Cleanup frame system (semaphores and fences)
    frameSystem.cleanup();

    // Cleanup video
    videoPlayer.shutdown();
    videoTexture.destroy(resourceSystem, vulkanContext.getDevice());

    // Cleanup uniform buffers
    uniformBufferManager.destroy(resourceSystem, vulkanContext.getDevice());

    // Cleanup descriptor sets
    descriptorSetManager.destroy(vulkanContext.getDevice());

    // Cleanup pipelines
    if (fullscreenPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkanContext.getDevice(), fullscreenPipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkanContext.getDevice(), pipelineLayout, nullptr);
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

    // VulkanContext and ResourceSystem will clean up their own resources in their destructors
}
