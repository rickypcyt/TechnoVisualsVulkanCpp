#include "VulkanContext.h"
#include <SDL2/SDL_vulkan.h>
#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <limits>

#ifdef NDEBUG
const bool kEnableValidationLayersDefault = false;
#else
const bool kEnableValidationLayersDefault = true;
#endif

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext() {
    cleanupSwapchain();
    
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }
    
    destroyDebugMessenger();
    
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    
    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
    }
    
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
}

void VulkanContext::init(SDL_Window* window, bool enableValidation) {
    enableValidationLayers = enableValidation;
    
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers not available");
    }
    
    createVulkanInstance(window);
    setupDebugMessenger();
}

void VulkanContext::createSurface(SDL_Window* window) {
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        throw std::runtime_error(std::string("failed to create surface: ") + SDL_GetError());
    }
    
    pickPhysicalDevice(surface);
    createLogicalDevice();
}

void VulkanContext::createCommandPool() {
    if (!queueFamilyIndices.graphicsFamilyFound || queueFamilyIndices.graphicsFamily == UINT32_MAX) {
        throw std::runtime_error("cannot create command pool: invalid graphics queue family");
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool");
    }

    std::cout << "[VulkanContext] Command pool created with queue family " << queueFamilyIndices.graphicsFamily << std::endl;
}

void VulkanContext::createSwapchain(uint32_t width, uint32_t height) {
    auto support = querySwapChainSupport(physicalDevice, surface);

    if (support.formats.empty()) {
        throw std::runtime_error("no swapchain formats available");
    }
    if (support.presentModes.empty()) {
        throw std::runtime_error("no swapchain present modes available");
    }

    VkSurfaceFormatKHR surfaceFormat = support.formats[0];
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    VkExtent2D extent = chooseSwapchainExtent(support, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    std::cout << "[VulkanContext] Swapchain extent: " << extent.width << "x" << extent.height << std::endl;
    std::cout << "[VulkanContext] Swapchain image count: " << imageCount << std::endl;

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

    createImageViews();
    createSwapchainSemaphores();

    std::cout << "[VulkanContext] Swapchain created successfully" << std::endl;
}

void VulkanContext::cleanupSwapchain() {
    vkDeviceWaitIdle(device);
    destroySwapchainSemaphores();
    
    for (auto imageView : swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView, nullptr);
        }
    }
    swapchainImageViews.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    swapchainImages.clear();
}

void VulkanContext::recreateSwapchain(uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(device);
    cleanupSwapchain();
    createSwapchain(width, height);
}

void VulkanContext::createVulkanInstance(SDL_Window* window) {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan App";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> extensions = getRequiredExtensions(window);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = enableValidationLayers ?
        static_cast<uint32_t>(kValidationLayers.size()) : 0;
    createInfo.ppEnabledLayerNames = enableValidationLayers ?
        kValidationLayers.data() : nullptr;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }
}

void VulkanContext::setupDebugMessenger() {
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

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func == nullptr || func(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger");
    }
}

void VulkanContext::destroyDebugMessenger() {
    if (!enableValidationLayers || debugMessenger == VK_NULL_HANDLE) {
        return;
    }

    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        func(instance, debugMessenger, nullptr);
    }
    debugMessenger = VK_NULL_HANDLE;
}

