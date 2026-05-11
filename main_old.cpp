#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>

#include <iostream>
#include <stdexcept>
#include <vector>
#include <set>
#include <array>
#include <algorithm>
#include <limits>
#include <fstream>
#include <string>
#include <cstring>
#include <cstddef>
#include <functional>
#include <utility>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <mutex>
#include <optional>
#include <filesystem>
#include <system_error>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <random>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace fs = std::filesystem;

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "render/EffectChain.h"
#include "render/GlobalUBO.h"
#include "app/UISystem.h"
#include "app/ControlState.h"
#include "app/Timeline.h"
#include "render/Export.h"
#include "app/PlaybackClock.h"
#include "app/ProjectState.h"
#include "render/RenderJob.h"
#include "render/MultiPassPipeline.h"
#include "app/VisualControls.h"
#include "video/VideoPlayer.h"
#include "video/VideoRegistry.h"
#include "video/VideoStaging.h"
#include "gfx/FrameSystem.h"
#include "gfx/MemoryAllocator.h"
#include "gfx/ResourceSystem.h"

const int WIDTH = 800;
const int HEIGHT = 600;
const int MAX_FRAMES_IN_FLIGHT = 2;
const size_t MAX_VIDEO_FRAME_BUFFER = 48;
const int MAX_VIDEO_OUTPUT_HEIGHT = 2160;  // Support 4K resolution
const std::array<int, 5> FORCED_FPS_OPTIONS = {0, 15, 24, 30, 60};
const std::string imguiIniFilename = "imgui.ini";
constexpr bool kEnableVideoTrace = false;

// Signal handler for crashes
#include <csignal>
#ifdef __linux__
#include <execinfo.h>
#endif

void crash_handler(int sig) {
#ifdef __linux__
    void* array[10];
    size_t size = backtrace(array, 10);

    std::cerr << "\n[CRASH] Signal " << sig << " caught!" << std::endl;
    std::cerr << "[CRASH] Backtrace:" << std::endl;
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    std::cerr << std::endl;
#else
    std::cerr << "\n[CRASH] Signal " << sig << " caught!" << std::endl;
#endif

    exit(1);
}

template<typename... Args>
inline void videoTrace(Args&&... args) {
    if constexpr (kEnableVideoTrace) {
        (std::cout << ... << args);
        std::cout << std::endl;
    }
}

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

namespace {
void ensureFFmpegInitialized() {
    static std::once_flag ffmpegInitFlag;
    std::call_once(ffmpegInitFlag, []() {
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
    });
}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {

    std::cerr << "[VULKAN] " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}

bool checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool found = false;

        for (const auto& layerProps : availableLayers) {
            if (strcmp(layerName, layerProps.layerName) == 0) {
                found = true;
                break;
            }
        }

        if (!found) return false;
    }

    return true;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) {

    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

struct Vertex {
    float pos[2];
    float color[3];
};

const std::vector<Vertex> vertices = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
};

struct RenderPass {
    virtual void execute(VkCommandBuffer commandBuffer, FrameContext& frame) = 0;
    virtual void onResize() {}
    virtual ~RenderPass() = default;
};

struct RenderNode {
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::function<void(VkCommandBuffer, FrameContext&)> execute;
};

class RenderGraph {
public:
    void addNode(const RenderNode& node) {
        nodes.push_back(node);
    }

    void execute(VkCommandBuffer commandBuffer, FrameContext& frame) {
        for (auto& node : nodes) {
            node.execute(commandBuffer, frame);
        }
    }

private:
    std::vector<RenderNode> nodes;
};

class Renderer {
public:
    void record(VkCommandBuffer commandBuffer, FrameContext& frame) {
        graph.execute(commandBuffer, frame);
    }

    void addNode(const RenderNode& node) {
        graph.addNode(node);
    }

private:
    RenderGraph graph;
};

class TrianglePass : public RenderPass {
public:
    TrianglePass() = default;

    void setup(VkRenderPass* renderPass,
               std::vector<VkFramebuffer>* framebuffers,
               VkExtent2D* extent,
               VkPipeline* pipeline,
               VkPipelineLayout* pipelineLayout,
               ResourceHandle* vertexBuffer,
               std::vector<VkDescriptorSet>* descriptorSets) {
        this->renderPass = renderPass;
        this->framebuffers = framebuffers;
        this->extent = extent;
        this->pipeline = pipeline;
        this->pipelineLayout = pipelineLayout;
        this->vertexBuffer = vertexBuffer;
        this->descriptorSets = descriptorSets;
        initialized = true;
    }

    void execute(VkCommandBuffer commandBuffer, FrameContext& frame) override {
        if (!initialized) {
            return;
        }

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = *renderPass;
        renderPassInfo.framebuffer = (*framebuffers)[frame.swapchainImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = *extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent->width);
        viewport.height = static_cast<float>(extent->height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = *extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkBuffer vertexBuffers[] = {vertexBuffer->buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            *pipelineLayout,
            0,
            1,
            &((*descriptorSets)[frame.frameIndex]),
            0,
            nullptr
        );

        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }

private:
    bool initialized = false;
    VkRenderPass* renderPass = nullptr;
    std::vector<VkFramebuffer>* framebuffers = nullptr;
    VkExtent2D* extent = nullptr;
    VkPipeline* pipeline = nullptr;
    VkPipelineLayout* pipelineLayout = nullptr;
    ResourceHandle* vertexBuffer = nullptr;
    std::vector<VkDescriptorSet>* descriptorSets = nullptr;
};

class FullscreenPass : public RenderPass {
public:
    FullscreenPass() = default;

    void setup(VkRenderPass* renderPass,
               std::vector<VkFramebuffer>* framebuffers,
               VkExtent2D* extent,
               VkPipeline* pipeline,
               VkPipelineLayout* pipelineLayout,
               std::vector<VkDescriptorSet>* descriptorSets) {
        this->renderPass = renderPass;
        this->framebuffers = framebuffers;
        this->extent = extent;
        this->pipeline = pipeline;
        this->pipelineLayout = pipelineLayout;
        this->descriptorSets = descriptorSets;
        initialized = true;
    }

    void setPostDrawCallback(std::function<void(VkCommandBuffer, FrameContext&)> callback) {
        postDrawCallback = std::move(callback);
    }

    void execute(VkCommandBuffer commandBuffer, FrameContext& frame) override {
        if (!initialized) {
            return;
        }

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = *renderPass;
        renderPassInfo.framebuffer = (*framebuffers)[frame.swapchainImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = *extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            *pipelineLayout,
            0,
            1,
            &((*descriptorSets)[frame.frameIndex]),
            0,
            nullptr
        );

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent->width);
        viewport.height = static_cast<float>(extent->height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = *extent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (postDrawCallback) {
            postDrawCallback(commandBuffer, frame);
        }
        vkCmdEndRenderPass(commandBuffer);
    }

private:
    bool initialized = false;
    VkRenderPass* renderPass = nullptr;
    std::vector<VkFramebuffer>* framebuffers = nullptr;
    VkExtent2D* extent = nullptr;
    VkPipeline* pipeline = nullptr;
    VkPipelineLayout* pipelineLayout = nullptr;
    std::vector<VkDescriptorSet>* descriptorSets = nullptr;
    std::function<void(VkCommandBuffer, FrameContext&)> postDrawCallback;
};


struct VideoTextureResources {
    struct StagingSlot {
        ResourceHandle buffer;
        void* mapped = nullptr;
        size_t capacity = 0;

        // Delete copy constructor and copy assignment to prevent double-ownership
        StagingSlot(const StagingSlot&) = delete;
        StagingSlot& operator=(const StagingSlot&) = delete;

        // Default move constructor and move assignment
        StagingSlot(StagingSlot&& other) noexcept
            : buffer(std::move(other.buffer))
            , mapped(other.mapped)
            , capacity(other.capacity)
        {
            other.mapped = nullptr;
            other.capacity = 0;
        }

        StagingSlot& operator=(StagingSlot&& other) noexcept {
            if (this != &other) {
                buffer = std::move(other.buffer);
                mapped = other.mapped;
                capacity = other.capacity;

                other.mapped = nullptr;
                other.capacity = 0;
            }
            return *this;
        }

        StagingSlot() = default;
    };

    ResourceHandle imageHandle;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptorInfo{};
    uint32_t width = 1;
    uint32_t height = 1;
    size_t frameSize = 4;
    bool ready = false;
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::array<StagingSlot, MAX_FRAMES_IN_FLIGHT> stagingSlots{};
    std::array<double, MAX_FRAMES_IN_FLIGHT> stagingTimestamps{};
    std::array<bool, MAX_FRAMES_IN_FLIGHT> pendingUploads{};

    // Second texture for frame interpolation (previous frame)
    ResourceHandle imageHandlePrev;
    VkImageView imageViewPrev = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptorInfoPrev{};
    VkImageLayout currentLayoutPrev = VK_IMAGE_LAYOUT_UNDEFINED;
    std::array<bool, MAX_FRAMES_IN_FLIGHT> pendingUploadsPrev{};
    std::vector<uint8_t> previousFrameData;  // CPU buffer for previous frame
    ResourceHandle prevFrameStagingBuffer;  // Staging buffer for previous frame upload
    void* prevFrameStagingMapped = nullptr;
};

// Global staging buffer lifecycle tracking
static int g_aliveStagingBuffers = 0;
static int g_destroyStagingCalls = 0;

// Reentrancy guards for debugging
static std::atomic<bool> g_swapchainRecreating(false);
static std::atomic<bool> g_videoReloading(false);
static std::string g_pendingVideoReloadAfterSwapchain;

// VideoRandomizerState - moved outside App class for sharing with UISystem
struct VideoRandomizerState {
    bool autoRandomize = false;
    bool useVideoDuration = false;
    float intervalSeconds = 30.0f;
    float elapsedSeconds = 0.0f;
    float currentVideoDuration = 0.0f;
    int historyWindow = 3;
    std::deque<int> recentHistory;
};

class App {
public:
    void run() {
        initSDL();
        createWindow();
        createVulkanInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        resourceSystem.init(device, physicalDevice);
        createSwapchain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createPipelineLayout();
        createGraphicsPipeline();
        createFullscreenPipeline();
        createSwapchainFramebuffers();
        createSwapchainSemaphores();
        createCommandPool();
        createVertexBuffer();
        createUniformBuffers();
        initializeVideoAssets();
        initVideoSystem();
        ControlState::load(controlStatePath, visualControls, videoRandomizer, allowDimensionChangeRecreation);
        createDescriptorPool();
        createDescriptorSets();
        createUiWindow();
        if (!uiSystem.initialize(uiWindow, uiRenderer)) {
            throw std::runtime_error("failed to initialize UI system");
        }
        
        // Initialize multi-pass pipeline
        std::cout << "[MULTIPASS INIT] videoTexture.sampler=" << videoTexture.sampler << " imageView=" << videoTexture.imageView << std::endl;
        if (!multiPassPipeline.initialize(
            physicalDevice,
            device,
            graphicsQueue,
            queueFamilyIndices.graphicsFamily,
            swapchainExtent,
            swapchainImageFormat,
            videoTexture.sampler,
            videoTexture.sampler,  // Use same sampler for prev
            videoTexture.imageView,
            videoTexture.imageViewPrev,  // Use prev view
            uniformBuffers,
            sizeof(GlobalUBO)
        )) {
            std::cerr << "[App] Failed to initialize multi-pass pipeline" << std::endl;
        }
        
        // Initialize formal NLE architecture
        renderWorker = std::make_unique<RenderWorker>();
        renderWorker->on_render_start = [this](std::shared_ptr<RenderJob> job) {
            // Pause renderer when render starts for faster FFmpeg processing
            std::cout << "[Render] Pausing renderer for faster FFmpeg processing" << std::endl;
            playbackClock.pause();
        };
        renderWorker->on_render_complete = [this](std::shared_ptr<RenderJob> job) {
            std::cout << "[Render] Completed, output at: " << job->output_file << std::endl;
            // Resume rendering after render completes (no reload for Apply Changes)
            playbackClock.resume();
        };

        g_project_state.active_file = videoSourcePath;
        // Reset all to auto-detect (0 = auto)
        g_project_state.width = 0;
        g_project_state.height = 0;
        g_project_state.fps = 0;

        fullscreenPass.setup(&renderPass,
                            &swapchainFramebuffers,
                            &swapchainExtent,
                            &fullscreenPipeline,
                            &pipelineLayout,
                            &descriptorSets);

        // Execute multipass pipeline (includes final swapchain pass)
        renderer.addNode({
            "MultiPassPipeline",
            {},
            {},
            [&](VkCommandBuffer cmd, FrameContext& frame) {
                multiPassPipeline.execute(cmd, frame.frameIndex, descriptorSets[frame.frameIndex],
                                         renderPass, swapchainFramebuffers, frame.swapchainImageIndex,
                                         fullscreenPipeline, pipelineLayout, descriptorSets[frame.frameIndex],
                                         swapchainExtent, videoTexture.sampler);
            }
        });

        createCommandBuffers();
        frameSystem.init(device, MAX_FRAMES_IN_FLIGHT);
        startTime = std::chrono::steady_clock::now();
        lastControlSaveTime = startTime;
        lastFrameTimestamp = startTime;
        initializationComplete = true;
        mainLoop();
        cleanup();
    }

private:
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        bool graphicsFamilyFound;
        bool presentFamilyFound;
    };

