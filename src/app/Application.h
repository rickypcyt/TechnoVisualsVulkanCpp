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
    std::vector<int> shuffleQueue;
    int currentShuffleIndex = 0;
};


// ------------------------------
// Slot genérico de video (CRÍTICO)
// ------------------------------
struct VideoSlot {
    VideoTexture texture;
    VideoPlayer player;
    CpuFramePool framePool;
    std::unique_ptr<VideoRenderer> renderer;

    std::string sourcePath = "media/sample.mp4";
    VideoRandomizerState randomizer;

    int selectedVideoAsset = -1;
    bool initialized = false;
    bool isReloading = false;
    std::chrono::steady_clock::time_point lastChangeTime =
        std::chrono::steady_clock::now();

    float transitionDuration = 1.0f;
    float lastPlaybackRate = 1.0f;
};


// ------------------------------
// Application
// ------------------------------
class Application {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;

    Application();
    ~Application();

    void run();

    // Presets
    std::vector<std::string> listPresets() const;
    bool savePreset(const std::string& name);
    bool savePresetFromPreview(const std::string& name);
    bool savePresetFromOutput(const std::string& name);
    bool loadPreset(const std::string& name);
    bool deletePreset(const std::string& name);
    bool renamePreset(const std::string& oldName, const std::string& newName);
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
    // Video slots (preview + output)
    // --------------------------
    VideoRegistry videoRegistry;
    std::array<VideoSlot, 3> previewSlots;
    std::array<VideoSlot, 3> outputSlots;

    // Per-video playback speed cache (path → rate)
    mutable std::unordered_map<std::string, float> videoSpeedCache;
    std::string videoSpeedCachePath = "video_speeds.json";

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

    // Temporal smoothing state for audio values
    struct AudioSmoothState {
        float energy = 0.0f;
        float bass = 0.0f;
        float mid = 0.0f;
        float high = 0.0f;
    };
    AudioSmoothState prevAudioSmooth;      // for preview
    AudioSmoothState outputPrevAudioSmooth; // for output

    float autoColorHue = 0.0f; // hue rotation state for tickAutoColors (was buggy static)


    // --------------------------
    // UI / Control
    // --------------------------
    UISystem uiSystem;
    VisualControls visualControls;

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

    uint32_t previewFrameCounter = 0; // 30fps throttle for preview UBO updates
    float currentDeltaTime = 0.0f;    // set by renderOneFrame, used by tryRender*

    // resize debounce (fixes tiling compositors like Hyprland)
    bool resizePending = false;
    bool outputResizePending = false;
    int consecutiveRenderErrors = 0;
    std::chrono::steady_clock::time_point resizeDebounceTime;
    uint32_t pendingResizeW = 0;
    uint32_t pendingResizeH = 0;
    uint32_t pendingOutputResizeW = 0;
    uint32_t pendingOutputResizeH = 0;

    // window state tracking
    bool previewWindowMinimized = false;
    bool outputWindowMinimized = false;
    bool outputWindowVisible = true;
    bool outputWindowHidden = false;
    bool inModalLoop = false;      // Win32 modal drag/resize loop active
    bool modalPreviewResize = false;
    bool modalOutputResize = false;
    uintptr_t modalTimerId = 0;    // Win32 timer ID for modal loop rendering
    bool renderingInProgress = false; // Re-entrancy guard for renderOneFrame

    // video / control flags
    bool allowDimensionChangeRecreation = false;
    bool controlsDirty = false;
    bool transitionActive = false;

    // video assets
    std::string videoAssetsRoot;

    float transitionProgress = 1.0f;  // 0.0 = old video, 1.0 = new video

    // command buffers (separate per window for independent rendering)
    std::vector<VkCommandBuffer> previewCommandBuffers;
    std::vector<VkCommandBuffer> outputCommandBuffers;

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
    void createFullscreenDescriptorSets();
    void createFullscreenPipeline();
    void createSwapchainSampler();
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

    void handleOscTrigger(const std::string& action, int arg = -1);

    // Commit preview state (controls + video paths) to the output renderer
    void commitPreviewToOutput();


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
    void handleOutputWindowResize(uint32_t width, uint32_t height);
    void processPendingResizes();

    void tickAutoColors(float dt, float energy);

    void tickAutoRandomize(float dt,
                           const std::chrono::steady_clock::time_point& now);

    UICallbacks buildUICallbacks();
    UICallbacks cachedCallbacks;


    // --------------------------
    // Main loop
    // --------------------------
    void mainLoop();
    void processSDLEvents();
    void frameLimit();
#ifdef _WIN32
    static void CALLBACK modalTimerProc(HWND hwnd, UINT message, UINT_PTR timerId, DWORD timestamp);
#endif
    void renderOneFrame();
    bool tryRenderPreview();
    bool tryRenderOutput();
    bool submitAndPresent(FrameSystem& frameSystem, VulkanPresenter& presenter,
                          FrameContext* frame, uint32_t imageIndex,
                          VkCommandBuffer cmdBuffer,
                          std::function<void()> onResizeNeeded);
    void updateUniformBuffer(uint32_t frameIndex, VisualControls& controls,
                            UniformBufferManager& uboManager,
                            const VulkanPresenter& presenter,
                            AnimState& anim,
                            VideoTexture& vid1, VideoTexture& vid2, VideoTexture& vid3,
                            bool vid1Init, bool vid2Init, bool vid3Init,
                            bool forOutput = false);
    void recordPreviewCommandBuffer(VkCommandBuffer cmd,
                                     FrameContext& previewFrame, uint32_t previewImageIndex);
    void recordOutputCommandBuffer(VkCommandBuffer cmd,
                                    FrameContext& outputFrame, uint32_t outputImageIndex);


    // --------------------------
    // Cleanup
    // --------------------------
    void cleanup();
};