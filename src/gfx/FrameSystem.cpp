#include "gfx/FrameSystem.h"
#include <stdexcept>
#include <iostream>

void FrameSystem::init(VkDevice deviceHandle, uint32_t frameCount, uint32_t swapchainImageCount) {
    if (frameCount == 0) {
        throw std::runtime_error("FrameSystem initialized with zero frames");
    }
    if (swapchainImageCount == 0) {
        throw std::runtime_error("FrameSystem initialized with zero swapchain images");
    }

    device = deviceHandle;
    maxFrames = frameCount;
    currentFrame = 0;
    frameContexts.resize(maxFrames);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < maxFrames; ++i) {
        FrameContext ctx{};
        ctx.frameIndex = i;

        if (vkCreateFence(device, &fenceInfo, nullptr, &ctx.inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fence for frame system");
        }

        // Create imageAvailable semaphore for this frame
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.imageAvailableSemaphore) != VK_SUCCESS) {
            vkDestroyFence(device, ctx.inFlightFence, nullptr);
            throw std::runtime_error("failed to create image available semaphore");
        }

        frameContexts[i] = ctx;
    }

    // Create renderFinished semaphores for each swapchain image
    renderFinishedSemaphores.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render finished semaphore");
        }
    }

    imagesInFlight.resize(swapchainImageCount, VK_NULL_HANDLE);

    std::cout << "[FrameSystem] Initialized with " << maxFrames << " frames and " << swapchainImageCount << " swapchain image semaphores" << std::endl;
}

FrameContext& FrameSystem::beginFrame(VkSwapchainKHR swapchain, uint32_t& imageIndex, VkResult& result) {
    if (frameContexts.empty()) {
        throw std::runtime_error("FrameSystem::beginFrame called with no frames initialized");
    }
    if (currentFrame >= frameContexts.size()) {
        throw std::runtime_error("FrameSystem::beginFrame currentFrame out of bounds");
    }

    FrameContext& frame = frameContexts[currentFrame];

    if (frame.inFlightFence == VK_NULL_HANDLE || frame.imageAvailableSemaphore == VK_NULL_HANDLE) {
        throw std::runtime_error("FrameContext not properly initialized");
    }

    // Wait for previous frame to complete
    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    result = vkAcquireNextImageKHR(
        device,
        swapchain,
        UINT64_MAX,
        frame.imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        // Ensure imagesInFlight is large enough
        if (imageIndex >= imagesInFlight.size()) {
            imagesInFlight.resize(imageIndex + 1, VK_NULL_HANDLE);
        }

        // Wait for previous owner of this image
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }

        imagesInFlight[imageIndex] = frame.inFlightFence;
        frame.swapchainImageIndex = imageIndex;
    }

    return frame;
}

void FrameSystem::endFrame() {
    if (maxFrames == 0) {
        return;
    }
    currentFrame = (currentFrame + 1) % maxFrames;
}

void FrameSystem::resizeSwapchainImages(size_t count) {
    // Destroy old renderFinished semaphores
    for (auto semaphore : renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }

    // Create new renderFinished semaphores
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    renderFinishedSemaphores.resize(count);

    for (uint32_t i = 0; i < count; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render finished semaphore during resize");
        }
    }

    imagesInFlight.assign(count, VK_NULL_HANDLE);
}

VkFence FrameSystem::getFence(uint32_t frameIndex) {
    if (frameIndex >= frameContexts.size()) {
        return VK_NULL_HANDLE;
    }
    return frameContexts[frameIndex].inFlightFence;
}

VkSemaphore FrameSystem::getRenderFinishedSemaphore(uint32_t swapchainImageIndex) const {
    if (swapchainImageIndex >= renderFinishedSemaphores.size()) {
        return VK_NULL_HANDLE;
    }
    return renderFinishedSemaphores[swapchainImageIndex];
}

void FrameSystem::resetCurrentFrame() {
    currentFrame = 0;
}

void FrameSystem::cleanup() {
    for (auto& frame : frameContexts) {
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
        }
    }

    for (auto semaphore : renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }

    frameContexts.clear();
    imagesInFlight.clear();
    renderFinishedSemaphores.clear();
    device = VK_NULL_HANDLE;
    maxFrames = 0;
    currentFrame = 0;
}

void FrameSystem::waitForAllFences() {
    for (auto& frame : frameContexts) {
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
        }
    }
}

void FrameSystem::recreateSemaphores() {
    waitForAllFences();

    uint32_t savedMaxFrames = maxFrames;
    uint32_t savedSwapchainImageCount = static_cast<uint32_t>(renderFinishedSemaphores.size());
    VkDevice savedDevice = device;

    cleanup();

    init(savedDevice, savedMaxFrames, savedSwapchainImageCount);
}
