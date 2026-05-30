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

    VulkanContext();
    ~VulkanContext();

    // Initialization
    void init(SDL_Window* window, bool enableValidationLayers = true);
    void selectDevice(VkSurfaceKHR surface);
    void createCommandPool();

    // Getters
    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }
    VkCommandPool getCommandPool() const { return commandPool; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }

    const QueueFamilyIndices& getQueueFamilyIndices() const { return queueFamilyIndices; }

    // Validation layers
    bool isValidationEnabled() const { return enableValidationLayers; }

private:
    // Instance and device
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    // Queues
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    // Command pool
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Queue family indices
    QueueFamilyIndices queueFamilyIndices;

    // Validation
    bool enableValidationLayers = false;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Helper functions
    void createVulkanInstance(SDL_Window* window);
    void setupDebugMessenger();
    void destroyDebugMessenger();
    void pickPhysicalDevice(VkSurfaceKHR surface);
    void createLogicalDevice();

    // Device selection helpers
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
    std::vector<const char*> getRequiredExtensions(SDL_Window* window);
    bool checkValidationLayerSupport();

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData);
};
