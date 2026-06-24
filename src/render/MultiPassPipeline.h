#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <array>
#include "EffectChain.h"
#include "../app/Timeline.h"
#include "../app/ProjectState.h"
#include "RenderJob.h"

// Multi-pass rendering pipeline for GPU-friendly post-processing
// Implements 7-pass architecture as specified in performance analysis
//
// PASS A — Base sampling (video / procedural / temporal)
// PASS B — Spatial effects (warp / ripple / swirl / kaleido / distortion)
// PASS C — Detail shaping (sharpen / blur)
// PASS D — Temporal domain (feedback / trail / motion blur)
// PASS E — Signal degradation (glitch / analog / VHS / chromatic)
// PASS F — Color pipeline (grading / split tone / hue / LUT)
// PASS G — Output post (bloom / CRT / grain / final mix)

class MultiPassPipeline {
public:
    struct PassConfig {
        VkPipeline computePipeline = VK_NULL_HANDLE;   // Compute dispatch for fullscreen post-processing
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayouts[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE}; // Set 0: UBOs, Set 1: Textures + Storage
        std::vector<VkDescriptorSet> descriptorSets[2]; // descriptorSets[0] for UBOs, descriptorSets[1] for textures
    };

    struct FramebufferResources {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct ImageResource {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
    };

    MultiPassPipeline();
    ~MultiPassPipeline();

    // Initialize the multi-pass pipeline
    bool initialize(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkQueue graphicsQueue,
        uint32_t graphicsQueueFamilyIndex,
        VkExtent2D extent,
        VkFormat colorFormat,
        VkSampler videoSampler,
        VkSampler videoSamplerPrev,
        VkImageView videoImageView,
        VkImageView videoPrevImageView,
        VkSampler video2Sampler,
        VkImageView video2ImageView,
        VkSampler video3Sampler,
        VkImageView video3ImageView,
        const std::vector<VkBuffer>& uniformBuffers,
        size_t uniformBufferSize
    );

    // Cleanup all resources
    void cleanup();

    // Execute all 7 passes (offscreen) + final pass to swapchain
    void execute(VkCommandBuffer cmd, uint32_t frameIndex, VkDescriptorSet uboDescriptorSet,
                 VkRenderPass swapchainRenderPass, const std::vector<VkFramebuffer>& swapchainFramebuffers,
                 uint32_t swapchainImageIndex, VkPipeline swapchainPipeline, VkPipelineLayout swapchainPipelineLayout,
                 VkDescriptorSet swapchainDescriptorSet, VkExtent2D swapchainExtent, VkSampler swapchainSampler,
                 int previewOverlay = 0);

    // Get final output framebuffer (for presentation)
    VkImageView getFinalOutput() const { return intermediate[1].imageView; }

    static constexpr int NUM_PASSES = 7;

    // GPU profiling constants
    static constexpr int QUERIES_PER_PASS = 2;
    static constexpr int PROFILED_PASS_COUNT = NUM_PASSES + 1; // +1 for swapchain final
    static constexpr int TOTAL_QUERIES = PROFILED_PASS_COUNT * QUERIES_PER_PASS;

    // GPU profiling data (updated each frame)
    std::array<float, PROFILED_PASS_COUNT> lastGpuPassTimes{};
    float lastGpuTotalTime = 0.0f;

    // Update descriptor sets with texture bindings
    void updateDescriptorSets(
        const std::vector<VkBuffer>& uniformBuffers,
        VkImageView videoImageView,
        VkImageView videoPrevImageView,
        VkSampler videoSampler,
        VkSampler videoSamplerPrev,
        VkImageView video2ImageView = VK_NULL_HANDLE,
        VkSampler video2Sampler = VK_NULL_HANDLE,
        VkImageView video3ImageView = VK_NULL_HANDLE,
        VkSampler video3Sampler = VK_NULL_HANDLE
    );

    // Recreate on resize
    void recreate(VkExtent2D extent);

    // Get final output framebuffer (for presentation)
    VkFramebuffer getFinalFramebuffer() const { return swapchainFramebuffer; }

    // Pass culling: enable/disable individual passes at runtime
    void setPassEnabled(int pass, bool enabled);
    bool isPassEnabled(int pass) const;

    // GPU profiling (timestamps)
    void initProfiling(VkDevice device);
    void cleanupProfiling(VkDevice device);
    void printProfilingResults(VkDevice device);

private:
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = 0;
    VkExtent2D extent{};
    VkFormat colorFormat{};

    // Input resources
    VkSampler videoSampler = VK_NULL_HANDLE;
    VkSampler videoSamplerPrev = VK_NULL_HANDLE;
    VkImageView videoImageView = VK_NULL_HANDLE;
    VkImageView videoPrevImageView = VK_NULL_HANDLE;
    VkSampler video2Sampler = VK_NULL_HANDLE;
    VkImageView video2ImageView = VK_NULL_HANDLE;
    VkSampler video3Sampler = VK_NULL_HANDLE;
    VkImageView video3ImageView = VK_NULL_HANDLE;
    std::vector<VkBuffer> uniformBuffers;
    size_t uniformBufferSize = 0;

    // Pipeline configurations for each pass
    PassConfig passes[NUM_PASSES];

    // Pass culling state (all enabled by default)
    bool passEnabled[NUM_PASSES] = {true, true, true, true, true, true, true};

    // Intermediate framebuffers (ping-pong between passes)
    // We need 2 intermediate textures plus final output
    FramebufferResources intermediate[2];  // Ping-pong buffers
    VkFramebuffer swapchainFramebuffer = VK_NULL_HANDLE;  // Final output to swapchain
    ImageResource temporalHistory;
    bool temporalHistoryInitialized = false;
    std::array<VkImageLayout, 2> intermediateLayouts{VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout temporalHistoryLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Descriptor pool for all passes
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // GPU profiling
    VkQueryPool queryPool = VK_NULL_HANDLE;

    // Initialization helpers
    bool createIntermediateFramebuffers();
    bool createTemporalHistoryImage();
    bool createComputePipelines();
    bool createDescriptorSets();
    
    void cleanupIntermediateFramebuffers();
    void destroyTemporalHistoryImage();
    void cleanupPipelines();
    void cleanupDescriptorSets();

    // Shader loading
    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    
    // Memory helper
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkPipelineStageFlags srcStageMask,
                               VkPipelineStageFlags dstStageMask);
};
