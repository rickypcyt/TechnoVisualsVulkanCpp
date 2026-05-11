#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <cstdint>
#include "../gfx/ResourceSystem.h"

class VideoTexture {
public:
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

    struct StagingSlot {
        ResourceHandle buffer;
        void* mapped = nullptr;
        size_t capacity = 0;

        StagingSlot(const StagingSlot&) = delete;
        StagingSlot& operator=(const StagingSlot&) = delete;

        StagingSlot(StagingSlot&& other) noexcept
            : buffer(std::move(other.buffer))
            , mapped(other.mapped)
            , capacity(other.capacity)
        {
            other.mapped = nullptr;
            other.capacity = 0;
        }

        StagingSlot& operator=(StagingSlot&& other) noexcept {
            if (this != &other) {
                buffer = std::move(other.buffer);
                mapped = other.mapped;
                capacity = other.capacity;
                other.mapped = nullptr;
                other.capacity = 0;
            }
            return *this;
        }

        StagingSlot() = default;
    };

    VideoTexture();
    ~VideoTexture();

    // Initialization
    void createResources(ResourceSystem& resourceSystem, VkDevice device, VkCommandPool commandPool,
                         VkQueue graphicsQueue, uint32_t width, uint32_t height);
    void destroy(ResourceSystem& resourceSystem, VkDevice device);

    // Upload management
    void uploadFrame(uint32_t frameIndex, const uint8_t* data, size_t size);
    void recordPendingUpload(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkQueue graphicsQueue);

    // Getters
    bool isReady() const { return ready; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
    size_t getFrameSize() const { return frameSize; }
    
    VkImageView getImageView() const { return imageView; }
    VkImageView getPrevImageView() const { return imageViewPrev; }
    VkSampler getSampler() const { return sampler; }
    VkImage getImage() const { return imageHandle.image; }
    VkImage getPrevImage() const { return imageHandlePrev.image; }
    
    const VkDescriptorImageInfo& getDescriptorInfo() const { return descriptorInfo; }
    const VkDescriptorImageInfo& getPrevDescriptorInfo() const { return descriptorInfoPrev; }
    
    const std::array<bool, MAX_FRAMES_IN_FLIGHT>& getPendingUploads() const { return pendingUploads; }
    std::array<bool, MAX_FRAMES_IN_FLIGHT>& getPendingUploads() { return pendingUploads; }
    const std::array<bool, MAX_FRAMES_IN_FLIGHT>& getPendingUploadsPrev() const { return pendingUploadsPrev; }
    std::array<bool, MAX_FRAMES_IN_FLIGHT>& getPendingUploadsPrev() { return pendingUploadsPrev; }
    
    void resetPendingUploads() {
        pendingUploads.fill(false);
        pendingUploadsPrev.fill(false);
    }

    // Previous frame management
    const std::vector<uint8_t>& getPreviousFrameData() const { return previousFrameData; }
    std::vector<uint8_t>& getPreviousFrameData() { return previousFrameData; }
    void uploadPreviousFrame(VkCommandBuffer commandBuffer, VkQueue graphicsQueue);

private:
    // GPU resources
    ResourceHandle imageHandle;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptorInfo{};
    
    // Previous frame resources
    ResourceHandle imageHandlePrev;
    VkImageView imageViewPrev = VK_NULL_HANDLE;
    VkDescriptorImageInfo descriptorInfoPrev{};
    
    // Dimensions
    uint32_t width = 1;
    uint32_t height = 1;
    size_t frameSize = 4;
    bool ready = false;
    
    // Layout tracking
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout currentLayoutPrev = VK_IMAGE_LAYOUT_UNDEFINED;
    
    // Staging buffers
    std::array<StagingSlot, MAX_FRAMES_IN_FLIGHT> stagingSlots{};
    std::array<double, MAX_FRAMES_IN_FLIGHT> stagingTimestamps{};
    std::array<bool, MAX_FRAMES_IN_FLIGHT> pendingUploads{};
    std::array<bool, MAX_FRAMES_IN_FLIGHT> pendingUploadsPrev{};
    
    // Previous frame CPU buffer and staging
    std::vector<uint8_t> previousFrameData;
    ResourceHandle prevFrameStagingBuffer;
    void* prevFrameStagingMapped = nullptr;

    // Helper functions
    void initializeVideoImage(VkDevice device, VkCommandPool commandPool, VkImage image, VkQueue graphicsQueue);
    void destroyStagingBuffers(ResourceSystem& resourceSystem);
};
