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
    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
    }

    destroyDebugMessenger();

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

void VulkanContext::selectDevice(VkSurfaceKHR surface) {
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

    // Helper: select and print a device
    auto selectDevice = [&](VkPhysicalDevice dev, const char* reason) {
        physicalDevice = dev;
        queueFamilyIndices = findQueueFamilies(dev, surface);
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        std::cout << "[VulkanContext] Selected GPU: " << props.deviceName << " (" << reason << ")" << std::endl;
        std::cout << "[VulkanContext] Graphics queue family: " << queueFamilyIndices.graphicsFamily << std::endl;
        std::cout << "[VulkanContext] Present queue family: " << queueFamilyIndices.presentFamily << std::endl;
    };

    // Check for manual GPU selection by index (unstable order, use with care)
    const char* gpuIndexEnv = std::getenv("VULKAN_GPU_INDEX");
    if (gpuIndexEnv != nullptr) {
        uint32_t selectedIndex = std::stoi(gpuIndexEnv);
        if (selectedIndex < deviceCount && isDeviceSuitable(devices[selectedIndex], surface)) {
            selectDevice(devices[selectedIndex], "Manual index");
            return;
        } else {
            std::cerr << "[VulkanContext] Warning: Invalid GPU index " << selectedIndex << " or device not suitable" << std::endl;
        }
    }

    // Check for stable GPU selection by type (integrated / discrete)
    const char* gpuTypeEnv = std::getenv("VULKAN_GPU_TYPE");
    if (gpuTypeEnv != nullptr) {
        std::string typeStr(gpuTypeEnv);
        bool wantDiscrete = false;
        bool wantIntegrated = false;
        if (typeStr == "discrete" || typeStr == "dgpu" || typeStr == "d" || typeStr == "1") {
            wantDiscrete = true;
        } else if (typeStr == "integrated" || typeStr == "igpu" || typeStr == "i" || typeStr == "0") {
            wantIntegrated = true;
        }

        if (wantDiscrete || wantIntegrated) {
            VkPhysicalDeviceType targetType = wantDiscrete ? VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU : VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
            for (const auto& device : devices) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(device, &props);
                if (props.deviceType == targetType && isDeviceSuitable(device, surface)) {
                    selectDevice(device, wantDiscrete ? "Discrete (forced)" : "Integrated (forced)");
                    return;
                }
            }
            std::cerr << "[VulkanContext] Warning: No " << (wantDiscrete ? "discrete" : "integrated") << " GPU found or suitable, falling back to auto" << std::endl;
        }
    }

    // Auto-selection: prefer discrete GPU
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && isDeviceSuitable(device, surface)) {
            selectDevice(device, "Discrete (auto)");
            return;
        }
    }

    // Fallback to any suitable GPU
    for (const auto& device : devices) {
        if (isDeviceSuitable(device, surface)) {
            selectDevice(device, "Fallback");
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
