#include "VulkanPresenter.h"
#include <SDL2/SDL_vulkan.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <limits>

VulkanPresenter::VulkanPresenter() = default;

VulkanPresenter::~VulkanPresenter() {
    cleanup();
}

void VulkanPresenter::init(VkInstance inst, VkPhysicalDevice physDevice, VkDevice dev,
                             SDL_Window* win,
                             uint32_t width, uint32_t height) {
    instance = inst;
    physicalDevice = physDevice;
    device = dev;
    window = win;

    createSurface();
    createSwapchain(width, height);
    createImageViews();
}

void VulkanPresenter::createFramebuffers(VkRenderPass renderPass) {
    createFramebuffersInternal(renderPass);
}

void VulkanPresenter::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    destroyFramebuffers();
    destroyImageViews();
    destroySwapchain();
    destroySurface();

    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    window = nullptr;
}

void VulkanPresenter::recreate(uint32_t width, uint32_t height, VkRenderPass renderPass) {
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device);

    destroyFramebuffers();
    destroyImageViews();
    destroySwapchain();

    createSwapchain(width, height);
    createImageViews();
    createFramebuffersInternal(renderPass);
}

// ── Surface ──────────────────────────────────────────────────────────────────

void VulkanPresenter::createSurface() {
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        throw std::runtime_error(std::string("failed to create surface: ") + SDL_GetError());
    }
}

void VulkanPresenter::destroySurface() {
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
}

// ── Swapchain ────────────────────────────────────────────────────────────────

void VulkanPresenter::createSwapchain(uint32_t width, uint32_t height) {
    auto support = querySwapChainSupport(physicalDevice, surface);

    if (support.formats.empty()) {
        throw std::runtime_error("no swapchain formats available");
    }
    if (support.presentModes.empty()) {
        throw std::runtime_error("no swapchain present modes available");
    }

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D ext = chooseSwapchainExtent(support, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = ext;
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
    images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

    format = surfaceFormat.format;
    extent = ext;
}

void VulkanPresenter::destroySwapchain() {
    if (swapchain != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    images.clear();
}

// ── Image views ──────────────────────────────────────────────────────────────

void VulkanPresenter::createImageViews() {
    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &createInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views");
        }
    }
}

void VulkanPresenter::destroyImageViews() {
    for (auto& view : imageViews) {
        if (view != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    imageViews.clear();
}

// ── Framebuffers ─────────────────────────────────────────────────────────────

void VulkanPresenter::createFramebuffersInternal(VkRenderPass renderPass) {
    framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass;
        info.attachmentCount = 1;
        info.pAttachments = &imageViews[i];
        info.width = extent.width;
        info.height = extent.height;
        info.layers = 1;

        if (vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
}

void VulkanPresenter::destroyFramebuffers() {
    for (auto& fb : framebuffers) {
        if (fb != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
            fb = VK_NULL_HANDLE;
        }
    }
    framebuffers.clear();
}

// ── Helpers ────────────────────────────────────────────────────────────────────

VulkanPresenter::SwapChainSupportDetails VulkanPresenter::querySwapChainSupport(VkPhysicalDevice dev, VkSurfaceKHR surf) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surf, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surf, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surf, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surf, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surf, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkExtent2D VulkanPresenter::chooseSwapchainExtent(const SwapChainSupportDetails& support, uint32_t width, uint32_t height) {
    if (support.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return support.capabilities.currentExtent;
    }
    VkExtent2D ext{};
    ext.width = std::clamp(width, support.capabilities.minImageExtent.width, support.capabilities.maxImageExtent.width);
    ext.height = std::clamp(height, support.capabilities.minImageExtent.height, support.capabilities.maxImageExtent.height);
    return ext;
}

VkSurfaceFormatKHR VulkanPresenter::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) {
    for (const auto& fmt : available) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    return available[0];
}

VkPresentModeKHR VulkanPresenter::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& available) {
    for (const auto& mode : available) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}
