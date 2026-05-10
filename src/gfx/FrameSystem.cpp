#include "gfx/FrameSystem.h"
#include <stdexcept>
#include <iostream>

void FrameSystem::init(VkDevice deviceHandle, uint32_t frameCount) {
    device = deviceHandle;
    maxFrames = frameCount;
    currentFrame = 0;
    frameContexts.resize(maxFrames);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < maxFrames; ++i) {
        FrameContext ctx{};
        ctx.frameIndex = i;

        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &ctx.inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for frame system");
        }

        frameContexts[i] = ctx;
    }

    std::cout << "[FrameSystem] Initialized with " << maxFrames << " frames" << std::endl;
}

FrameContext& FrameSystem::beginFrame(VkSwapchainKHR swapchain, uint32_t& imageIndex, VkResult& result) {
    FrameContext& frame = frameContexts[currentFrame];

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
        if (imageIndex >= imagesInFlight.size()) {
            imagesInFlight.resize(imageIndex + 1, VK_NULL_HANDLE);
        }

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
    imagesInFlight.assign(count, VK_NULL_HANDLE);
}

void FrameSystem::clearImageTracking() {
    imagesInFlight.clear();
}

void FrameSystem::resetCurrentFrame() {
    currentFrame = 0;
}

VkFence FrameSystem::getFence(uint32_t frameIndex) {
    if (frameIndex >= frameContexts.size()) {
        return VK_NULL_HANDLE;
    }
    return frameContexts[frameIndex].inFlightFence;
}

void FrameSystem::cleanup() {
    for (auto& frame : frameContexts) {
        if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (frame.renderFinishedSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr);
            frame.renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }
    }

    frameContexts.clear();
    imagesInFlight.clear();
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
