#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <cstdint>
#include "../gfx/ResourceSystem.h"
#include "GlobalUBO.h"

class UniformBufferManager {
public:
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 3;

    UniformBufferManager();
    ~UniformBufferManager();

    // Initialization
    void createBuffers(ResourceSystem& resourceSystem, VkDevice device);
    void destroy(ResourceSystem& resourceSystem, VkDevice device);

    // Update uniform buffer for a specific frame
    void update(uint32_t frameIndex, const GlobalParamsUBO& ubo, VkDevice device);

    // Getters
    const std::vector<VkBuffer>& getBuffers() const { return uniformBuffers; }
    const std::vector<ResourceHandle>& getHandles() const { return uniformBufferHandles; }
    VkDescriptorBufferInfo getDescriptorInfo(uint32_t frameIndex) const;
    static size_t getBufferSize() { return sizeof(GlobalParamsUBO); }

private:
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;
    std::vector<ResourceHandle> uniformBufferHandles;
};