    SDL_Window* window = nullptr;
    SDL_Window* uiWindow = nullptr;
    SDL_Renderer* uiRenderer = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilyIndices{};
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkSemaphore> swapchainRenderSemaphores;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    FrameSystem frameSystem;
    ResourceSystem resourceSystem;
    Renderer renderer;
    TrianglePass trianglePass;
    FullscreenPass fullscreenPass;
    MultiPassPipeline multiPassPipeline;
    bool running = true;
    bool framebufferResized = false;
    bool resizePending = false;
    bool initializationComplete = false;
    VkExtent2D lastDrawableExtent{0, 0};
    uint32_t resizeStableFrames = 0;
    static constexpr uint32_t RESIZE_STABILITY_THRESHOLD = 2;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline fullscreenPipeline = VK_NULL_HANDLE;
    VkPipeline tonemapPipeline = VK_NULL_HANDLE;
    ResourceHandle vertexBufferHandle;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
    std::vector<ResourceHandle> uniformBufferHandles;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    std::chrono::steady_clock::time_point startTime;
    std::vector<FrameContext*> imagesInFlight;
    int currentMode = 0;
    VisualControls visualControls;
    UISystem uiSystem;
    uint32_t lastFrameImageIndex = 0;
    uint32_t lastFrameFrameIndex = 0;
    VideoPlayer videoPlayer;
    VideoTextureResources videoTexture;
    struct CachedVideoFrame {
        std::vector<uint8_t> pixels;
        double timestamp = 0.0;
        int width = 0;
        int height = 0;
        bool keyframe = false;
    };
    std::deque<CachedVideoFrame> videoFrameBuffer;
    std::vector<uint8_t> loopBlendScratch;
    std::vector<uint8_t> transitionFrame;  // Last frame from previous video for crossfade
    double loopBlendDuration = 0.5;
    float transitionDuration = 0.5f;  // Crossfade duration during video change
    double transitionProgress = 0.0;  // 0.0 to 1.0, 1.0 = transition complete
    bool inTransition = false;
    double videoPlaybackCursor = 0.0;
    double videoDecodeCursor = 0.0;
    double videoDisplayTimer = 0.0;
    double videoDecodeTimer = 0.0;
    std::chrono::steady_clock::time_point lastFrameTimestamp;
    bool videoSubsystemInitialized = false;
    std::string videoSourcePath = "media/sample.mp4";
    std::string pendingVideoReload;  // Thread-safe video reload request
    std::mutex videoReloadMutex;
    VideoRegistry videoRegistry;
    std::string videoAssetsRoot = "mp4s";
    int selectedVideoAsset = -1;
    uint64_t currentEpoch = 1;
    VideoRandomizerState videoRandomizer;
    bool allowDimensionChangeRecreation = false;
    std::mt19937 randomEngine{std::random_device{}()};
    std::string controlStatePath = "controls_state.cfg";
    bool controlsDirty = false;
    std::chrono::steady_clock::time_point lastControlSaveTime;
    std::chrono::steady_clock::time_point lastReloadTime;
    bool videoReloadInProgress = false;
    
    // NLE Architecture Components
    EffectChain currentEffectChain;
    Timeline timeline;
    PlaybackClock playbackClock;
    bool useNLEPlayback = false;  // Toggle between old sequential and new PTS-based playback
    
    // Formal NLE Architecture (ProjectState system)
    std::unique_ptr<RenderWorker> renderWorker;
    uint64_t last_loaded_version = 0;

