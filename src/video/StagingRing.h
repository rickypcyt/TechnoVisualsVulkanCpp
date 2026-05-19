#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cassert>
#include <cstring>
#include "gfx/ResourceSystem.h"

// Staging Ring Buffer - fixed MAX capacity, frames-in-flight safe
// CRITICAL: capacity is FIXED based on MAX VIDEO SIZE, not current frame
struct StagingSlot {
    ResourceHandle buffer;
    void* mapped = nullptr;
    size_t capacity = 0;
};

class StagingRing {
public:
    static constexpr int FRAMES_IN_FLIGHT = 2;

    StagingRing() = default;

    void create(ResourceSystem& resourceSystem, VkDevice device, size_t frameSize) {
        for (size_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            auto bufferHandle = resourceSystem.createBuffer(
                static_cast<VkDeviceSize>(frameSize),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            void* mapped = resourceSystem.map(bufferHandle);
            if (mapped == nullptr) {
                resourceSystem.destroy(bufferHandle);
                throw std::runtime_error("Failed to map staging buffer");
            }

            slots[i].buffer = std::move(bufferHandle);
            slots[i].mapped = mapped;
            slots[i].capacity = frameSize;
        }
    }

    void destroy(ResourceSystem& resourceSystem) {
        for (size_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            auto& slot = slots[i];
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

    // Get slot for current frame (ring buffer)
    StagingSlot& getSlot(uint32_t frameIndex) {
        return slots[frameIndex % FRAMES_IN_FLIGHT];
    }

    const StagingSlot& getSlot(uint32_t frameIndex) const {
        return slots[frameIndex % FRAMES_IN_FLIGHT];
    }

    // CRITICAL SAFETY GUARD: assert before memcpy
    void upload(const uint8_t* src, size_t size, uint32_t frameIndex) {
        StagingSlot& slot = getSlot(frameIndex);
        assert(size <= slot.capacity && "Frame size exceeds staging buffer capacity!");
        std::memcpy(slot.mapped, src, size);
    }

    size_t getCapacity() const { return slots[0].capacity; }

private:
    std::array<StagingSlot, FRAMES_IN_FLIGHT> slots{};
};
