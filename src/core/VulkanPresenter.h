#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>
#include <optional>

class VulkanPresenter {
public:
    VulkanPresenter();
    ~VulkanPresenter();

    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;

    void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
              SDL_Window* window,
              uint32_t width, uint32_t height);
    void createFramebuffers(VkRenderPass renderPass);
    void cleanup();
    void recreate(uint32_t width, uint32_t height, VkRenderPass renderPass);

    VkSwapchainKHR getSwapchain() const { return swapchain; }
    VkSurfaceKHR getSurface() const { return surface; }
    VkExtent2D getExtent() const { return extent; }
    VkFormat getFormat() const { return format; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return framebuffers; }
    const std::vector<VkImageView>& getImageViews() const { return imageViews; }
    size_t getImageCount() const { return images.size(); }

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    static SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    void createFramebuffersInternal(VkRenderPass renderPass);

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    SDL_Window* window = nullptr;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};

    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;

    void createSurface();
    void createSwapchain(uint32_t width, uint32_t height);
    void createImageViews();
    void destroyFramebuffers();
    void destroyImageViews();
    void destroySwapchain();
    void destroySurface();

    VkExtent2D chooseSwapchainExtent(const SwapChainSupportDetails& support, uint32_t width, uint32_t height);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& available);
};
