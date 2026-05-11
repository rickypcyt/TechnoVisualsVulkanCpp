#include "VideoTexture.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

VideoTexture::VideoTexture() = default;

VideoTexture::~VideoTexture() {
    // Resources should be destroyed via destroy() before destructor
}

void VideoTexture::createResources(ResourceSystem& resourceSystem, VkDevice device, VkCommandPool commandPool,
                                    VkQueue graphicsQueue, uint32_t width, uint32_t height) {
    // Check if recreation is necessary
    bool buffersValid = !stagingSlots.empty();
    for (size_t i = 0; i < stagingSlots.size() && buffersValid; ++i) {
        if (stagingSlots[i].mapped == nullptr) {
            buffersValid = false;
        }
    }

    size_t requiredSize = static_cast<size_t>(width) * height * 4;
    size_t allocatedSize = frameSize;

    std::cout << "[VideoTexture] Dimension check: allocated=" << allocatedSize 
              << " required=" << requiredSize 
              << " width=" << width << " height=" << height 
              << " old=" << this->width << "x" << this->height
              << " ready=" << ready
              << " buffersValid=" << buffersValid << std::endl;

    if (ready && this->width == width && this->height == height && buffersValid && allocatedSize >= requiredSize) {
        std::cout << "[VideoTexture] Skipping recreation (valid)" << std::endl;
        return;
    }

    std::cout << "[VideoTexture] Creating video texture resources: " << width << "x" << height << std::endl;
    destroy(resourceSystem, device);

    this->width = width;
    this->height = height;
    frameSize = static_cast<size_t>(width) * height * 4;

    // Create current frame texture
    imageHandle = resourceSystem.createImage(
        width,
        height,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Create previous frame texture for interpolation
    imageHandlePrev = resourceSystem.createImage(
        width,
        height,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = imageHandle.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create video image view");
    }

    // Create image view for previous frame texture
    viewInfo.image = imageHandlePrev.image;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageViewPrev) != VK_SUCCESS) {
        throw std::runtime_error("failed to create video image view for previous frame");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create video sampler");
    }

    initializeVideoImage(device, commandPool, imageHandle.image, graphicsQueue);
    initializeVideoImage(device, commandPool, imageHandlePrev.image, graphicsQueue);

    descriptorInfo.sampler = sampler;
    descriptorInfo.imageView = imageView;
    descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pendingUploads.fill(false);

    descriptorInfoPrev.sampler = sampler;
    descriptorInfoPrev.imageView = imageViewPrev;
    descriptorInfoPrev.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currentLayoutPrev = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pendingUploadsPrev.fill(false);

    // Allocate CPU buffer for previous frame
    previousFrameData.resize(frameSize);
    std::fill(previousFrameData.begin(), previousFrameData.end(), 0);

    // Create staging buffer for previous frame upload
    auto prevStagingBuffer = resourceSystem.createBuffer(
        static_cast<VkDeviceSize>(frameSize),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* prevMapped = resourceSystem.map(prevStagingBuffer);
    if (prevMapped == nullptr) {
        resourceSystem.destroy(prevStagingBuffer);
        throw std::runtime_error("failed to map previous frame staging buffer");
    }

    prevFrameStagingBuffer = std::move(prevStagingBuffer);
    prevFrameStagingMapped = prevMapped;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (frameSize == 0) {
            stagingSlots[i] = {};
            continue;
        }

        auto buffer = resourceSystem.createBuffer(
            static_cast<VkDeviceSize>(frameSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* mapped = resourceSystem.map(buffer);
        if (mapped == nullptr) {
            resourceSystem.destroy(buffer);
            throw std::runtime_error("failed to map video staging buffer");
        }

        stagingSlots[i].buffer = std::move(buffer);
        stagingSlots[i].mapped = mapped;
        stagingSlots[i].capacity = frameSize;
    }

    ready = true;
    std::cout << "[VideoTexture] Resources created successfully" << std::endl;
}

void VideoTexture::destroy(ResourceSystem& resourceSystem, VkDevice device) {
    std::cout << "[VideoTexture] Destroy called" << std::endl;
    
    destroyStagingBuffers(resourceSystem);

    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }
    if (imageViewPrev != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageViewPrev, nullptr);
        imageViewPrev = VK_NULL_HANDLE;
    }
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

    if (imageHandle.type != ResourceType::Unknown) {
        resourceSystem.destroy(imageHandle);
        imageHandle = {};
    }
    if (imageHandlePrev.type != ResourceType::Unknown) {
        resourceSystem.destroy(imageHandlePrev);
        imageHandlePrev = {};
    }
    if (prevFrameStagingBuffer.type != ResourceType::Unknown) {
        if (prevFrameStagingMapped != nullptr) {
            resourceSystem.unmap(prevFrameStagingBuffer);
            prevFrameStagingMapped = nullptr;
        }
        resourceSystem.destroy(prevFrameStagingBuffer);
        prevFrameStagingBuffer = {};
    }
    ready = false;
    descriptorInfo = {};
    descriptorInfoPrev = {};
    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    currentLayoutPrev = VK_IMAGE_LAYOUT_UNDEFINED;
    pendingUploads.fill(false);
    pendingUploadsPrev.fill(false);
    previousFrameData.clear();
    
    std::cout << "[VideoTexture] Destroy completed" << std::endl;
}

