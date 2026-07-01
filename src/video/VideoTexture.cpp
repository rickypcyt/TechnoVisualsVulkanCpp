#include "VideoTexture.h"
#include "FrameLayout.h"
#include <iostream>
#include <cstring>
#include <stdexcept>

VideoTexture::VideoTexture() = default;

VideoTexture::~VideoTexture() {
    // Resources should be destroyed via destroy() before destructor
}

void VideoTexture::createResources(ResourceSystem& resourceSystem, VkDevice device, VkCommandPool commandPool,
                                    VkQueue graphicsQueue, uint32_t width, uint32_t height) {
    // Check if GPU image recreation is necessary
    FrameLayout newLayout(width, height);  // Assume compact layout for GPU
    size_t requiredSize = newLayout.compactSize();
    size_t allocatedSize = frameSize;

    std::cout << "[VideoTexture] Dimension check: allocated=" << std::dec << allocatedSize 
              << " required=" << requiredSize 
              << " width=" << width << " height=" << height 
              << " old=" << this->width << "x" << this->height
              << " ready=" << ready << std::endl;

    // Skip recreation if dimensions are the same (GPU image only)
    if (ready && this->width == width && this->height == height) {
        std::cout << "[VideoTexture] Skipping recreation (same dimensions)" << std::endl;
        return;
    }

    std::cout << "[VideoTexture] Creating video texture resources: " << std::dec << width << "x" << height << std::endl;

    this->width = width;
    this->height = height;
    this->stride = width * 4;  // GPU expects compact layout
    // Add 64 bytes of padding for SIMD alignment safety (sws_scale may
    // write slightly past the end of each line for SSE/AVX alignment)
    frameSize = requiredSize + 64;

    // Create staging ring buffer (dynamic capacity based on current video size)
    if (!stagingRing.getSlot(0).mapped) {
        stagingRing.create(resourceSystem, device, frameSize);
        std::cout << "[VideoTexture] Staging ring created with capacity: " << std::dec << stagingRing.getCapacity() << " bytes" << std::endl;
    }

    // Create current frame texture (needs TRANSFER_SRC for copying to prev frame)
    imageHandle = resourceSystem.createImage(
        width,
        height,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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

    ready = true;
    std::cout << "[VideoTexture] Resources created successfully" << std::endl;
}

void VideoTexture::destroy(ResourceSystem& resourceSystem, VkDevice device) {
    std::cout << "[VideoTexture] Destroy called" << std::endl;
    
    // NOTE: Staging ring is NOT destroyed here - it has fixed MAX capacity
    // and persists across resolution changes

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
    
    ready = false;
    descriptorInfo = {};
    descriptorInfoPrev = {};
    currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    currentLayoutPrev = VK_IMAGE_LAYOUT_UNDEFINED;
    pendingUploads.fill(false);
    frameSize = 0;
    width = 0;
    height = 0;
    stride = 0;

    std::cout << "[VideoTexture] Destroy completed" << std::endl;
}

void VideoTexture::uploadFrame(uint32_t frameIndex, const uint8_t* data, size_t size) {
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT || !ready) {
        return;
    }

    // Validate incoming data size against expected compact layout
    FrameLayout expectedLayout(width, height, stride);
    if (size != expectedLayout.compactSize()) {
        std::cerr << "[VideoTexture] Size mismatch: received=" << size 
                  << " expected=" << expectedLayout.compactSize()
                  << " for " << width << "x" << height 
                  << " stride=" << stride << std::endl;
        // Don't upload invalid data
        return;
    }

    // Use StagingRing with built-in safety assert
    stagingRing.upload(data, size, frameIndex);
    pendingUploads[frameIndex] = true;
}

void VideoTexture::recordPendingUpload(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkQueue graphicsQueue) {
    if (!ready || frameIndex >= MAX_FRAMES_IN_FLIGHT) {
        return;
    }

    const auto& slot = stagingRing.getSlot(frameIndex);

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
        } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
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
        // GPU→GPU copy: preserve old current frame as previous frame BEFORE overwriting
        // (skipped when freezePrev is active to keep the snapshot for crossfade transitions)
        if (!freezePrev && imageHandle.image != VK_NULL_HANDLE && imageHandlePrev.image != VK_NULL_HANDLE) {
            transition(imageHandle.image,
                       currentLayout,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
            currentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            transition(imageHandlePrev.image,
                       currentLayoutPrev,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.baseArrayLayer = 0;
            copyRegion.srcSubresource.mipLevel = 0;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource = copyRegion.srcSubresource;
            copyRegion.extent = {width, height, 1};

            vkCmdCopyImage(commandBuffer,
                          imageHandle.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          imageHandlePrev.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &copyRegion);

            transition(imageHandlePrev.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            currentLayoutPrev = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkImageLayout startLayout = currentLayout;
        VkPipelineStageFlags srcStage = (startLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                            : VK_PIPELINE_STAGE_TRANSFER_BIT;

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
    // Staging ring is destroyed separately in cleanup, not here
    // This function is kept for compatibility but does nothing
}

void VideoTexture::cleanup(ResourceSystem& resourceSystem) {
    std::cout << "[VideoTexture] Cleanup called - destroying staging ring" << std::endl;
    stagingRing.destroy(resourceSystem);
}
