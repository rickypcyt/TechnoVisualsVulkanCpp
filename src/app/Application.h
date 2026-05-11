#pragma once

#include <memory>
#include <chrono>
#include <deque>
#include "../core/Window.h"
#include "../core/VulkanContext.h"
#include "../gfx/ResourceSystem.h"
#include "../gfx/FrameSystem.h"
#include "../video/VideoTexture.h"
#include "../video/VideoPlayer.h"
#include "../video/VideoRegistry.h"
#include "../video/VideoRenderer.h"
#include "../render/UniformBufferManager.h"
#include "../render/DescriptorSetManager.h"
#include "../render/ShaderCompiler.h"
#include "../render/MultiPassPipeline.h"
#include "../app/UISystem.h"
#include "../app/VisualControls.h"
#include "../app/ControlState.h"
#include "../app/PlaybackClock.h"
#include "../app/ProjectState.h"
#include "../app/Timeline.h"
#include "../render/EffectChain.h"
#include "../render/Export.h"
#include "../render/RenderJob.h"

// Define VideoRandomizerState since it's only forward-declared in UISystem.h
struct VideoRandomizerState {
    bool autoRandomize = false;
    bool useVideoDuration = false;
    float intervalSeconds = 30.0f;
    float elapsedSeconds = 0.0f;
    float currentVideoDuration = 0.0f;
    int historyWindow = 3;
    std::deque<int> recentHistory;
};

class Application {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr int WIDTH = 800;
    static constexpr int HEIGHT = 600;

    Application();
    ~Application();

    void run();

private:
    // Core systems
    Window window;
    VulkanContext vulkanContext;
    ResourceSystem resourceSystem;
    FrameSystem frameSystem;

    // Video systems
    VideoTexture videoTexture;
    VideoPlayer videoPlayer;
    VideoRegistry videoRegistry;
    std::unique_ptr<VideoRenderer> videoRenderer;
    std::string videoAssetsRoot = "mp4s";
    std::string videoSourcePath = "media/sample.mp4";
    int selectedVideoAsset = -1;
    bool videoSubsystemInitialized = false;

    // Rendering systems
    UniformBufferManager uniformBufferManager;
    DescriptorSetManager descriptorSetManager;
    MultiPassPipeline multiPassPipeline;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline fullscreenPipeline = VK_NULL_HANDLE;
    VkSampler swapchainSampler = VK_NULL_HANDLE;
    ResourceHandle vertexBufferHandle;

    // UI and controls
    UISystem uiSystem;
    VisualControls visualControls;
    VideoRandomizerState videoRandomizer;
    std::string controlStatePath = "controls_state.cfg";
    bool controlsDirty = false;
    bool allowDimensionChangeRecreation = false;
    float transitionDuration = 1.0f;
    std::chrono::steady_clock::time_point lastControlSaveTime;
    std::mt19937 rng{std::random_device{}()};

    // NLE components
    EffectChain currentEffectChain;
    Timeline timeline;
    PlaybackClock playbackClock;
    std::unique_ptr<RenderWorker> renderWorker;

    // State
    bool running = true;
    bool initializationComplete = false;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastFrameTimestamp;
    
    // Command buffers
    std::vector<VkCommandBuffer> commandBuffers;

    // Initialization methods
    void initVulkan();
    void initSwapchain();
    void initRenderPass();
    void initPipelines();
    void initFramebuffers();
    void initCommandBuffers();
    void initVideo();
    void initUI();
    void initNLE();
    void initMultiPassPipeline();

    // Main loop
    void mainLoop();
    void updateUniformBuffer(uint32_t frameIndex);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, FrameContext& frame);

    // Cleanup
    void cleanup();
};
