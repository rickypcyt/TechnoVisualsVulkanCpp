#pragma once
#include <cstdint>
#include <cstddef>
#include <vulkan/vulkan.h>
#include "../gfx/ResourceSystem.h"

struct VideoStagingSlot {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t capacity = 0;
    double timestamp = 0.0;

    // Delete copy constructor and copy assignment to prevent double-ownership
    VideoStagingSlot(const VideoStagingSlot&) = delete;
    VideoStagingSlot& operator=(const VideoStagingSlot&) = delete;

    // Default move constructor and move assignment
    VideoStagingSlot(VideoStagingSlot&& other) noexcept
        : buffer(other.buffer)
        , memory(other.memory)
        , mapped(other.mapped)
        , capacity(other.capacity)
        , timestamp(other.timestamp)
    {
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.mapped = nullptr;
        other.capacity = 0;
        other.timestamp = 0.0;
    }

    VideoStagingSlot& operator=(VideoStagingSlot&& other) noexcept {
        if (this != &other) {
            buffer = other.buffer;
            memory = other.memory;
            mapped = other.mapped;
            capacity = other.capacity;
            timestamp = other.timestamp;

            other.buffer = VK_NULL_HANDLE;
            other.memory = VK_NULL_HANDLE;
            other.mapped = nullptr;
            other.capacity = 0;
            other.timestamp = 0.0;
        }
        return *this;
    }

    VideoStagingSlot() = default;
};

struct VideoStagingWriteResult {
    bool success;
};

class VideoStaging {
public:
    bool init(uint32_t slotCount, size_t slotSize);
    void destroy();

    VideoStagingWriteResult writeVideoStagingSlot(
        uint32_t slot,
        const void* data,
        size_t size,
        uint64_t epoch
    );

    const VideoStagingSlot& getSlot(uint32_t slot) const;
    void setSlot(uint32_t slot, VkBuffer buffer, VkDeviceMemory memory, void* mapped, size_t capacity);
    void clearSlot(uint32_t slot);

    uint32_t getSlotCount() const { return m_slotCount; }

private:
    uint32_t m_slotCount = 0;
    size_t m_slotSize = 0;

    uint64_t m_epoch = 0;

    VideoStagingSlot* m_slots = nullptr;
};