    void initSDL() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            throw std::runtime_error(std::string("failed to initialize SDL: ") + SDL_GetError());
        }
    }

    void createWindow() {
        window = SDL_CreateWindow(
            "Vulkan",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            WIDTH,
            HEIGHT,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            throw std::runtime_error(std::string("failed to create SDL window: ") + SDL_GetError());
        }
    }

    void createUiWindow() {
        uiWindow = SDL_CreateWindow(
            "Controls",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            420,
            420,
            SDL_WINDOW_RESIZABLE
        );

        if (!uiWindow) {
            throw std::runtime_error(std::string("failed to create ImGui SDL window: ") + SDL_GetError());
        }

        uiRenderer = SDL_CreateRenderer(uiWindow, -1, SDL_RENDERER_ACCELERATED);
        if (!uiRenderer) {
            uiRenderer = SDL_CreateRenderer(uiWindow, -1, 0);
        }

        if (!uiRenderer) {
            SDL_DestroyWindow(uiWindow);
            uiWindow = nullptr;
            throw std::runtime_error(std::string("failed to create SDL renderer: ") + SDL_GetError());
        }

        SDL_SetRenderDrawBlendMode(uiRenderer, SDL_BLENDMODE_BLEND);
        if (SDL_RenderSetVSync(uiRenderer, 0) != 0) {
            std::cerr << "[ImGui] warning: failed to disable renderer vsync: " << SDL_GetError() << std::endl;
        }
    }

    void destroyUiWindow() {
        if (uiRenderer != nullptr) {
            SDL_DestroyRenderer(uiRenderer);
            uiRenderer = nullptr;
        }
        if (uiWindow != nullptr) {
            SDL_DestroyWindow(uiWindow);
            uiWindow = nullptr;
        }
    }

    void createVulkanInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Minimal Vulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers not available");
        }

        std::vector<const char*> extensions = getRequiredExtensions();

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = enableValidationLayers ?
            static_cast<uint32_t>(validationLayers.size()) : 0;
        createInfo.ppEnabledLayerNames = enableValidationLayers ?
            validationLayers.data() : nullptr;

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create instance!");
        }
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t extensionCount = 0;
        SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr);

        std::vector<const char*> extensions(extensionCount);
        SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data());

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void setupDebugMessenger() {
        if (!enableValidationLayers) {
            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;

        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger");
        }
    }

    void destroyDebugMessenger() {
        if (!enableValidationLayers || debugMessenger == VK_NULL_HANDLE) {
            return;
        }

        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    void createSurface() {
        if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
            throw std::runtime_error(std::string("failed to create surface: ") + SDL_GetError());
        }
    }

    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        std::cout << "[GPU] Found " << deviceCount << " physical devices" << std::endl;

        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        for (const auto& device : devices) {
            if (isDeviceSuitable(device)) {
                physicalDevice = device;
                queueFamilyIndices = findQueueFamilies(device);

                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(device, &props);
                std::cout << "[GPU] Selected: " << props.deviceName << std::endl;
                std::cout << "[GPU] Graphics queue family: " << queueFamilyIndices.graphicsFamily << std::endl;
                std::cout << "[GPU] Present queue family: " << queueFamilyIndices.presentFamily << std::endl;
                break;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
        QueueFamilyIndices indices{};

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
                indices.graphicsFamilyFound = true;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (presentSupport) {
                indices.presentFamily = i;
                indices.presentFamilyFound = true;
            }

            if (indices.graphicsFamilyFound && indices.presentFamilyFound) {
                break;
            }

            i++;
        }

        return indices;
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> available(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

        std::set<std::string> required = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        for (const auto& ext : available) {
            required.erase(ext.extensionName);
        }

        return required.empty();
    }

    bool isDeviceSuitable(VkPhysicalDevice device) {
        QueueFamilyIndices indices = findQueueFamilies(device);

        if (!indices.graphicsFamilyFound || !indices.presentFamilyFound) {
            return false;
        }

        if (!checkDeviceExtensionSupport(device)) {
            return false;
        }

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && supportedFeatures.geometryShader) {
            return true;
        }

        return false;
    }

    void createLogicalDevice() {
        float priority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {
            queueFamilyIndices.graphicsFamily,
            queueFamilyIndices.presentFamily
        };

        for (uint32_t queueFamily : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &priority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("failed to create logical device!");
        }

        vkGetDeviceQueue(device, queueFamilyIndices.graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, queueFamilyIndices.presentFamily, 0, &presentQueue);
    }

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());

        return details;
    }

    VkExtent2D chooseSwapchainExtent(const SwapChainSupportDetails& support) const {
        if (support.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return support.capabilities.currentExtent;
        }

        uint32_t width = WIDTH;
        uint32_t height = HEIGHT;
        if (window) {
            int drawableWidth = 0;
            int drawableHeight = 0;
            SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
            if (drawableWidth > 0 && drawableHeight > 0) {
                width = static_cast<uint32_t>(drawableWidth);
                height = static_cast<uint32_t>(drawableHeight);
            }
        }

        VkExtent2D extent{};
        extent.width = std::clamp(width, support.capabilities.minImageExtent.width, support.capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, support.capabilities.minImageExtent.height, support.capabilities.maxImageExtent.height);
        return extent;
    }

    void handleResizeHint() {
        if (!resizePending) {
            return;
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);

        if (drawableWidth == 0 || drawableHeight == 0) {
            framebufferResized = false;
            return;
        }

        VkExtent2D currentExtent{static_cast<uint32_t>(drawableWidth), static_cast<uint32_t>(drawableHeight)};
        if (currentExtent.width == lastDrawableExtent.width && currentExtent.height == lastDrawableExtent.height) {
            if (++resizeStableFrames >= RESIZE_STABILITY_THRESHOLD) {
                framebufferResized = true;
                resizePending = false;
            }
        } else {
            resizeStableFrames = 0;
            lastDrawableExtent = currentExtent;
        }
    }

    void createSwapchain() {
        auto support = querySwapChainSupport(physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = support.formats[0];
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

        VkExtent2D extent = chooseSwapchainExtent(support);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        std::cout << "[Swapchain] Extent: " << extent.width << "x" << extent.height << std::endl;
        std::cout << "[Swapchain] Image count: " << imageCount << std::endl;
        std::cout << "[Swapchain] Format: " << surfaceFormat.format << std::endl;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swapchain");
        }

        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

        swapchainImageFormat = surfaceFormat.format;
        swapchainExtent = extent;

        frameSystem.resizeSwapchainImages(swapchainImages.size());
        imagesInFlight.assign(swapchainImages.size(), nullptr);

        std::cout << "[Swapchain] Created successfully" << std::endl;
    }

    void createImageViews() {
        swapchainImageViews.resize(swapchainImages.size());

        std::cout << "[ImageViews] Creating " << swapchainImages.size() << " image views" << std::endl;

        for (size_t i = 0; i < swapchainImages.size(); ++i) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &createInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create image views");
            }
        }

        std::cout << "[ImageViews] Created successfully" << std::endl;
    }

    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchainImageFormat;
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

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create geometry render pass");
        }

        std::cout << "[GeometryPass] Render pass created" << std::endl;
    }

    void createSwapchainFramebuffers() {
        swapchainFramebuffers.resize(swapchainImageViews.size());

        for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
            VkImageView attachments[] = {swapchainImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapchainExtent.width;
            framebufferInfo.height = swapchainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer");
            }
        }

        std::cout << "[Swapchain] Framebuffers created" << std::endl;
    }

    void createSwapchainSemaphores() {
        destroySwapchainSemaphores();

        swapchainRenderSemaphores.resize(swapchainImages.size(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (size_t i = 0; i < swapchainRenderSemaphores.size(); ++i) {
            if (vkCreateSemaphore(device, &info, nullptr, &swapchainRenderSemaphores[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create swapchain render semaphore");
            }
        }
    }

    void destroySwapchainSemaphores() {
        for (auto& semaphore : swapchainRenderSemaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
                semaphore = VK_NULL_HANDLE;
            }
        }
        swapchainRenderSemaphores.clear();
    }

    void cleanupSwapchain() {
        destroySwapchainSemaphores();
        for (auto framebuffer : swapchainFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        swapchainFramebuffers.clear();

        if (!swapchainImageViews.empty()) {
            for (auto imageView : swapchainImageViews) {
                if (imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, imageView, nullptr);
                }
            }
            swapchainImageViews.clear();
        }

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        swapchainImages.clear();
    }

    void recreateSwapchain() {
        if (!initializationComplete) {
            std::cout << "[Swapchain] recreateSwapchain blocked during initialization" << std::endl;
            framebufferResized = true;
            return;
        }

        // Check for reentrancy
        if (g_swapchainRecreating.exchange(true)) {
            std::cout << "[SWAPCHAIN] RECREATE SKIPPED (already recreating)" << std::endl;
            return;
        }

        std::cout << "[SWAPCHAIN] RECREATE START (video reloading: " << g_videoReloading << ")" << std::endl;
        vkDeviceWaitIdle(device);

        cleanupSwapchain();
        std::cout << "[SWAPCHAIN] DESTROY old resources" << std::endl;

        // Wait for all fences to ensure GPU is done with command buffers
        frameSystem.waitForAllFences();

        // Reset command buffers to clear references to old swapchain resources
        for (auto cmdBuffer : commandBuffers) {
            vkResetCommandBuffer(cmdBuffer, 0);
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);

        if (drawableWidth == 0 || drawableHeight == 0) {
            framebufferResized = true;
            return;
        }

        createSwapchain();
        createImageViews();
        createSwapchainFramebuffers();
        createSwapchainSemaphores();
        frameSystem.resizeSwapchainImages(swapchainImages.size());
        multiPassPipeline.recreate(swapchainExtent);
        std::cout << "[SWAPCHAIN] CREATE new extent=" << drawableWidth << "," << drawableHeight << std::endl;

        std::cout << "[RESET] frameIndex=0 fences reset staging reset" << std::endl;
        frameSystem.resetCurrentFrame();
        videoTexture.pendingUploads.fill(false);

        uint32_t targetVideoWidth = std::max<uint32_t>(1u, videoTexture.width);
        uint32_t targetVideoHeight = std::max<uint32_t>(1u, videoTexture.height);
        if (videoSubsystemInitialized && videoPlayer.isReady()) {
            std::cout << "[VIDEO] Reinitializing decoder after swapchain resize" << std::endl;
            
            int newW = static_cast<int>(swapchainExtent.width);
            int newH = static_cast<int>(swapchainExtent.height);
            
            if (!videoPlayer.initialize(videoPlayer.sourcePath(), newW, newH)) {
                std::cerr << "[VIDEO] Decoder reinit failed" << std::endl;
                videoSubsystemInitialized = false;
            } else {
                targetVideoWidth = static_cast<uint32_t>(std::max(1, videoPlayer.width()));
                targetVideoHeight = static_cast<uint32_t>(std::max(1, videoPlayer.height()));
            }
        }

        createVideoTextureResources(targetVideoWidth, targetVideoHeight);

        // Update descriptor sets to ensure they point to valid resources
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT && i < descriptorSets.size(); ++i) {
            updateDescriptorSet(i);
        }

        // Update multipass descriptor sets with new resources
        multiPassPipeline.updateDescriptorSets(
            uniformBuffers,
            videoTexture.imageView,
            videoTexture.imageViewPrev,
            videoTexture.sampler,
            videoTexture.sampler
        );

        std::cout << "[SWAPCHAIN] RECREATE END" << std::endl;
        g_swapchainRecreating = false;

        // Process pending video reload after swapchain recreation
        if (!g_pendingVideoReloadAfterSwapchain.empty()) {
            std::cout << "[SWAPCHAIN] Processing pending video reload: " << g_pendingVideoReloadAfterSwapchain << std::endl;
            std::string pendingPath = g_pendingVideoReloadAfterSwapchain;
            g_pendingVideoReloadAfterSwapchain.clear();
            reloadVideoSource(pendingPath);
        }
    }

    void createDescriptorSetLayout() {
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

        VkDescriptorSetLayoutBinding samplerBindingPrev{};
        samplerBindingPrev.binding = 2;
        samplerBindingPrev.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBindingPrev.descriptorCount = 1;
        samplerBindingPrev.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerBindingPrev.pImmutableSamplers = nullptr;

        std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboLayoutBinding, samplerBinding, samplerBindingPrev};
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }
    }

    void createPipelineLayout() {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;

        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout");
        }

        std::cout << "[Pipeline] Layout created" << std::endl;
    }

    void createGraphicsPipeline() {
        auto vertShaderCode = readFile("shaders/triangle.vert.spv");
        auto fragShaderCode = readFile("shaders/triangle.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

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

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &binding;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

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
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create geometry pipeline");
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        std::cout << "[Pipeline] Geometry pipeline created" << std::endl;
    }

    void createFullscreenPipeline() {
        auto vertShaderCode = readFile("shaders/fullscreen.vert.spv");
        auto fragShaderCode = readFile("shaders/fullscreen.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

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

        // No vertex input for fullscreen quad (generated in vertex shader)
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

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
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &fullscreenPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fullscreen pipeline");
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        std::cout << "[Pipeline] Fullscreen pipeline created" << std::endl;
    }

    void createUniformBuffers() {
        VkDeviceSize bufferSize = sizeof(GlobalUBO);
        uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBufferHandles.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            ResourceHandle handle = resourceSystem.createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Save legacy fields before moving
            uniformBuffers[i] = handle.buffer;
            uniformBuffersMemory[i] = handle.memory;

            // Map before moving
            uniformBuffersMapped[i] = resourceSystem.map(handle);
            if (uniformBuffersMapped[i] == nullptr) {
                throw std::runtime_error("failed to map uniform buffer");
            }

            // Now move the handle
            uniformBufferHandles[i] = std::move(handle);
        }
    }

    void initVideoSystem() {
        if (videoSubsystemInitialized) {
            return;
        }

        if (!fs::exists(videoSourcePath)) {
            std::cerr << "[Video] Source not found: " << videoSourcePath << std::endl;
            return;
        }

        int screenW = static_cast<int>(swapchainExtent.width);
        int screenH = static_cast<int>(swapchainExtent.height);

        if (!videoPlayer.initialize(videoSourcePath, screenW, screenH)) {
            std::cerr << "[Video] Failed to initialize player for " << videoSourcePath << std::endl;
            return;
        }

        std::cout << "[Video] Player initialized: " << videoSourcePath << " (" << videoPlayer.width() << "x" << videoPlayer.height() << ")" << std::endl;

        // Decode a probe frame to ensure ensureScalingContext calculates
        // the final dimensions before creating buffers
        {
            std::cout << "[Video] Grabbing probe frame..." << std::endl;
            std::vector<uint8_t> probeFrame;
            int probeW = 0, probeH = 0;
            videoPlayer.grabFrame(probeFrame, probeW, probeH);
            std::cout << "[Video] Probe frame grabbed: " << probeW << "x" << probeH << " size=" << probeFrame.size() << std::endl;
            // Don't use the frame, just force the scaler to initialize
            // with the correct dimensions
            std::cout << "[Video] Rewinding to 0s..." << std::endl;
            videoPlayer.seekSeconds(0.0);  // rewind
            std::cout << "[Video] Rewind complete" << std::endl;
        }

        std::cout << "[Video] Final decode dimensions: " << videoPlayer.width() << "x" << videoPlayer.height() << std::endl;

        // Reset lastFrameTimestamp to prevent large deltaTime on first frame
        lastFrameTimestamp = std::chrono::steady_clock::now();

        if (visualControls.randomVideoStart && videoPlayer.durationSeconds() > 0.1) {
            std::uniform_real_distribution<double> dist(0.0, std::max(0.0, videoPlayer.durationSeconds() - videoPlayer.frameDuration()));
            double target = dist(randomEngine);
            std::cout << "[Video] Random start: seeking to " << target << "s (duration: " << videoPlayer.durationSeconds() << "s)" << std::endl;
            if (videoPlayer.seekSeconds(target)) {
                resetVideoPlaybackState(target);
            } else {
                std::cout << "[Video] Random start: seek failed, starting from 0" << std::endl;
                resetVideoPlaybackState(0.0);
            }
        } else {
            std::cout << "[Video] Starting from 0s (random start: " << visualControls.randomVideoStart << ", duration: " << videoPlayer.durationSeconds() << "s)" << std::endl;
            resetVideoPlaybackState(0.0);
        }

        createVideoTextureResources(static_cast<uint32_t>(std::max(1, videoPlayer.width())),
                                    static_cast<uint32_t>(std::max(1, videoPlayer.height())));

        videoSubsystemInitialized = videoTexture.ready;
        if (!videoSubsystemInitialized) {
            return;
        }

        videoRandomizer.currentVideoDuration = static_cast<float>(expectedPlaybackDurationSeconds());

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT && i < descriptorSets.size(); ++i) {
            updateDescriptorSet(i);
        }

        std::cout << "[Video] Initialized " << videoSourcePath << " (" << videoPlayer.width() << "x" << videoPlayer.height() << ")" << std::endl;
    }

    void createVideoTextureResources(uint32_t width, uint32_t height) {
        // Check if recreation is necessary
        // ANTES: saltaba si mismas dimensiones, pero los buffers pueden
        // haber sido destruidos por un reload previo
        // AHORA: comprueba que los buffers realmente existen
        bool buffersValid = !videoTexture.stagingSlots.empty();
        for (size_t i = 0; i < videoTexture.stagingSlots.size() && buffersValid; ++i) {
            if (videoTexture.stagingSlots[i].mapped == nullptr) {
                buffersValid = false;
            }
        }

        size_t requiredSize = static_cast<size_t>(width) * height * 4;
        size_t allocatedSize = videoTexture.frameSize;

        std::cout << "[STAGING] Dimension check: allocated=" << allocatedSize 
                  << " required=" << requiredSize 
                  << " width=" << width << " height=" << height 
                  << " old=" << videoTexture.width << "x" << videoTexture.height
                  << " ready=" << videoTexture.ready
                  << " buffersValid=" << buffersValid << std::endl;

        if (videoTexture.ready && videoTexture.width == width && videoTexture.height == height && buffersValid && allocatedSize >= requiredSize) {
            std::cout << "[STAGING] Skipping recreation (valid)" << std::endl;
            return;
        }

        std::cout << "[STAGING] Creating video texture resources: " << width << "x" << height << std::endl;
        destroyVideoTexture();

        videoTexture.width = width;
        videoTexture.height = height;
        videoTexture.frameSize = static_cast<size_t>(width) * height * 4;

        // Create current frame texture
        videoTexture.imageHandle = resourceSystem.createImage(
            width,
            height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // Create previous frame texture for interpolation
        videoTexture.imageHandlePrev = resourceSystem.createImage(
            width,
            height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = videoTexture.imageHandle.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &videoTexture.imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create video image view");
        }

        // Create image view for previous frame texture
        viewInfo.image = videoTexture.imageHandlePrev.image;
        if (vkCreateImageView(device, &viewInfo, nullptr, &videoTexture.imageViewPrev) != VK_SUCCESS) {
            throw std::runtime_error("failed to create video image view for previous frame");
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(device, &samplerInfo, nullptr, &videoTexture.sampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create video sampler");
        }

        initializeVideoImage(videoTexture.imageHandle.image);
        initializeVideoImage(videoTexture.imageHandlePrev.image);

        videoTexture.descriptorInfo.sampler = videoTexture.sampler;
        videoTexture.descriptorInfo.imageView = videoTexture.imageView;
        videoTexture.descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.pendingUploads.fill(false);

        videoTexture.descriptorInfoPrev.sampler = videoTexture.sampler;
        videoTexture.descriptorInfoPrev.imageView = videoTexture.imageViewPrev;
        videoTexture.descriptorInfoPrev.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.currentLayoutPrev = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        videoTexture.pendingUploadsPrev.fill(false);

        // Allocate CPU buffer for previous frame
        videoTexture.previousFrameData.resize(videoTexture.frameSize);

        // Create staging buffer for previous frame upload
        auto prevStagingBuffer = resourceSystem.createBuffer(
            static_cast<VkDeviceSize>(videoTexture.frameSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* prevMapped = resourceSystem.map(prevStagingBuffer);
        if (prevMapped == nullptr) {
            resourceSystem.destroy(prevStagingBuffer);
            throw std::runtime_error("failed to map previous frame staging buffer");
        }

        videoTexture.prevFrameStagingBuffer = std::move(prevStagingBuffer);
        videoTexture.prevFrameStagingMapped = prevMapped;
        g_aliveStagingBuffers++;
        std::cout << "[STAGING] Created prev frame staging buffer, alive=" << g_aliveStagingBuffers << std::endl;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (videoTexture.frameSize == 0) {
                videoTexture.stagingSlots[i] = {};
                continue;
            }

            auto buffer = resourceSystem.createBuffer(
                static_cast<VkDeviceSize>(videoTexture.frameSize),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* mapped = resourceSystem.map(buffer);
            if (mapped == nullptr) {
                resourceSystem.destroy(buffer);
                throw std::runtime_error("failed to map video staging buffer");
            }

            videoTexture.stagingSlots[i].buffer = std::move(buffer);
            videoTexture.stagingSlots[i].mapped = mapped;
            videoTexture.stagingSlots[i].capacity = videoTexture.frameSize;
            g_aliveStagingBuffers++;
            std::cout << "[STAGING] Created staging slot " << i << ", alive=" << g_aliveStagingBuffers << std::endl;
        }

        videoTexture.ready = true;
    }

    void initializeVideoImage(VkImage image) {
        if (image == VK_NULL_HANDLE) {
            return;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffer for video image init");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to begin command buffer for video image init");
        }

        auto transition = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkAccessFlags srcAccessMask = 0;
            VkAccessFlags dstAccessMask = 0;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            }

            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;

            vkCmdPipelineBarrier(
                commandBuffer,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        };

        transition(VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange clearRange{};
        clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel = 0;
        clearRange.levelCount = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount = 1;

        vkCmdClearColorImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor,
            1,
            &clearRange);

        transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to record command buffer for video image init");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
            throw std::runtime_error("failed to submit command buffer for video image init");
        }

        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

    void resetVideoPlaybackState(double startTime = 0.0) {
        videoPlaybackCursor = startTime;
        videoDecodeCursor = startTime;
        videoDisplayTimer = 0.0;
        videoDecodeTimer = 0.0;
        inTransition = false;
        transitionProgress = 0.0;
        transitionFrame.clear();
        videoFrameBuffer.clear();
    }

    bool decodeFrameIntoBuffer(double frameDuration) {
        if (!videoSubsystemInitialized || !videoPlayer.isReady() || videoTexture.frameSize == 0) {
            return false;
        }

        double duration = videoPlayer.durationSeconds();
        bool didLoop = false;
        if (duration > 0.1 && videoDecodeCursor >= duration) {
            // Soft loop: just reset cursors, don't clear buffer to prevent FPS drops
            videoDecodeCursor = 0.0;
            videoPlaybackCursor = 0.0;
            // Keep last few frames for smooth transition
            while (videoFrameBuffer.size() > 2) {
                videoFrameBuffer.pop_front();
            }
            didLoop = true;
        }

        try {
            CachedVideoFrame frame;
            frame.timestamp = videoDecodeCursor;
            frame.width = static_cast<int>(videoTexture.width);
            frame.height = static_cast<int>(videoTexture.height);
            frame.pixels.resize(videoTexture.frameSize);

            int decodedWidth = 0;
            int decodedHeight = 0;
            if (!videoPlayer.grabFrameInto(frame.pixels.data(), frame.pixels.size(), decodedWidth, decodedHeight)) {
                if (didLoop) {
                    return false;
                }
                return false;
            }

            if (decodedWidth != static_cast<int>(videoTexture.width) || decodedHeight != static_cast<int>(videoTexture.height)) {
                std::cout << "[VIDEO] Dimension change detected: " << videoTexture.width << "x" << videoTexture.height
                          << " -> " << decodedWidth << "x" << decodedHeight << std::endl;
                if (allowDimensionChangeRecreation) {
                    createVideoTextureResources(decodedWidth, decodedHeight);
                    frame.width = decodedWidth;
                    frame.height = decodedHeight;
                    videoFrameBuffer.clear();
                    videoFrameBuffer.push_back(frame);
                    videoDecodeCursor = frame.timestamp + frameDuration;
                    return true;
                } else {
                    std::cout << "[VIDEO] Dimension change recreation disabled, skipping texture recreation" << std::endl;
                    frame.width = decodedWidth;
                    frame.height = decodedHeight;
                    videoFrameBuffer.push_back(std::move(frame));
                    while (videoFrameBuffer.size() > 4) {
                        videoFrameBuffer.pop_front();
                    }
                }
            }

            frame.width = decodedWidth;
            frame.height = decodedHeight;
            videoFrameBuffer.push_back(std::move(frame));
            // Limit buffer size to prevent memory growth
            while (videoFrameBuffer.size() > MAX_VIDEO_FRAME_BUFFER) {
                videoFrameBuffer.pop_front();
            }
            videoDecodeCursor += frameDuration;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[VIDEO] Exception in decodeFrameIntoBuffer: " << e.what() << std::endl;
            return false;
        } catch (...) {
            std::cerr << "[VIDEO] Unknown exception in decodeFrameIntoBuffer" << std::endl;
            return false;
        }
    }

    bool crossfadeFrames(const CachedVideoFrame& a, const CachedVideoFrame& b, float mix, uint8_t* dst, size_t capacity) {
        mix = std::clamp(mix, 0.0f, 1.0f);
        size_t copySize = static_cast<size_t>(a.width) * a.height * 4;
        if (copySize == 0 || copySize > capacity || a.pixels.empty() || b.pixels.empty() || a.width != b.width || a.height != b.height) {
            return false;
        }
        if (loopBlendScratch.size() < copySize) {
            loopBlendScratch.resize(copySize);
        }
        const uint8_t* pa = a.pixels.data();
        const uint8_t* pb = b.pixels.data();
        uint8_t* out = dst;
        for (size_t i = 0; i < copySize; ++i) {
            float value = pa[i] * (1.0f - mix) + pb[i] * mix;
            out[i] = static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 255.0f)));
        }
        return true;
    }

    void pruneVideoFrameBuffer(double minTimestamp) {
        // Keep buffer size appropriate for loopBlendSeconds (up to 2.0s at various framerates)
        // At 60fps, 2.0s = 120 frames; at 30fps, 2.0s = 60 frames
        // Use MAX_VIDEO_FRAME_BUFFER as a reasonable limit
        while (videoFrameBuffer.size() > MAX_VIDEO_FRAME_BUFFER) {
            videoFrameBuffer.pop_front();
        }
    }

    double clipFramesPerSecond() const {
        if (!videoPlayer.isReady()) {
            return 0.0;
        }
        double frameDuration = videoPlayer.frameDuration();
        if (frameDuration <= 1e-6) {
            return 0.0;
        }
        return 1.0 / frameDuration;
    }

    double effectivePlaybackRate(double clipFps) const {
        double baseRate = std::clamp(static_cast<double>(visualControls.videoPlaybackRate), 0.05, 8.0);
        if (clipFps <= 0.0) {
            return baseRate;
        }
        int idx = std::clamp(visualControls.forcedFpsIndex, 0, static_cast<int>(FORCED_FPS_OPTIONS.size()) - 1);
        int forcedFps = FORCED_FPS_OPTIONS[idx];
        if (forcedFps <= 0) {
            return baseRate;
        }
        double forcedRate = forcedFps / clipFps;
        return std::clamp(forcedRate, 0.05, 8.0);
    }

    bool sampleFrameForTime(double targetTime, uint8_t* dst, size_t capacity, size_t& outCopySize) {
        outCopySize = 0;
        if (videoFrameBuffer.empty() || dst == nullptr) {
            return false;
        }

        const CachedVideoFrame* frameA = &videoFrameBuffer.front();
        const CachedVideoFrame* frameB = frameA;

        double minDelta = std::numeric_limits<double>::max();
        for (const auto& cached : videoFrameBuffer) {
            double delta = std::abs(cached.timestamp - targetTime);
            if (delta < minDelta) {
                minDelta = delta;
                frameA = &cached;
            }
        }
        frameB = frameA;

        loopBlendDuration = std::clamp(static_cast<double>(visualControls.loopBlendSeconds), 0.0, 5.0);
        const double duration = videoPlayer.durationSeconds();
        const bool loopingClip = videoPlayer.isReady() && duration > 0.1 && videoFrameBuffer.size() >= 2;

        size_t copySize = static_cast<size_t>(frameA->width) * frameA->height * 4;
        if (copySize == 0 || copySize > capacity || frameA->pixels.empty()) {
            return false;
        }

        bool didCrossfade = false;
        if (loopingClip && loopBlendDuration > 0.01) {
            const double epsilon = 1e-3;
            const double nearEndStart = std::max(0.0, duration - loopBlendDuration);
            const bool nearStart = targetTime <= loopBlendDuration;
            const bool nearEnd = targetTime >= nearEndStart - epsilon;
            if (nearStart || nearEnd) {
                const CachedVideoFrame* earliest = &videoFrameBuffer.front();
                const CachedVideoFrame* latest = &videoFrameBuffer.back();
                if (earliest && latest && earliest != latest && earliest->width == latest->width && earliest->height == latest->height) {
                    float mix = 0.0f;
                    if (nearStart) {
                        mix = static_cast<float>(targetTime / std::max(loopBlendDuration, epsilon));
                    } else {
                        double timeIntoBlend = duration - targetTime;
                        mix = static_cast<float>(std::clamp(timeIntoBlend / std::max(loopBlendDuration, epsilon), 0.0, 1.0));
                    }
                    if (crossfadeFrames(*latest, *earliest, mix, dst, capacity)) {
                        outCopySize = copySize;
                        didCrossfade = true;
                    }
                }
            }
        }

        if (!didCrossfade) {
            std::memcpy(dst, frameA->pixels.data(), copySize);
            outCopySize = copySize;
        }
        return true;
    }

    double expectedPlaybackDurationSeconds() const {
        double duration = videoPlayer.durationSeconds();
        if (duration <= 0.1) {
            return 0.0;
        }
        double clipFps = clipFramesPerSecond();
        double rate = effectivePlaybackRate(clipFps);
        double oversample = std::max(1.0, static_cast<double>(visualControls.videoDecodeOversample));
        double effective = std::max(rate, oversample);
        return duration * (rate / effective);
    }

    void updateVideoTexture(float deltaTime, FrameContext& frame) {
        if (!videoSubsystemInitialized) {
            return;
        }

        // Guard: si los staging buffers no son válidos, no toques nada
        if (frame.frameIndex >= videoTexture.stagingSlots.size()) {
            return;
        }
        const auto& guardSlot = videoTexture.stagingSlots[frame.frameIndex];
        if (guardSlot.mapped == nullptr || guardSlot.capacity == 0) {
            std::cerr << "[VIDEO] Staging slot inválido, saltando frame" << std::endl;
            return;
        }

        try {
            // Version-based reload check
            uint64_t current_version = g_project_state.get_version();
            if (current_version != last_loaded_version) {
                // Reload video source if version changed
                std::cout << "[Video] Version changed from " << last_loaded_version << " to " << current_version << ", reloading" << std::endl;
                reloadVideoSource(g_project_state.active_file);
                last_loaded_version = current_version;
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Video] Exception in version check: " << e.what() << std::endl;
            return;
        }

        const uint32_t writeSlot = frame.frameIndex % MAX_FRAMES_IN_FLIGHT;
        videoTrace("[VIDEO] decode start frameIndex=", frame.frameIndex);
        videoTrace("[VIDEO] snapshot/write slot=", writeSlot);

        // Debug: log frame info every 60 frames
        static int debugFrameCounter = 0;
        static auto lastDebugTime = std::chrono::steady_clock::now();
        if (++debugFrameCounter % 60 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDebugTime).count();
            double actualFps = elapsed > 0 ? (60000.0 / elapsed) : 0.0;
            double targetFps = videoPlayer.frameDuration() > 0 ? (1.0 / videoPlayer.frameDuration()) : 0.0;
            lastDebugTime = now;

            // std::cout << "[FRAME DEBUG] frameIndex=" << frame.frameIndex
            //           << " writeSlot=" << writeSlot
            //           << " videoSize=" << videoTexture.width << "x" << videoTexture.height
            //           << " screenSize=" << swapchainExtent.width << "x" << swapchainExtent.height
            //           << " targetFps=" << targetFps
            //           << " actualFps=" << actualFps
            //           << " elapsed=" << elapsed << "ms" << std::endl;
        }

        if (writeSlot >= videoTexture.stagingSlots.size()) {
            std::cerr << "[STAGING] invalid write slot " << writeSlot
                      << " (slotCount=" << videoTexture.stagingSlots.size() << ")" << std::endl;
            return;
        }

        try {
            auto& slot = videoTexture.stagingSlots[writeSlot];
            if (slot.mapped == nullptr || slot.capacity == 0) {
                std::cerr << "[STAGING] slot " << writeSlot << " is not mapped" << std::endl;
                return;
            }

            // Wait for fence of the write slot to ensure GPU is done with it
            // This is safe because fence is reset BEFORE submit in mainLoop
            VkFence slotFence = frameSystem.getFence(writeSlot);
            if (slotFence != VK_NULL_HANDLE) {
                vkWaitForFences(device, 1, &slotFence, VK_TRUE, UINT64_MAX);
            }

        const double frameDuration = std::max(1e-6, videoPlayer.frameDuration());

        // Sequential playback mode (always used for live display)
        const double clipFps = (frameDuration > 1e-6) ? (1.0 / frameDuration) : 0.0;
        const double playbackRate = effectivePlaybackRate(clipFps);
        const double decodeOversample = std::max(1.0, static_cast<double>(visualControls.videoDecodeOversample));
        const double decodeSpeed = std::max(playbackRate, decodeOversample);
        const double decodeInterval = frameDuration / decodeSpeed;
        const double displayInterval = frameDuration / playbackRate;

        videoDecodeTimer += deltaTime;
        int decodeIterations = 0;
        while (videoDecodeTimer >= decodeInterval) {
            if (!decodeFrameIntoBuffer(frameDuration)) {
                break;
            }
            videoDecodeTimer -= decodeInterval;
            if (++decodeIterations > 16) {
                break;
            }
        }

        if (videoFrameBuffer.empty()) {
            if (!decodeFrameIntoBuffer(frameDuration)) {
                return;
            }
        }

        videoDisplayTimer += deltaTime;
        while (videoDisplayTimer >= displayInterval) {
            videoDisplayTimer -= displayInterval;
            videoPlaybackCursor += frameDuration;
        }

        double fraction = displayInterval > 1e-6 ? (videoDisplayTimer / displayInterval) : 0.0;
        double targetTime = videoPlaybackCursor + fraction * frameDuration;

        size_t copySize = 0;
        if (!sampleFrameForTime(targetTime, static_cast<uint8_t*>(slot.mapped), slot.capacity, copySize)) {
            return;
        }

        // Apply crossfade transition if in transition
        if (inTransition && !transitionFrame.empty()) {
            transitionProgress += deltaTime / transitionDuration;
            if (transitionProgress >= 1.0) {
                transitionProgress = 1.0;
                inTransition = false;
                transitionFrame.clear();  // Free memory after transition
            }

            float mix = static_cast<float>(transitionProgress);
            size_t transitionSize = static_cast<size_t>(videoTexture.width) * videoTexture.height * 4;
            if (transitionSize > 0 && transitionSize <= slot.capacity && transitionFrame.size() >= transitionSize) {
                if (loopBlendScratch.size() < transitionSize) {
                    loopBlendScratch.resize(transitionSize);
                }
                const uint8_t* prevFrame = transitionFrame.data();
                const uint8_t* currFrame = static_cast<const uint8_t*>(slot.mapped);
                uint8_t* out = loopBlendScratch.data();
                for (size_t i = 0; i < transitionSize; ++i) {
                    float value = prevFrame[i] * (1.0f - mix) + currFrame[i] * mix;
                    out[i] = static_cast<uint8_t>(std::round(std::clamp(value, 0.0f, 255.0f)));
                }
                std::memcpy(slot.mapped, loopBlendScratch.data(), transitionSize);
            }
        }

        pruneVideoFrameBuffer(videoPlaybackCursor - frameDuration * 2.0);

        // Store current frame as previous frame for interpolation
        if (copySize > 0 && copySize <= videoTexture.previousFrameData.size()) {
            std::memcpy(videoTexture.previousFrameData.data(), slot.mapped, copySize);
            // Trigger upload of previous frame texture
            videoTexture.pendingUploadsPrev[writeSlot] = true;
        }

        videoTexture.stagingTimestamps[writeSlot] = targetTime;

        videoTrace("[STAGING] writing slot=", writeSlot, " size=", copySize, " fenceState=SIGNALLED t=", targetTime);
        videoTexture.pendingUploads[writeSlot] = true;
        } catch (const std::exception& e) {
            std::cerr << "[VIDEO] Exception in video decode: " << e.what() << std::endl;
            return;
        }
    }

    void destroyVideoStagingBuffers() {
        if (device == VK_NULL_HANDLE) {
            return;
        }

        g_destroyStagingCalls++;
        std::cout << "[STAGING] destroyVideoStagingBuffers call #" << g_destroyStagingCalls 
                  << " slots=" << videoTexture.stagingSlots.size() 
                  << " alive=" << g_aliveStagingBuffers << std::endl;

        for (size_t i = 0; i < videoTexture.stagingSlots.size(); ++i) {
            auto& slot = videoTexture.stagingSlots[i];

            std::cout << "[STAGING] slot=" << i
                      << " buffer=" << (void*)slot.buffer.buffer
                      << " memory=" << (void*)slot.buffer.memory
                      << " mapped=" << slot.mapped << std::endl;

            // Use resourceSystem.unmap instead of manual vkUnmapMemory
            // The allocator manages mapping/unmapping at the block level
            if (slot.mapped != nullptr && slot.buffer.type != ResourceType::Unknown) {
                resourceSystem.unmap(slot.buffer);
                slot.mapped = nullptr;
                g_aliveStagingBuffers--;
                if (g_aliveStagingBuffers < 0) {
                    std::cerr << "[STAGING] ERROR: Double-free detected! g_aliveStagingBuffers went negative: " 
                              << g_aliveStagingBuffers << std::endl;
                    std::cerr << "[STAGING] This indicates a memory corruption bug!" << std::endl;
                }
                std::cout << "[STAGING] Unmapped slot " << i << ", alive=" << g_aliveStagingBuffers << std::endl;
            }
            if (slot.buffer.type != ResourceType::Unknown) {
                resourceSystem.destroy(slot.buffer);
                slot.buffer = {};
            }
            slot.capacity = 0;
        }

        std::cout << "[STAGING] destroyVideoStagingBuffers complete, alive=" << g_aliveStagingBuffers << std::endl;
    }

    void destroyVideoTexture() {
        std::cout << "[STAGING] destroyVideoTexture called" << std::endl;
        
        // Destroy staging buffers first to prevent buffer leaks
        destroyVideoStagingBuffers();

        if (videoTexture.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, videoTexture.imageView, nullptr);
            videoTexture.imageView = VK_NULL_HANDLE;
        }
        if (videoTexture.imageViewPrev != VK_NULL_HANDLE) {
            vkDestroyImageView(device, videoTexture.imageViewPrev, nullptr);
            videoTexture.imageViewPrev = VK_NULL_HANDLE;
        }
        if (videoTexture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, videoTexture.sampler, nullptr);
            videoTexture.sampler = VK_NULL_HANDLE;
        }

        if (videoTexture.imageHandle.type != ResourceType::Unknown) {
            resourceSystem.destroy(videoTexture.imageHandle);
            videoTexture.imageHandle = {};
        }
        if (videoTexture.imageHandlePrev.type != ResourceType::Unknown) {
            resourceSystem.destroy(videoTexture.imageHandlePrev);
            videoTexture.imageHandlePrev = {};
        }
        if (videoTexture.prevFrameStagingBuffer.type != ResourceType::Unknown) {
            if (videoTexture.prevFrameStagingMapped != nullptr) {
                resourceSystem.unmap(videoTexture.prevFrameStagingBuffer);
                videoTexture.prevFrameStagingMapped = nullptr;
                g_aliveStagingBuffers--;
                if (g_aliveStagingBuffers < 0) {
                    std::cerr << "[STAGING] ERROR: Double-free detected in prev frame buffer! g_aliveStagingBuffers went negative: " 
                              << g_aliveStagingBuffers << std::endl;
                    std::cerr << "[STAGING] This indicates a memory corruption bug!" << std::endl;
                }
                std::cout << "[STAGING] Unmapped prev frame staging buffer, alive=" << g_aliveStagingBuffers << std::endl;
            }
            resourceSystem.destroy(videoTexture.prevFrameStagingBuffer);
            videoTexture.prevFrameStagingBuffer = {};
        }
        videoTexture.ready = false;
        std::cout << "[STAGING] destroyVideoTexture completed" << std::endl;
        videoTexture.descriptorInfo = {};
        videoTexture.descriptorInfoPrev = {};
        videoTexture.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        videoTexture.currentLayoutPrev = VK_IMAGE_LAYOUT_UNDEFINED;
        videoTexture.pendingUploads.fill(false);
        videoTexture.pendingUploadsPrev.fill(false);
        videoTexture.previousFrameData.clear();
    }

    void initializeVideoAssets() {
        videoRegistry.scan(videoAssetsRoot);
        const auto& assets = videoRegistry.getAssets();
        if (!assets.empty()) {
            selectedVideoAsset = 0;
            videoSourcePath = assets[0].metadata.path;
            g_project_state.active_file = videoSourcePath;  // Keep ProjectState in sync with video asset
            updateRandomizerForSelection(selectedVideoAsset, true);
            if (!assets.empty()) {
                videoRandomizer.currentVideoDuration = static_cast<float>(assets[0].metadata.duration);
            }
        } else {
            selectedVideoAsset = -1;
            videoRandomizer.recentHistory.clear();
            videoRandomizer.currentVideoDuration = 0.0f;
        }
    }

    void drawVideoAssetSelector() {
        const auto& assets = videoRegistry.getAssets();
        if (assets.empty()) {
            ImGui::TextDisabled("No videos found in %s", videoAssetsRoot.c_str());
            return;
        }

        if (selectedVideoAsset < 0 || selectedVideoAsset >= static_cast<int>(assets.size())) {
            selectedVideoAsset = 0;
            updateRandomizerForSelection(selectedVideoAsset, true);
        }

        // Display current selection
        std::string currentLabel = assets[selectedVideoAsset].metadata.filename.c_str();
        if (ImGui::BeginCombo("Video Asset", currentLabel.c_str())) {
            // Show all videos in a flat list
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                bool isSelected = (i == selectedVideoAsset);
                std::string label = assets[i].metadata.filename;
                
                // Add relative path if not in root
                fs::path assetPath(assets[i].metadata.path);
                fs::path relativePath = fs::relative(assetPath.parent_path(), videoAssetsRoot);
                if (relativePath.string() != "." && !relativePath.string().empty()) {
                    label = relativePath.string() + "/" + assets[i].metadata.filename;
                }
                
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    if (selectedVideoAsset != i) {
                        selectedVideoAsset = i;
                        updateRandomizerForSelection(i, true);
                        reloadVideoSource(assets[i].metadata.path);
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            
            ImGui::EndCombo();
        }
    }

    void updateRandomizerForSelection(int index, bool recordHistory) {
        const auto& assets = videoRegistry.getAssets();
        if (index >= 0 && index < static_cast<int>(assets.size())) {
            float duration = static_cast<float>(assets[index].metadata.duration);
            if (!std::isfinite(duration) || duration <= 0.1f) {
                duration = videoRandomizer.intervalSeconds;
            }
            videoRandomizer.currentVideoDuration = duration;
            if (recordHistory) {
                auto& history = videoRandomizer.recentHistory;
                history.erase(std::remove(history.begin(), history.end(), index), history.end());
                history.push_back(index);
                const int window = std::max(1, videoRandomizer.historyWindow);
                while (static_cast<int>(history.size()) > window) {
                    history.pop_front();
                }
            }
        }
        videoRandomizer.elapsedSeconds = 0.0f;
    }

    int chooseRandomVideoIndexAvoidingHistory(const std::vector<VideoAsset>& assets) {
        if (assets.empty()) {
            return -1;
        }
        if (assets.size() == 1) {
            return 0;
        }

        std::vector<int> pool;
        const auto& history = videoRandomizer.recentHistory;
        for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
            if (selectedVideoAsset >= 0 && i == selectedVideoAsset) {
                continue;
            }
            if (std::find(history.begin(), history.end(), i) == history.end()) {
                pool.push_back(i);
            }
        }

        if (pool.empty()) {
            for (int i = 0; i < static_cast<int>(assets.size()); ++i) {
                if (selectedVideoAsset >= 0 && i == selectedVideoAsset) {
                    continue;
                }
                pool.push_back(i);
            }
        }

        if (pool.empty()) {
            pool.push_back(selectedVideoAsset >= 0 ? selectedVideoAsset : 0);
        }

        std::uniform_int_distribution<int> dist(0, static_cast<int>(pool.size()) - 1);
        return pool[dist(randomEngine)];
    }

    void randomizeVideoAsset(bool recordHistory = true) {
        const auto& assets = videoRegistry.getAssets();
        if (assets.empty()) {
            return;
        }

        int newIndex = chooseRandomVideoIndexAvoidingHistory(assets);
        if (newIndex < 0) {
            return;
        }

        bool selectionChanged = (selectedVideoAsset != newIndex);
        selectedVideoAsset = newIndex;
        updateRandomizerForSelection(newIndex, recordHistory);
        if (selectionChanged || !videoSubsystemInitialized) {
            // Capture last frame for crossfade transition
            if (!videoFrameBuffer.empty() && videoTexture.width > 0 && videoTexture.height > 0) {
                const auto& lastFrame = videoFrameBuffer.back();
                size_t frameSize = static_cast<size_t>(lastFrame.width) * lastFrame.height * 4;
                if (transitionFrame.size() < frameSize) {
                    transitionFrame.resize(frameSize);
                }
                std::memcpy(transitionFrame.data(), lastFrame.pixels.data(), std::min(frameSize, lastFrame.pixels.size()));
                inTransition = true;
                transitionProgress = 0.0;
            }
            reloadVideoSource(assets[newIndex].metadata.path);
        }
        videoRandomizer.currentVideoDuration = static_cast<float>(expectedPlaybackDurationSeconds());
    }

    void updateVideoRandomizer(float deltaTime) {
        if (!videoRandomizer.autoRandomize) {
            videoRandomizer.elapsedSeconds = 0.0f;
            return;
        }

        const auto& assets = videoRegistry.getAssets();
        if (assets.size() <= 1) {
            videoRandomizer.elapsedSeconds = 0.0f;
            return;
        }

        videoRandomizer.intervalSeconds = std::max(1.0f, videoRandomizer.intervalSeconds);
        float durationInterval = std::max(1.0f, videoRandomizer.currentVideoDuration);
        float targetInterval = (videoRandomizer.useVideoDuration && videoRandomizer.currentVideoDuration > 0.0f)
                                   ? durationInterval
                                   : videoRandomizer.intervalSeconds;
        videoRandomizer.elapsedSeconds += deltaTime;
        if (videoRandomizer.elapsedSeconds >= targetInterval) {
            if (videoReloadInProgress) {
                return;
            }
            videoRandomizer.elapsedSeconds = 0.0f;
            randomizeVideoAsset();
        }
    }

    void reloadVideoSource(const std::string& path) {
        if (videoReloadInProgress) {
            std::cout << "[Reload] Reload already in progress, ignoring request" << std::endl;
            return;
        }

        // Check for reentrancy with swapchain recreation
        if (g_swapchainRecreating) {
            std::cout << "[Reload] QUEUED (swapchain recreation in progress)" << std::endl;
            g_pendingVideoReloadAfterSwapchain = path;
            return;
        }

        if (g_videoReloading.exchange(true)) {
            std::cout << "[Reload] SKIPPED (already reloading)" << std::endl;
            return;
        }

        if (videoSourcePath == path && videoSubsystemInitialized) {
            std::cout << "[Reload] Already playing this file, but forcing reload for new version" << std::endl;
            // Don't return - force reload even if path is same (file content may have changed)
        }

        std::cout << "[Reload] Starting reload for: " << path << " (swapchain recreating: " << g_swapchainRecreating << ")" << std::endl;

        videoReloadInProgress = true;
        videoSourcePath = path;
        g_project_state.active_file = path;  // Update ProjectState to keep it in sync

        // GPU idle ANTES de cualquier destrucción
        std::cout << "[Reload] Waiting for GPU idle..." << std::endl;
        vkDeviceWaitIdle(device);
        std::cout << "[Reload] GPU idle, destroying video player..." << std::endl;

        // Invalida uploads pendientes ANTES de destruir los buffers
        videoTexture.pendingUploads.fill(false);
        videoTexture.pendingUploadsPrev.fill(false);

        videoPlayer.shutdown();
        videoSubsystemInitialized = false;
        resetVideoPlaybackState(0.0);
        lastFrameTimestamp = std::chrono::steady_clock::now();

        // Small delay to let allocator stabilize after FFmpeg cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::cout << "[Reload] Initializing video system..." << std::endl;
        initVideoSystem();
        lastReloadTime = std::chrono::steady_clock::now();
        videoReloadInProgress = false;
        g_videoReloading = false;
        std::cout << "[Reload] Video system initialized" << std::endl;

        // Update multipass descriptors with new video texture resources
        multiPassPipeline.updateDescriptorSets(
            uniformBuffers,
            videoTexture.imageView,
            videoTexture.imageViewPrev,
            videoTexture.sampler,
            videoTexture.sampler
        );
    }

    void createDescriptorPool() {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2; // Increased for current + previous frame

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(layouts.size());
        if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate descriptor sets");
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            updateDescriptorSet(i);
        }
    }

    void updateDescriptorSet(uint32_t frameIndex) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[frameIndex];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUBO);

        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[frameIndex];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[frameIndex];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &videoTexture.descriptorInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets[frameIndex];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &videoTexture.descriptorInfoPrev;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void createCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool");
        }

        std::cout << "[CommandPool] Created successfully with queue family " << queueFamilyIndices.graphicsFamily << std::endl;
    }

    void createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        std::cout << "[CommandBuffers] Allocating " << commandBuffers.size() << " command buffers" << std::endl;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers");
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, FrameContext& frame) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer");
        }

        recordPendingVideoUpload(commandBuffer, frame.frameIndex);

        renderer.record(commandBuffer, frame);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer");
        }
    }

    void recordPendingVideoUpload(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
        if (!videoTexture.ready) {
            return;
        }
        if (frameIndex >= MAX_FRAMES_IN_FLIGHT) {
            return;
        }

        const auto& slot = videoTexture.stagingSlots[frameIndex];

        // Helper function to transition a single image
        auto transition = [&](VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                              VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkAccessFlags srcAccessMask = 0;
            VkAccessFlags dstAccessMask = 0;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            }

            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;

            vkCmdPipelineBarrier(
                commandBuffer,
                srcStage,
                dstStage,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier);
        };

        // Upload to current frame texture
        if (videoTexture.pendingUploads[frameIndex]) {
            VkImageLayout startLayout = videoTexture.currentLayout;
            VkPipelineStageFlags srcStage = (startLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

            transition(videoTexture.imageHandle.image,
                       startLayout,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       srcStage,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {videoTexture.width, videoTexture.height, 1};

            vkCmdCopyBufferToImage(
                commandBuffer,
                slot.buffer.buffer,
                videoTexture.imageHandle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region);

            transition(
                videoTexture.imageHandle.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            videoTexture.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            videoTexture.pendingUploads[frameIndex] = false;
        }

        // Upload to previous frame texture
        if (videoTexture.pendingUploadsPrev[frameIndex] && !videoTexture.previousFrameData.empty() && videoTexture.prevFrameStagingMapped != nullptr) {
            VkImageLayout startLayout = videoTexture.currentLayoutPrev;
            VkPipelineStageFlags srcStage = (startLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

            transition(videoTexture.imageHandlePrev.image,
                       startLayout,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       srcStage,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);

            // Copy previous frame data to staging buffer
            std::memcpy(videoTexture.prevFrameStagingMapped, videoTexture.previousFrameData.data(), videoTexture.frameSize);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {videoTexture.width, videoTexture.height, 1};

            vkCmdCopyBufferToImage(
                commandBuffer,
                videoTexture.prevFrameStagingBuffer.buffer,
                videoTexture.imageHandlePrev.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region);

            transition(
                videoTexture.imageHandlePrev.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            videoTexture.currentLayoutPrev = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            videoTexture.pendingUploadsPrev[frameIndex] = false;
        }
    }

    void updateUniformBuffer(uint32_t frameIndex) {
        GlobalUBO ubo{};
        auto currentTime = std::chrono::steady_clock::now();
        float time = std::chrono::duration<float>(currentTime - startTime).count();
        float proceduralTime = time * visualControls.animationSpeed;
        visualControls.activeMode = std::clamp(visualControls.activeMode, 0, 1);
        currentMode = visualControls.activeMode;

        float rotation = glm::radians(90.0f) * time;
        ubo.model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f),
                                    swapchainExtent.width / static_cast<float>(swapchainExtent.height),
                                    0.1f,
                                    10.0f);
        ubo.proj[1][1] *= -1.0f;
        ubo.resolution = glm::vec2(static_cast<float>(swapchainExtent.width),
                                   static_cast<float>(swapchainExtent.height));
        ubo.videoResolution = glm::vec2(static_cast<float>(videoTexture.width),
                                       static_cast<float>(videoTexture.height));

        // Debug: log video resolution periodically
        static int frameCounter = 0;
        if (++frameCounter % 300 == 0) {
            // std::cout << "[UBO] Video resolution: " << videoTexture.width << "x" << videoTexture.height
            //           << " Screen: " << swapchainExtent.width << "x" << swapchainExtent.height << std::endl;
        }

        ubo.time = proceduralTime;
        ubo.tempo = visualControls.tempo;
        ubo.energy = visualControls.energy;
        ubo.bass = visualControls.bass;
        ubo.mid = visualControls.mid;
        ubo.high = visualControls.high;
        ubo.primaryColor = visualControls.primaryColor;
        ubo.secondaryColor = visualControls.secondaryColor;
        ubo.colorBlend = visualControls.colorBlend;
        ubo.mode = currentMode;
        ubo.videoMix = visualControls.videoMix;
        ubo.videoAvailable = (videoSubsystemInitialized && videoTexture.ready) ? 1.0f : 0.0f;
        ubo.grayscaleAmount = visualControls.grayscaleAmount;
        ubo.sharpenAmount = visualControls.sharpenAmount;
        ubo.upscaleEnabled = visualControls.upscaleEnabled ? 1.0f : 0.0f;

        auto postCrtCurvatureEnabled = visualControls.enablePostCrtCurvature;
        ubo.crtCurvature = postCrtCurvatureEnabled ? visualControls.crtCurvature : 0.0f;
        ubo.crtHorizontalCurvature = postCrtCurvatureEnabled ? visualControls.crtHorizontalCurvature : 0.0f;

        auto postScanMaskEnabled = visualControls.enablePostScanMask;
        ubo.crtScanlineIntensity = postScanMaskEnabled ? visualControls.crtScanlineIntensity : 0.0f;
        ubo.crtMaskIntensity = postScanMaskEnabled ? visualControls.crtMaskIntensity : 0.0f;

        auto postVignetteEnabled = visualControls.enablePostVignette;
        ubo.crtVignette = postVignetteEnabled ? visualControls.crtVignette : 0.0f;

        auto postFishEyeEnabled = visualControls.enablePostFishEye;
        ubo.crtFishEye = postFishEyeEnabled ? visualControls.crtFishEye : 0.0f;

        auto postBloomEnabled = visualControls.enablePostBloom;
        ubo.bloomIntensity = postBloomEnabled ? visualControls.bloomIntensity : 0.0f;
        ubo.bloomThreshold = postBloomEnabled ? visualControls.bloomThreshold : 1.0f;

        auto postAberrationEnabled = visualControls.enablePostAberration;
        ubo.aberrationAmount = postAberrationEnabled ? visualControls.aberrationAmount : 0.0f;

        auto postGrainEnabled = visualControls.enablePostGrain;
        ubo.grainStrength = postGrainEnabled ? visualControls.grainStrength : 0.0f;

        auto postBendEnabled = visualControls.enablePostBend;
        ubo.bendAmount = postBendEnabled ? visualControls.bendAmount : 0.0f;

        auto postGlitchEnabled = visualControls.enablePostGlitch;
        auto glitchEnabled = visualControls.enableGlitch;
        float maxGlitchParam = std::max({
            visualControls.glitchDatamosh,
            visualControls.glitchRGBSplit,
            visualControls.glitchScanlineBreak,
            visualControls.glitchJitter,
            visualControls.glitchTearing,
            visualControls.glitchPixelSort,
            visualControls.glitchBufferCorruption
        });
        ubo.glitchAmount = (postGlitchEnabled ? visualControls.glitchAmount : (glitchEnabled ? maxGlitchParam : 0.0f));

        auto postColorBalanceEnabled = visualControls.enablePostColorBalance;
        ubo.colorBalance = postColorBalanceEnabled ? visualControls.colorBalance : glm::vec3(1.0f);

        auto colorEnabled = visualControls.enableColorGrading;
        ubo.gradeBrightness = colorEnabled ? visualControls.gradeBrightness : 0.0f;
        ubo.gradeContrast = colorEnabled ? visualControls.gradeContrast : 1.0f;
        ubo.gradeSaturation = colorEnabled ? visualControls.gradeSaturation : 1.0f;
        ubo.gradeHueShift = colorEnabled ? visualControls.gradeHueShift : 0.0f;
        ubo.gradeGamma = colorEnabled ? visualControls.gradeGamma : 1.0f;
        ubo.slowMotionFactor = visualControls.animationSpeed;
        ubo.temporalInterpolation = 0.0f; // TODO: Add control for this
        ubo.colorLUTIndex = colorEnabled ? visualControls.colorLUTIndex : 0;
        ubo.splitToneBalance = colorEnabled ? visualControls.splitToneBalance : 0.0f;
        ubo.splitToneShadows = colorEnabled ? visualControls.splitToneShadows : glm::vec3(1.0f);
        ubo.splitToneHighlights = colorEnabled ? visualControls.splitToneHighlights : glm::vec3(1.0f);

        auto feedbackEnabled = visualControls.enableFeedback;
        ubo.feedbackAmount = feedbackEnabled ? visualControls.feedbackAmount : 0.0f;
        ubo.trailStrength = feedbackEnabled ? visualControls.trailStrength : 0.0f;
        ubo.temporalAccumulation = feedbackEnabled ? visualControls.temporalAccumulation : 0.0f;
        ubo.feedbackDecay = feedbackEnabled ? visualControls.feedbackDecay : 0.0f;
        ubo.recursiveBlend = feedbackEnabled ? visualControls.recursiveBlend : 0.0f;

        auto distEnabled = visualControls.enableDistortion;
        ubo.uvWarpStrength = distEnabled ? visualControls.uvWarpStrength : 0.0f;
        ubo.rippleStrength = distEnabled ? visualControls.rippleStrength : 0.0f;
        ubo.rippleFrequency = distEnabled ? visualControls.rippleFrequency : 1.0f;
        ubo.swirlStrength = distEnabled ? visualControls.swirlStrength : 0.0f;
        ubo.displacementAmount = distEnabled ? visualControls.displacementAmount : 0.0f;
        ubo.kaleidoSegments = distEnabled ? visualControls.kaleidoSegments : 0.0f;
        ubo.tunnelDepth = distEnabled ? visualControls.tunnelDepth : 0.0f;
        ubo.tunnelCurvature = distEnabled ? visualControls.tunnelCurvature : 0.0f;

        auto blurEnabled = visualControls.enableBlurMotion;
        ubo.gaussianBlur = blurEnabled ? visualControls.gaussianBlur : 0.0f;
        ubo.directionalBlur = blurEnabled ? visualControls.directionalBlur : 0.0f;
        ubo.directionalBlurAngle = blurEnabled ? visualControls.directionalBlurAngle : 0.0f;
        ubo.zoomBlur = blurEnabled ? visualControls.zoomBlur : 0.0f;
        ubo.motionBlur = blurEnabled ? visualControls.motionBlur : 0.0f;
        ubo.temporalBlur = blurEnabled ? visualControls.temporalBlur : 0.0f;

        auto sharpenEnabled = visualControls.enableSharpen;
        ubo.unsharpMask = sharpenEnabled ? visualControls.unsharpMask : 0.0f;
        ubo.casAmount = sharpenEnabled ? visualControls.casAmount : 0.0f;
        ubo.localContrast = sharpenEnabled ? visualControls.localContrast : 0.0f;

        ubo.glitchDatamosh = glitchEnabled ? visualControls.glitchDatamosh : 0.0f;
        ubo.glitchRGBSplit = glitchEnabled ? visualControls.glitchRGBSplit : 0.0f;
        ubo.glitchScanlineBreak = glitchEnabled ? visualControls.glitchScanlineBreak : 0.0f;
        ubo.glitchJitter = glitchEnabled ? visualControls.glitchJitter : 0.0f;
        ubo.glitchTearing = glitchEnabled ? visualControls.glitchTearing : 0.0f;
        ubo.glitchPixelSort = glitchEnabled ? visualControls.glitchPixelSort : 0.0f;
        ubo.glitchBufferCorruption = glitchEnabled ? visualControls.glitchBufferCorruption : 0.0f;

        auto blendEnabled = visualControls.enableBlending;
        ubo.blendModeProcedural = blendEnabled ? visualControls.blendModeProcedural : 0;
        ubo.blendModeVideo = blendEnabled ? visualControls.blendModeVideo : 0;
        ubo.blendModeFeedback = blendEnabled ? visualControls.blendModeFeedback : 0;
        ubo.blendProceduralMix = blendEnabled ? visualControls.blendProceduralMix : 0.0f;
        ubo.blendVideoMix = blendEnabled ? visualControls.blendVideoMix : 0.0f;
        ubo.blendFeedbackMix = blendEnabled ? visualControls.blendFeedbackMix : 0.0f;

        auto analogEnabled = visualControls.enableAnalog;
        ubo.analogScanlineFocus = analogEnabled ? visualControls.analogScanlineFocus : 0.0f;
        ubo.analogMaskBalance = analogEnabled ? visualControls.analogMaskBalance : 0.0f;
        // TODO: Add these fields to GlobalUBO if needed
        // ubo.analogNoise = analogEnabled ? visualControls.analogNoise : 0.0f;
        // ubo.analogBloom = analogEnabled ? visualControls.analogBloom : 0.0f;
        // ubo.vhsDistortion = analogEnabled ? visualControls.vhsDistortion : 0.0f;
        // ubo.analogChromaticAberration = analogEnabled ? visualControls.analogChromaticAberration : 0.0f;

        // TODO: Add audio response fields to GlobalUBO if needed
        // auto audioEnabled = visualControls.enableAudioReactive;
        // ubo.audioWarpResponse = audioEnabled ? visualControls.audioWarpResponse : 0.0f;
        // ubo.audioFeedbackResponse = audioEnabled ? visualControls.audioFeedbackResponse : 0.0f;
        // ubo.audioBlurResponse = audioEnabled ? visualControls.audioBlurResponse : 0.0f;
        // ubo.audioColorResponse = audioEnabled ? visualControls.audioColorResponse : 0.0f;
        // ubo.audioGlitchResponse = audioEnabled ? visualControls.audioGlitchResponse : 0.0f;
        // ubo.audioBeatSync = audioEnabled ? visualControls.audioBeatSync : 0.0f;
        // ubo.audioLfoRate = audioEnabled ? visualControls.audioLfoRate : 0.0f;

        auto temporalEnabled = visualControls.enableTemporal;
        ubo.temporalInterpolation = temporalEnabled ? visualControls.temporalInterpolation : 0.0f;
        // TODO: Add to GlobalUBO if needed
        // ubo.temporalBlendStrength = temporalEnabled ? visualControls.temporalBlendStrength : 0.0f;
        ubo.slowMotionFactor = temporalEnabled ? visualControls.slowMotionFactor : 1.0f;
        ubo.frameAccumulation = temporalEnabled ? visualControls.frameAccumulation : 0.0f;

        // Extra effects (VJAY EXTRA)
        ubo.pixelateAmount = visualControls.enablePixelate ? visualControls.pixelateAmount : 0.0f;
        ubo.strobeSpeed = visualControls.enableStrobe ? visualControls.strobeSpeed : 0.0f;
        ubo.thresholdLevel = visualControls.enableThreshold ? visualControls.thresholdLevel : 0.5f;
        ubo.slowZoomAmount = visualControls.enableSlowZoom ? visualControls.slowZoomAmount : 0.0f;

        // TODO: NLE Effect Chain parameters - add fields to GlobalUBO if needed
        // if (currentEffectChain.enabled) {
        //     ubo.nleOutputWidth = 0;
        //     ubo.nleOutputHeight = 0;
        //     ubo.nleGrayscale = currentEffectChain.grayscale ? 1.0f : 0.0f;
        //     ubo.nleBrightness = currentEffectChain.brightness;
        //     ubo.nleContrast = currentEffectChain.contrast;
        //     ubo.nleSaturation = currentEffectChain.saturation;
        // }

        memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
    }

    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filename);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();

        return buffer;
    }

    static std::string resolveIncludes(const std::string& source, const std::string& basePath) {
        std::istringstream stream(source);
        std::string line;
        std::string result;

        while (std::getline(stream, line)) {
            size_t includePos = line.find("#include");
            if (includePos != std::string::npos) {
                size_t start = line.find('"', includePos);
                size_t end = line.find('"', start + 1);
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string includeFile = line.substr(start + 1, end - start - 1);
                    std::string includePath = basePath + "/" + includeFile;
                    auto includedRaw = readFile(includePath);
                    std::string includedSource(includedRaw.begin(), includedRaw.end());
                    result += resolveIncludes(includedSource, basePath);
                    continue;
                }
            }

            result += line + "\n";
        }

        return result;
    }

    static std::vector<char> loadShaderCode(const std::string& path) {
        auto raw = readFile(path);
        std::string source(raw.begin(), raw.end());
        std::string basePath = ".";
        size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            basePath = path.substr(0, slash);
        }
        std::string resolved = resolveIncludes(source, basePath);
        return std::vector<char>(resolved.begin(), resolved.end());
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        if (code.empty()) {
            throw std::runtime_error("shader code is empty");
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module");
        }

        return shaderModule;
    }

    void createVertexBuffer() {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
        ResourceHandle handle = resourceSystem.createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        void* data = resourceSystem.map(handle);
        memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
        resourceSystem.unmap(handle);

        vertexBufferHandle = std::move(handle);
    }

    void mainLoop() {
        while (running) {
            // Check for pending video reload from render completion callback
            {
                std::lock_guard<std::mutex> lock(videoReloadMutex);
                if (!pendingVideoReload.empty()) {
                    std::cout << "[Main] Reloading video: " << pendingVideoReload << std::endl;
                    reloadVideoSource(pendingVideoReload);
                    pendingVideoReload.clear();
                }
            }

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                uiSystem.processEvent(event);
                if (event.type == SDL_QUIT) {
                    running = false;
                }
                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                        running = false;
                    }
                }
                if (event.type == SDL_WINDOWEVENT) {
                    SDL_Window* sourceWindow = SDL_GetWindowFromID(event.window.windowID);
                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        if (sourceWindow == window || sourceWindow == uiWindow) {
                            running = false;
                        }
                    }
                    if (sourceWindow == window &&
                        (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                        resizePending = true;
                        resizeStableFrames = 0;
                    }
                }
            }

            if (!running) {
                break;
            }

            handleResizeHint();

            if (framebufferResized && initializationComplete) {
                recreateSwapchain();
                framebufferResized = false;
            }

            uint32_t imageIndex = 0;
            VkResult result = VK_SUCCESS;
            FrameContext& frame = frameSystem.beginFrame(swapchain, imageIndex, result);
            videoTrace("[FRAME START] cpuFrameIndex=", frame.frameIndex, " imageIndex=", imageIndex);
            lastFrameFrameIndex = frame.frameIndex;
            lastFrameImageIndex = imageIndex;

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                std::cerr << "swapchain out of date" << std::endl;
                framebufferResized = true;
                continue;
            } else if (result != VK_SUCCESS) {
                throw std::runtime_error("failed to acquire swapchain image");
            }

            if (imagesInFlight[imageIndex] != nullptr && imagesInFlight[imageIndex] != &frame) {
                vkWaitForFences(device, 1, &imagesInFlight[imageIndex]->inFlightFence, VK_TRUE, UINT64_MAX);
            }
            imagesInFlight[imageIndex] = &frame;

            auto now = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTimestamp).count();
            lastFrameTimestamp = now;
            updateVideoRandomizer(deltaTime);
            updateVideoTexture(deltaTime, frame);
            updateUniformBuffer(frame.frameIndex);

            UIDiagnostics diag;
            diag.lastFrameFrameIndex = lastFrameFrameIndex;
            diag.lastFrameImageIndex = lastFrameImageIndex;
            diag.swapchainWidth = swapchainExtent.width;
            diag.swapchainHeight = swapchainExtent.height;
            diag.currentMode = currentMode;
            diag.videoReady = videoSubsystemInitialized && videoTexture.ready;
            diag.videoWidth = videoTexture.width;
            diag.videoHeight = videoTexture.height;

            UICallbacks cb;
            cb.onReloadVideo = [this](const std::string& p) { reloadVideoSource(p); };
            cb.onRandomizeVideo = [this]() { randomizeVideoAsset(); };
            cb.onJumpRandom = [this]() { /* TODO: implement jump random */ };
            cb.onApplyChanges = [this]() { /* TODO: implement apply changes */ };
            cb.onDeleteAsset = [this](int i) { /* TODO: implement delete asset */ };
            cb.onRenameAsset = [this](int i, const std::string& n) { /* TODO: implement rename asset */ };
            cb.onControlsChanged = [this]() { controlsDirty = true; };

            uiSystem.render(visualControls, videoRandomizer, videoPlayer, videoRegistry,
                            selectedVideoAsset, transitionDuration,
                            allowDimensionChangeRecreation, controlsDirty,
                            randomEngine, diag, cb);

            if (controlsDirty) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<float>(now - lastControlSaveTime).count() > 1.0f) {
                    ControlState::save(controlStatePath, visualControls, videoRandomizer, allowDimensionChangeRecreation);
                    controlsDirty = false;
                    lastControlSaveTime = now;
                }
            }

            // Reset fence BEFORE submitting work to ensure atomic wait/reset cycle
            if (vkResetFences(device, 1, &frame.inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to reset in-flight fence");
            }

            if (vkResetCommandBuffer(commandBuffers[frame.frameIndex], 0) != VK_SUCCESS) {
                throw std::runtime_error("failed to reset command buffer");
            }

            recordCommandBuffer(commandBuffers[frame.frameIndex], frame);

            VkSemaphore waitSemaphores[] = {frame.imageAvailableSemaphore};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {swapchainRenderSemaphores[imageIndex]};

            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[frame.frameIndex];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to submit draw command buffer");
            }

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &swapchainRenderSemaphores[imageIndex];
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain;
            presentInfo.pImageIndices = &imageIndex;

            result = vkQueuePresentKHR(presentQueue, &presentInfo);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
                std::cerr << "swapchain out of date during present" << std::endl;
                framebufferResized = true;
                continue;
            } else if (result != VK_SUCCESS) {
                throw std::runtime_error("failed to present swapchain image");
            }

            frameSystem.endFrame();
        }
    }

    void cleanup() {
        ControlState::save(controlStatePath, visualControls, videoRandomizer, allowDimensionChangeRecreation);
        vkDeviceWaitIdle(device);

        uiSystem.shutdown();
        destroyUiWindow();

        multiPassPipeline.cleanup();
        videoPlayer.shutdown();
        destroyVideoTexture();  // This now calls destroyVideoStagingBuffers internally
        destroySwapchainSemaphores();

        for (size_t i = 0; i < uniformBufferHandles.size(); ++i) {
            if (uniformBuffersMapped[i] != nullptr && uniformBufferHandles[i].type != ResourceType::Unknown) {
                resourceSystem.unmap(uniformBufferHandles[i]);
                uniformBuffersMapped[i] = nullptr;
            }
        }

        for (auto& handle : uniformBufferHandles) {
            if (handle.type != ResourceType::Unknown) {
                resourceSystem.destroy(handle);
            }
        }
        uniformBufferHandles.clear();
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();

        if (vertexBufferHandle.type != ResourceType::Unknown) {
            resourceSystem.destroy(vertexBufferHandle);
            vertexBufferHandle = {};
        }

        cleanupSwapchain();

        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
        }

        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }
        if (fullscreenPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, fullscreenPipeline, nullptr);
            fullscreenPipeline = VK_NULL_HANDLE;
        }
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        if (!swapchainImageViews.empty()) {
            for (auto imageView : swapchainImageViews) {
                if (imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, imageView, nullptr);
                }
            }
            swapchainImageViews.clear();
        }

        if (descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
            descriptorSetLayout = VK_NULL_HANDLE;
        }

        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        frameSystem.cleanup();
        resourceSystem.cleanup();

        destroyDebugMessenger();

        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }

        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }

        if (window != nullptr) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }
};

int main() {
    // Register signal handlers for crashes
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGILL, crash_handler);

    App app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
