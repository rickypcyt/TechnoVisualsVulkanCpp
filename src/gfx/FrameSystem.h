// gfx/FrameSystem.h
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <mutex>
#include <cstdint>
#include <string>

struct FrameContext {
    uint32_t    frameIndex           = 0;
    uint32_t    swapchainImageIndex  = 0;
    VkFence     inFlightFence        = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
};

class FrameSystem {
public:
    FrameSystem() = default;
    ~FrameSystem();

    // Non-copyable, movable
    FrameSystem(const FrameSystem&)            = delete;
    FrameSystem& operator=(const FrameSystem&) = delete;
    FrameSystem(FrameSystem&& other) noexcept;
    FrameSystem& operator=(FrameSystem&& other) noexcept;

    void init(VkDevice device, uint32_t frameCount, uint32_t swapchainImageCount);
    void cleanup();

    // Returns nullptr if result is ERROR_OUT_OF_DATE or other fatal error
    FrameContext* beginFrame(VkSwapchainKHR swapchain, uint32_t& imageIndex, VkResult& result);
    void endFrame();

    void resizeSwapchainImages(uint32_t count);
    void waitForAllFences();
    void recreateSemaphores();
    void resetCurrentFrame();

    // Safe getters — return nullopt on invalid index
    std::optional<VkFence>     getFence(uint32_t frameIndex) const;
    std::optional<VkSemaphore> getRenderFinishedSemaphore(uint32_t swapchainImageIndex) const;

    [[nodiscard]] uint32_t currentFrameIndex()   const noexcept { return m_currentFrame; }
    [[nodiscard]] uint32_t maxFrameCount()        const noexcept { return m_maxFrames; }
    [[nodiscard]] bool     isInitialized()         const noexcept { return m_device != VK_NULL_HANDLE; }

private:
    void destroyFrameSyncObjects();
    void destroySwapchainSemaphores();
    void createFrameSyncObjects(uint32_t frameCount);
    void createSwapchainSemaphores(uint32_t count);

    VkDevice                  m_device       = VK_NULL_HANDLE;
    uint32_t                  m_maxFrames    = 0;
    uint32_t                  m_currentFrame = 0;

    std::vector<FrameContext> m_frameContexts;
    std::vector<VkSemaphore>  m_renderFinishedSemaphores;
    std::vector<VkFence>      m_imagesInFlight;   // per-swapchain-image fence reference

    mutable std::mutex        m_mutex;
};