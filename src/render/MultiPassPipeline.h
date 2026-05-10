#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
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
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;
    };

    struct FramebufferResources {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
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
        const std::vector<VkBuffer>& uniformBuffers,
        size_t uniformBufferSize
    );

    // Cleanup all resources
    void cleanup();

    // Execute all 7 passes (offscreen) + final pass to swapchain
    void execute(VkCommandBuffer cmd, uint32_t frameIndex, VkDescriptorSet uboDescriptorSet,
                 VkRenderPass swapchainRenderPass, std::vector<VkFramebuffer>& swapchainFramebuffers,
                 uint32_t swapchainImageIndex, VkPipeline swapchainPipeline, VkPipelineLayout swapchainPipelineLayout,
                 VkDescriptorSet swapchainDescriptorSet, VkExtent2D swapchainExtent, VkSampler swapchainSampler);

    // Get final output framebuffer (for presentation)
    VkImageView getFinalOutput() const { return intermediate[1].imageView; }

    // Update descriptor sets with texture bindings
    void updateDescriptorSets(
        const std::vector<VkBuffer>& uniformBuffers,
        VkImageView videoImageView,
        VkImageView videoPrevImageView,
        VkSampler videoSampler,
        VkSampler videoSamplerPrev
    );

    // Recreate on resize
    void recreate(VkExtent2D extent);

    // Get final output framebuffer (for presentation)
    VkFramebuffer getFinalFramebuffer() const { return swapchainFramebuffer; }

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
    std::vector<VkBuffer> uniformBuffers;
    size_t uniformBufferSize = 0;

    // Pipeline configurations for each pass
    static const int NUM_PASSES = 7;
    PassConfig passes[NUM_PASSES];

    // Intermediate framebuffers (ping-pong between passes)
    // We need 2 intermediate textures plus final output
    FramebufferResources intermediate[2];  // Ping-pong buffers
    VkFramebuffer swapchainFramebuffer = VK_NULL_HANDLE;  // Final output to swapchain

    // Render pass for offscreen rendering
    VkRenderPass offscreenRenderPass = VK_NULL_HANDLE;
    
    // Descriptor pool for all passes
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    // Fullscreen quad vertex buffer
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;

    // Initialization helpers
    bool createOffscreenRenderPass();
    bool createIntermediateFramebuffers();
    bool createPipelines();
    bool createDescriptorSets();
    bool createFullscreenQuad();
    
    void cleanupIntermediateFramebuffers();
    void cleanupPipelines();
    void cleanupDescriptorSets();
    void cleanupFullscreenQuad();

    // Shader loading
    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    
    // Memory helper
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
