#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <vector>
#include <optional>
#include <cstdint>

class VulkanContext {
public:
    struct QueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;
        bool graphicsFamilyFound = false;
        bool presentFamilyFound = false;
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    VulkanContext();
    ~VulkanContext();

    // Initialization
    void init(SDL_Window* window, bool enableValidationLayers = true);
    void createSurface(SDL_Window* window);
    void createCommandPool();

    // Swapchain management
    void createSwapchain(uint32_t width, uint32_t height);
    void cleanupSwapchain();
    void recreateSwapchain(uint32_t width, uint32_t height);

    // Getters
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkSurfaceKHR getSurface() const { return surface; }
    VkSwapchainKHR getSwapchain() const { return swapchain; }
    VkCommandPool getCommandPool() const { return commandPool; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    
    const QueueFamilyIndices& getQueueFamilyIndices() const { return queueFamilyIndices; }
    VkExtent2D getSwapchainExtent() const { return swapchainExtent; }
    VkFormat getSwapchainImageFormat() const { return swapchainImageFormat; }
    const std::vector<VkImage>& getSwapchainImages() const { return swapchainImages; }
    const std::vector<VkImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
    const std::vector<VkSemaphore>& getSwapchainSemaphores() const { return swapchainRenderSemaphores; }
    size_t getSwapchainImageCount() const { return swapchainImages.size(); }

    // Validation layers
    bool isValidationEnabled() const { return enableValidationLayers; }

private:
    // Instance and device
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    
    // Queues
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    
    // Command pool
    VkCommandPool commandPool = VK_NULL_HANDLE;
    
    // Swapchain
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkSemaphore> swapchainRenderSemaphores;
    
    // Queue family indices
    QueueFamilyIndices queueFamilyIndices;
    
    // Validation
    bool enableValidationLayers = false;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    
    // Frame synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Helper functions
    void createVulkanInstance(SDL_Window* window);
    void setupDebugMessenger();
    void destroyDebugMessenger();
    void pickPhysicalDevice(VkSurfaceKHR surface);
    void createLogicalDevice();
    void createImageViews();
    void createSwapchainSemaphores();
    void destroySwapchainSemaphores();

    // Device selection helpers
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
    VkExtent2D chooseSwapchainExtent(const SwapChainSupportDetails& support, uint32_t width, uint32_t height);
    std::vector<const char*> getRequiredExtensions(SDL_Window* window);
    bool checkValidationLayerSupport();

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData);
};