void VideoTexture::uploadFrame(uint32_t frameIndex, const uint8_t* data, size_t size) {
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT || !ready) {
        return;
    }

    auto& slot = stagingSlots[frameIndex];
    if (slot.mapped != nullptr && size <= slot.capacity) {
        std::memcpy(slot.mapped, data, size);
        pendingUploads[frameIndex] = true;
    }
}

void VideoTexture::recordPendingUpload(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkQueue graphicsQueue) {
    if (!ready || frameIndex >= MAX_FRAMES_IN_FLIGHT) {
        return;
    }

    const auto& slot = stagingSlots[frameIndex];

    // Helper function to transition a single image
    auto transition = [&](VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkAccessFlags srcAccessMask = 0;
        VkAccessFlags dstAccessMask = 0;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }

        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;

        vkCmdPipelineBarrier(
            commandBuffer,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    };

    // Upload to current frame texture
    if (pendingUploads[frameIndex]) {
        VkImageLayout startLayout = currentLayout;
        VkPipelineStageFlags srcStage = (startLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        transition(imageHandle.image,
                   startLayout,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   srcStage,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            slot.buffer.buffer,
            imageHandle.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);

        transition(
            imageHandle.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pendingUploads[frameIndex] = false;
    }

    // Upload to previous frame texture
    if (pendingUploadsPrev[frameIndex] && !previousFrameData.empty() && prevFrameStagingMapped != nullptr) {
        VkImageLayout startLayout = currentLayoutPrev;
        VkPipelineStageFlags srcStage = (startLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        transition(imageHandlePrev.image,
                   startLayout,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   srcStage,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);

        // Copy previous frame data to staging buffer
        std::memcpy(prevFrameStagingMapped, previousFrameData.data(), frameSize);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            prevFrameStagingBuffer.buffer,
            imageHandlePrev.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);

        transition(
            imageHandlePrev.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        currentLayoutPrev = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        pendingUploadsPrev[frameIndex] = false;
    }
}

void VideoTexture::uploadPreviousFrame(VkCommandBuffer commandBuffer, VkQueue graphicsQueue) {
    // This is a simplified version - in the full implementation this would
    // need proper command buffer recording and submission
    if (!previousFrameData.empty() && prevFrameStagingMapped != nullptr) {
        std::memcpy(prevFrameStagingMapped, previousFrameData.data(), frameSize);
        pendingUploadsPrev.fill(true);
    }
}

void VideoTexture::initializeVideoImage(VkDevice device, VkCommandPool commandPool, VkImage image, VkQueue graphicsQueue) {
    if (image == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer for video image init");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to begin command buffer for video image init");
    }

    auto transition = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkAccessFlags srcAccessMask = 0;
        VkAccessFlags dstAccessMask = 0;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }

        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;

        vkCmdPipelineBarrier(
            commandBuffer,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
    };

    transition(VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange clearRange{};
    clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearRange.baseMipLevel = 0;
    clearRange.levelCount = 1;
    clearRange.baseArrayLayer = 0;
    clearRange.layerCount = 1;

    vkCmdClearColorImage(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clearColor,
        1,
        &clearRange);

    transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to record command buffer for video image init");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        throw std::runtime_error("failed to submit command buffer for video image init");
    }

    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void VideoTexture::destroyStagingBuffers(ResourceSystem& resourceSystem) {
    for (size_t i = 0; i < stagingSlots.size(); ++i) {
        auto& slot = stagingSlots[i];

        if (slot.mapped != nullptr && slot.buffer.type != ResourceType::Unknown) {
            resourceSystem.unmap(slot.buffer);
            slot.mapped = nullptr;
        }
        if (slot.buffer.type != ResourceType::Unknown) {
            resourceSystem.destroy(slot.buffer);
            slot.buffer = {};
        }
        slot.capacity = 0;
    }
}
