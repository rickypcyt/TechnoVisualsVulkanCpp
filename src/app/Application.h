#pragma once

#include <memory>
#include <chrono>
#include <deque>
#include <queue>
#include <mutex>
#include <random>
#include <numeric>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <glm/glm.hpp>

#include "../core/Window.h"
#include "../core/VulkanContext.h"
#include "../core/VulkanPresenter.h"
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
#include "../app/parameters/ParameterRegistry.h"


// ------------------------------
// Randomizer (unificado)
// ------------------------------
struct VideoRandomizerState {
    bool autoRandomize = false;
    bool useVideoDuration = false;
    float intervalSeconds = 30.0f;
    float elapsedSeconds = 0.0f;
    float currentVideoDuration = 0.0f;

    bool useShuffleMode = true;

    std::deque<int> recentHistory;
    int historyWindow = 3;

    std::vector<int> shuffleQueue;
    int currentShuffleIndex = 0;
};

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


// ------------------------------
// Slot genérico de video (CRÍTICO)
// ------------------------------
struct VideoSlot {
    VideoTexture* texture = nullptr;
    VideoPlayer* player = nullptr;
    CpuFramePool* framePool = nullptr;
    std::unique_ptr<VideoRenderer>* renderer = nullptr;

    std::string* sourcePath = nullptr;
    VideoRandomizerState* randomizer = nullptr;

    bool* initialized = nullptr;
    bool* isReloading = nullptr;
    std::chrono::steady_clock::time_point* lastChangeTime = nullptr;
};


// ------------------------------
// Application
// ------------------------------
class Application {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
    static constexpr int WIDTH = 800;
    static constexpr int HEIGHT = 600;

    Application();
    ~Application();

    void run();

    // Presets
    std::vector<std::string> listPresets() const;
    bool savePreset(const std::string& name);
    bool loadPreset(const std::string& name);
    bool deletePreset(const std::string& name);
    std::string presetsDir = "presets";

private:

    // --------------------------
    // Core
    // --------------------------
    Window window;
    VulkanContext vulkanContext;
    ResourceSystem resourceSystem;
    VulkanPresenter previewPresenter;
    VulkanPresenter outputPresenter;
    FrameSystem previewFrameSystem;
    FrameSystem outputFrameSystem;


    // --------------------------
    // Video slot 1
    // --------------------------
    VideoTexture videoTexture;
    VideoPlayer videoPlayer;
    VideoRegistry videoRegistry;
    CpuFramePool cpuFramePool;
    std::unique_ptr<VideoRenderer> videoRenderer;

    std::string videoSourcePath = "media/sample.mp4";
    int selectedVideoAsset = -1;
    bool videoSubsystemInitialized = false;
    bool isReloadingVideo = false;
    std::chrono::steady_clock::time_point lastVideoChangeTime =
        std::chrono::steady_clock::now();


    // --------------------------
    // Video slot 2 (SIMPLIFICADO)
    // --------------------------
    VideoTexture videoTexture2;
    VideoPlayer videoPlayer2;
    CpuFramePool cpuFramePool2;
    std::unique_ptr<VideoRenderer> videoRenderer2;

    std::string videoSourcePath2 = "media/sample.mp4";
    int selectedVideoAsset2 = -1;
    bool videoSubsystemInitialized2 = false;
    bool isReloadingVideo2 = false;
    std::chrono::steady_clock::time_point lastVideoChangeTime2 =
        std::chrono::steady_clock::now();

    // --------------------------
    // Video slot 3
    // --------------------------
    VideoTexture videoTexture3;
    VideoPlayer videoPlayer3;
    CpuFramePool cpuFramePool3;
    std::unique_ptr<VideoRenderer> videoRenderer3;

    std::string videoSourcePath3 = "media/sample.mp4";
    int selectedVideoAsset3 = -1;
    bool videoSubsystemInitialized3 = false;
    bool isReloadingVideo3 = false;
    std::chrono::steady_clock::time_point lastVideoChangeTime3 =
        std::chrono::steady_clock::now();

    // --------------------------
    // Output Video slot 1
    // --------------------------
    VideoTexture outputVideoTexture;
    VideoPlayer outputVideoPlayer;
    CpuFramePool outputCpuFramePool;
    std::unique_ptr<VideoRenderer> outputVideoRenderer;

    std::string outputVideoSourcePath = "media/sample.mp4";
    int outputSelectedVideoAsset = -1;
    bool outputVideoSubsystemInitialized = false;
    bool outputIsReloadingVideo = false;
    std::chrono::steady_clock::time_point outputLastVideoChangeTime =
        std::chrono::steady_clock::now();

