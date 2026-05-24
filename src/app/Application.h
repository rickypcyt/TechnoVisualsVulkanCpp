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
#include "../video/CpuFramePool.h"
#include "../render/UniformBufferManager.h"
#include "../render/DescriptorSetManager.h"
#include "../render/ShaderCompiler.h"
#include "../render/MultiPassPipeline.h"
#include "../app/UISystem.h"
#include "../app/VisualControls.h"
#include "../app/ControlState.h"
#include "../app/MidiSystem.h"
#include "../app/OscSystem.h"
#include "../app/AudioSystem.h"
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
    bool useShuffleMode = true; // New: use shuffle to ensure all videos play before repeat
    std::vector<int> shuffleQueue; // New: shuffled order of video indices
    int currentShuffleIndex = 0; // New: current position in shuffle queue
};

// VideoRandomizerState for Video 2
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
    CpuFramePool cpuFramePool;
    std::unique_ptr<VideoRenderer> videoRenderer;
    std::string videoAssetsRoot = "mp4s";
    std::string videoSourcePath = "media/sample.mp4";
    int selectedVideoAsset = -1;
    bool videoSubsystemInitialized = false;
    bool isReloadingVideo = false;
    bool pendingNLEReload = false;
    std::chrono::steady_clock::time_point lastVideoChangeTime = std::chrono::steady_clock::now();
    const std::chrono::milliseconds videoChangeCooldown{500};

    // Video 2 systems (dual source)
    VideoTexture videoTexture2;
    VideoPlayer videoPlayer2;
    CpuFramePool cpuFramePool2;
    std::unique_ptr<VideoRenderer> videoRenderer2;
    std::string videoSourcePath2 = "media/sample.mp4";
    int selectedVideoAsset2 = -1;
    bool videoSubsystemInitialized2 = false;
    bool isReloadingVideo2 = false;
    std::chrono::steady_clock::time_point lastVideoChangeTime2 = std::chrono::steady_clock::now();

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

    // Fullscreen descriptor set (separate from multipass - only 2 bindings)
    VkDescriptorSetLayout fullscreenDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool fullscreenDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> fullscreenDescriptorSets;

    // UI and controls
    UISystem uiSystem;
    VisualControls visualControls;
    VideoRandomizerState videoRandomizer;
    VideoRandomizerState2 videoRandomizer2;
    MidiSystem midiSystem;
    OscSystem oscSystem;
    AudioSystem audioSystem;
    std::string controlStatePath = "controls_state.cfg";
    bool controlsDirty = false;
    bool allowDimensionChangeRecreation = false;
    float transitionDuration = 1.0f;
    float transitionDuration2 = 0.5f;
    float transitionProgress = 0.0f;  // 0.0 to 1.0, current transition progress
    bool transitionActive = false;    // whether a transition is in progress
    bool transitionFromV1toV2 = true; // transition direction
    float transitionStartMix = 0.0f;
    float transitionEndMix = 1.0f;
    std::chrono::steady_clock::time_point transitionStartTime;
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
    std::chrono::steady_clock::time_point lastRandomJumpTime;
    std::chrono::high_resolution_clock::time_point lastGlobalTime;
    float accumulatedTime = 0.0f;
    float debugAnimationTime = 0.0f;
    float debugAnimationDelta = 0.0f;
    float debugAnimationElapsedSeconds = 0.0f;
    bool animationTimeInitialized = false;
    
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
    void updateFullscreenDescriptorSets();
    void initUI();
    void initNLE();
    void initMultiPassPipeline();
    void initMidi();
    void initOsc();
    void initAudio();

    // OSC trigger action handlers
    void handleOscTrigger(const std::string& action);
    bool canChangeVideo() {
        if (isReloadingVideo) return false;
        auto now = std::chrono::steady_clock::now();
        if (now - lastVideoChangeTime < videoChangeCooldown) return false;
        isReloadingVideo = true;
        lastVideoChangeTime = now;
        return true;
    }

    int pickNextVideoIndex(const std::vector<VideoAsset>& assets);
    bool reloadVideoAtIndex(int newIndex, const std::vector<VideoAsset>& assets);
    
    int pickNextVideoIndex2(const std::vector<VideoAsset>& assets);
    bool reloadVideoAtIndex2(int newIndex, const std::vector<VideoAsset>& assets);

    // Main loop
    void mainLoop();
    void updateUniformBuffer(uint32_t frameIndex);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, FrameContext& frame);

    // Cleanup
    void cleanup();
};
