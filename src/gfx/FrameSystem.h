#pragma once
#include <vulkan/vulkan.h>
#include <vector>

struct FrameContext {
    uint32_t frameIndex;
    VkCommandBuffer commandBuffer;
    VkFence inFlightFence;
    VkSemaphore imageAvailableSemaphore;
    uint32_t swapchainImageIndex;
};

// FrameSystem - manages per-frame synchronization objects (semaphores, fences)
class FrameSystem {
public:
    void init(VkDevice deviceHandle, uint32_t frameCount, uint32_t swapchainImageCount);

    FrameContext& beginFrame(VkSwapchainKHR swapchain, uint32_t& imageIndex, VkResult& result);
    void endFrame();
    void resizeSwapchainImages(size_t count);
    void resetCurrentFrame();
    VkFence getFence(uint32_t frameIndex);
    VkSemaphore getRenderFinishedSemaphore(uint32_t swapchainImageIndex) const;
    void cleanup();
    void waitForAllFences();
    void recreateSemaphores();

private:
    VkDevice device = VK_NULL_HANDLE;
    uint32_t maxFrames = 0;
    uint32_t currentFrame = 0;
    std::vector<FrameContext> frameContexts;
    std::vector<VkFence> imagesInFlight;
    std::vector<VkSemaphore> renderFinishedSemaphores;
};