    // --------------------------
    // Output Video slot 2
    // --------------------------
    VideoTexture outputVideoTexture2;
    VideoPlayer outputVideoPlayer2;
    CpuFramePool outputCpuFramePool2;
    std::unique_ptr<VideoRenderer> outputVideoRenderer2;

    std::string outputVideoSourcePath2 = "media/sample.mp4";
    int outputSelectedVideoAsset2 = -1;
    bool outputVideoSubsystemInitialized2 = false;
    bool outputIsReloadingVideo2 = false;
    std::chrono::steady_clock::time_point outputLastVideoChangeTime2 =
        std::chrono::steady_clock::now();

    // --------------------------
    // Output Video slot 3
    // --------------------------
    VideoTexture outputVideoTexture3;
    VideoPlayer outputVideoPlayer3;
    CpuFramePool outputCpuFramePool3;
    std::unique_ptr<VideoRenderer> outputVideoRenderer3;

    std::string outputVideoSourcePath3 = "media/sample.mp4";
    int outputSelectedVideoAsset3 = -1;
    bool outputVideoSubsystemInitialized3 = false;
    bool outputIsReloadingVideo3 = false;
    std::chrono::steady_clock::time_point outputLastVideoChangeTime3 =
        std::chrono::steady_clock::now();

    VideoRandomizerState outputVideoRandomizer;
    VideoRandomizerState2 outputVideoRandomizer2;
    VideoRandomizerState2 outputVideoRandomizer3;

    // Per-video playback speed cache (path → rate)
    mutable std::unordered_map<std::string, float> videoSpeedCache;
    std::string videoSpeedCachePath = "video_speeds.json";
    float lastPlaybackRate  = 1.0f;
    float lastPlaybackRate2 = 1.0f;
    float lastPlaybackRate3 = 1.0f;
    void loadVideoSpeeds();
    void saveVideoSpeeds() const;

    // --------------------------
    // Rendering
    // --------------------------
    UniformBufferManager uniformBufferManager;
    UniformBufferManager outputUniformBufferManager;
    DescriptorSetManager descriptorSetManager;
    DescriptorSetManager outputDescriptorSetManager;
    MultiPassPipeline multiPassPipeline;
    MultiPassPipeline outputMultiPassPipeline;

    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline fullscreenPipeline = VK_NULL_HANDLE;

    VkSampler swapchainSampler = VK_NULL_HANDLE;
    ResourceHandle vertexBufferHandle;


    // --------------------------
    // Fullscreen descriptors
    // --------------------------
    VkDescriptorSetLayout fullscreenDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool fullscreenDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> fullscreenDescriptorSets;

    VkDescriptorPool outputFullscreenDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> outputFullscreenDescriptorSets;

    // Output live toggle (Enter key)
    bool outputLive = false;
    // Preview pause toggle (Space key)
    bool previewPaused = false;

    VisualControls outputVisualControls;

    struct AnimState {
        bool timeInitialized = false;
        std::chrono::high_resolution_clock::time_point lastGlobalTime;
        float accumulatedTime = 0.0f;
        float debugElapsed = 0.0f;
        float debugDelta = 0.0f;
        float shaderTime = 0.0f;
    };
    AnimState previewAnim;
    AnimState outputAnim;


    // --------------------------
    // UI / Control
    // --------------------------
    UISystem uiSystem;
    VisualControls visualControls;

    VideoRandomizerState videoRandomizer;
    VideoRandomizerState2 videoRandomizer2;
    VideoRandomizerState2 videoRandomizer3;

    MidiSystem midiSystem;
    OscSystem oscSystem;
    AudioSystem audioSystem;
    ParameterRegistry parameterRegistry;

    std::string controlStatePath = "controls_state.cfg";
    std::string visualControlsPath = "visual_controls.json";


    // --------------------------
    // NLE / Timeline
    // --------------------------
    EffectChain currentEffectChain;
    Timeline timeline;
    PlaybackClock playbackClock;

    std::unique_ptr<RenderWorker> renderWorker;


    // --------------------------
    // State
    // --------------------------
    bool running = true;
    bool initializationComplete = false;

    std::mt19937 rng{ std::random_device{}() };

    // timing / state
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastControlSaveTime;
    std::chrono::steady_clock::time_point lastFrameTimestamp;
    std::chrono::steady_clock::time_point lastRandomJumpTime;
    std::chrono::high_resolution_clock::time_point lastGlobalTime;

    float accumulatedTime = 0.0f;
    int   frameCount = 0;
    float fpsAccumTime = 0.0f;
    float currentFps = 0.0f;
    float debugAnimationTime = 0.0f;
    float debugAnimationDelta = 0.0f;
    float debugAnimationElapsedSeconds = 0.0f;