void VulkanContext::pickPhysicalDevice(VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    std::cout << "[VulkanContext] Found " << deviceCount << " physical devices" << std::endl;

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // List all available GPUs
    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        const char* typeStr = "Unknown";
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeStr = "Integrated"; break;
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: typeStr = "Discrete"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: typeStr = "Virtual"; break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU: typeStr = "CPU"; break;
            default: typeStr = "Other"; break;
        }
        std::cout << "[VulkanContext]   [" << i << "] " << props.deviceName << " (" << typeStr << ")" << std::endl;
    }

    // Check for manual GPU selection via environment variable
    const char* gpuIndexEnv = std::getenv("VULKAN_GPU_INDEX");
    if (gpuIndexEnv != nullptr) {
        uint32_t selectedIndex = std::stoi(gpuIndexEnv);
        if (selectedIndex < deviceCount && isDeviceSuitable(devices[selectedIndex], surface)) {
            physicalDevice = devices[selectedIndex];
            queueFamilyIndices = findQueueFamilies(physicalDevice, surface);
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physicalDevice, &props);
            std::cout << "[VulkanContext] Manually selected GPU [" << selectedIndex << "]: " << props.deviceName << std::endl;
            std::cout << "[VulkanContext] Graphics queue family: " << queueFamilyIndices.graphicsFamily << std::endl;
            std::cout << "[VulkanContext] Present queue family: " << queueFamilyIndices.presentFamily << std::endl;
            return;
        } else {
            std::cerr << "[VulkanContext] Warning: Invalid GPU index " << selectedIndex << " or device not suitable, using automatic selection" << std::endl;
        }
    }

    // First pass: try to find a discrete GPU
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && isDeviceSuitable(device, surface)) {
            physicalDevice = device;
            queueFamilyIndices = findQueueFamilies(device, surface);
            std::cout << "[VulkanContext] Selected GPU: " << props.deviceName << " (Discrete)" << std::endl;
            std::cout << "[VulkanContext] Graphics queue family: " << queueFamilyIndices.graphicsFamily << std::endl;
            std::cout << "[VulkanContext] Present queue family: " << queueFamilyIndices.presentFamily << std::endl;
            return;
        }
    }

    // Second pass: fallback to any suitable GPU (integrated, etc.)
    for (const auto& device : devices) {
        if (isDeviceSuitable(device, surface)) {
            physicalDevice = device;
            queueFamilyIndices = findQueueFamilies(device, surface);

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            std::cout << "[VulkanContext] Selected GPU: " << props.deviceName << " (Fallback)" << std::endl;
            std::cout << "[VulkanContext] Graphics queue family: " << queueFamilyIndices.graphicsFamily << std::endl;
            std::cout << "[VulkanContext] Present queue family: " << queueFamilyIndices.presentFamily << std::endl;
            return;
        }
    }

    throw std::runtime_error("failed to find a suitable GPU!");
}

void VulkanContext::createLogicalDevice() {
    if (!queueFamilyIndices.graphicsFamilyFound || queueFamilyIndices.graphicsFamily == UINT32_MAX) {
        throw std::runtime_error("cannot create logical device: invalid graphics queue family");
    }
    if (!queueFamilyIndices.presentFamilyFound || queueFamilyIndices.presentFamily == UINT32_MAX) {
        throw std::runtime_error("cannot create logical device: invalid present queue family");
    }

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

void VulkanContext::createImageViews() {
    swapchainImageViews.resize(swapchainImages.size());

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
}

void VulkanContext::createSwapchainSemaphores() {
    destroySwapchainSemaphores();

    swapchainRenderSemaphores.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (size_t i = 0; i < swapchainRenderSemaphores.size(); ++i) {
        if (vkCreateSemaphore(device, &info, nullptr, &swapchainRenderSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swapchain render semaphore");
        }
    }
}

void VulkanContext::destroySwapchainSemaphores() {
    for (auto& semaphore : swapchainRenderSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    swapchainRenderSemaphores.clear();
}

VulkanContext::QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

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

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
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

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = findQueueFamilies(device, surface);

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

    bool isDiscrete = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (isDiscrete) {
        std::cout << "[VulkanContext] Device is discrete GPU" << std::endl;
    } else {
        std::cout << "[VulkanContext] Device is integrated or other GPU type" << std::endl;
    }

    return true;
}

VulkanContext::SwapChainSupportDetails VulkanContext::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkExtent2D VulkanContext::chooseSwapchainExtent(const SwapChainSupportDetails& support, uint32_t width, uint32_t height) {
    if (support.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return support.capabilities.currentExtent;
    }

    VkExtent2D extent{};
    extent.width = std::clamp(width, support.capabilities.minImageExtent.width, support.capabilities.maxImageExtent.width);
    extent.height = std::clamp(height, support.capabilities.minImageExtent.height, support.capabilities.maxImageExtent.height);
    return extent;
}

std::vector<const char*> VulkanContext::getRequiredExtensions(SDL_Window* window) {
    uint32_t extensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr);

    std::vector<const char*> extensions(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data());

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanContext::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : kValidationLayers) {
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

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {

    std::cerr << "[VULKAN] " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}
