#include "UniformBufferManager.h"
#include <stdexcept>
#include <cstring>

UniformBufferManager::UniformBufferManager() = default;

UniformBufferManager::~UniformBufferManager() {
    // Resources should be destroyed via destroy() before destructor
}

void UniformBufferManager::createBuffers(ResourceSystem& resourceSystem, VkDevice device) {
    VkDeviceSize bufferSize = sizeof(GlobalParamsUBO);
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBufferHandles.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        ResourceHandle handle = resourceSystem.createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        // Save legacy fields before moving
        uniformBuffers[i] = handle.buffer;
        uniformBuffersMemory[i] = handle.memory;

        // Map before moving
        uniformBuffersMapped[i] = resourceSystem.map(handle);
        if (uniformBuffersMapped[i] == nullptr) {
            throw std::runtime_error("failed to map uniform buffer");
        }

        // Now move the handle
        uniformBufferHandles[i] = std::move(handle);
    }
}

void UniformBufferManager::destroy(ResourceSystem& resourceSystem, VkDevice device) {
    for (size_t i = 0; i < uniformBufferHandles.size(); ++i) {
        if (uniformBuffersMapped[i] != nullptr && uniformBufferHandles[i].type != ResourceType::Unknown) {
            resourceSystem.unmap(uniformBufferHandles[i]);
            uniformBuffersMapped[i] = nullptr;
        }
    }

    for (auto& handle : uniformBufferHandles) {
        if (handle.type != ResourceType::Unknown) {
            resourceSystem.destroy(handle);
        }
    }
    
    uniformBufferHandles.clear();
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();
}

void UniformBufferManager::update(uint32_t frameIndex, const GlobalParamsUBO& ubo) {
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT || uniformBuffersMapped[frameIndex] == nullptr) {
        return;
    }
    
    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
}

VkDescriptorBufferInfo UniformBufferManager::getDescriptorInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo bufferInfo{};
    if (frameIndex < uniformBuffers.size()) {
        bufferInfo.buffer = uniformBuffers[frameIndex];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalParamsUBO);
    }
    return bufferInfo;
}