    bool animationTimeInitialized = false;

    // resize debounce (fixes tiling compositors like Hyprland)
    bool resizePending = false;
    std::chrono::steady_clock::time_point resizeDebounceTime;
    uint32_t pendingResizeW = 0;
    uint32_t pendingResizeH = 0;

    // video / control flags
    bool allowDimensionChangeRecreation = false;
    bool controlsDirty = false;
    bool transitionActive = false;

    // video assets
    std::string videoAssetsRoot = "mp4s";

    // transition durations
    float transitionDuration = 1.0f;
    float transitionDuration2 = 0.5f;
    float transitionDuration3 = 0.5f;
    float transitionProgress = 1.0f;  // 0.0 = old video, 1.0 = new video

    // command buffers
    std::vector<VkCommandBuffer> commandBuffers;

    // async render jobs
    std::mutex completedRenderJobsMutex;
    std::queue<std::shared_ptr<RenderJob>> completedRenderJobs;


    // --------------------------
    // Helpers
    // --------------------------
    template<typename T>
    int pickNextIndex(const std::vector<VideoAsset>& assets,
                      T& rz,
                      int currentIdx) {

        if (assets.size() <= 1) return 0;

        if (rz.useShuffleMode) {
            if (rz.shuffleQueue.empty() ||
                rz.currentShuffleIndex >= (int)rz.shuffleQueue.size()) {

                rz.shuffleQueue.resize(assets.size());
                std::iota(rz.shuffleQueue.begin(),
                           rz.shuffleQueue.end(),
                           0);

                std::shuffle(rz.shuffleQueue.begin(),
                             rz.shuffleQueue.end(),
                             rng);

                rz.currentShuffleIndex = 0;
            }

            return rz.shuffleQueue[rz.currentShuffleIndex++ %
                                  rz.shuffleQueue.size()];
        }

        std::uniform_int_distribution<int> dist(0, (int)assets.size() - 1);

        int idx;
        do {
            idx = dist(rng);
        } while (idx == currentIdx && assets.size() > 1);

        return idx;
    }


    // --------------------------
    // Core methods
    // --------------------------
    void initVulkan();
    void initPresenters();
    void initRenderPass();
    void initPipelines();
    void initFramebuffers();
    void initCommandBuffers();

    void initVideo();
    void initOutputVideo();
    void updateFullscreenDescriptorSets();

    void initUI();
    void initNLE();

    void initMultiPassPipeline();
    void initMidi();
    void initOsc();
    void initAudio();

    void handleCompletedRenderJob(const std::shared_ptr<RenderJob>& job);

    void handleOscTrigger(const std::string& action);


    // --------------------------
    // Video control
    // --------------------------
    bool canChangeVideo() const;

    int pickNextVideoIndex(const std::vector<VideoAsset>& assets);
    int pickNextVideoIndex2(const std::vector<VideoAsset>& assets);
    int pickNextVideoIndex3(const std::vector<VideoAsset>& assets);

    bool reloadVideoAtIndex(int newIndex,
                            const std::vector<VideoAsset>& assets);

    bool reloadVideoAtIndex2(int newIndex,
                             const std::vector<VideoAsset>& assets);

    bool reloadVideoAtIndex3(int newIndex,
                             const std::vector<VideoAsset>& assets);


    // --------------------------
    // Helpers
    // --------------------------
    glm::ivec2 getScreenSize() const;

    void rebuildVideoTexture(int slot);
    void updateAllDescriptorSets();

    bool reloadVideoSlot(int slot, const std::string& path);
    bool reloadOutputVideoSlot(int slot, const std::string& path);

    void saveState() const;

    void handleWindowResize(uint32_t width, uint32_t height);

    void tickAutoColors(float dt, float energy);

    void tickAutoRandomize(float dt,
                           const std::chrono::steady_clock::time_point& now);

    UICallbacks buildUICallbacks();


    // --------------------------
    // Main loop
    // --------------------------
    void mainLoop();
    void updateUniformBuffer(uint32_t frameIndex, VisualControls& controls,
                            UniformBufferManager& uboManager,
                            const VulkanPresenter& presenter,
                            AnimState& anim,
                            VideoTexture& vid1, VideoTexture& vid2, VideoTexture& vid3,
                            bool vid1Init, bool vid2Init, bool vid3Init);
    void recordCommandBuffer(VkCommandBuffer commandBuffer,
                             FrameContext& previewFrame, uint32_t previewImageIndex,
                             FrameContext& outputFrame, uint32_t outputImageIndex);


    // --------------------------
    // Cleanup
    // --------------------------
    void cleanup();
};