#include "DescriptorSetManager.h"
#include "GlobalUBO.h"
#include <stdexcept>
#include <iostream>

DescriptorSetManager::DescriptorSetManager() = default;

DescriptorSetManager::~DescriptorSetManager() {
    // Resources should be destroyed via destroy() before destructor
}

void DescriptorSetManager::createLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerBindingPrev{};
    samplerBindingPrev.binding = 2;
    samplerBindingPrev.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBindingPrev.descriptorCount = 1;
    samplerBindingPrev.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBindingPrev.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {uboLayoutBinding, samplerBinding, samplerBindingPrev};
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }
}

void DescriptorSetManager::createPool(VkDevice device) {
    // Safety multiplier for pool sizing to handle transient updates and future expansion
    constexpr uint32_t POOL_SAFETY_MULTIPLIER = 2;
    
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * POOL_SAFETY_MULTIPLIER;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // 2 samplers per frame (current + previous) * safety multiplier
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2 * POOL_SAFETY_MULTIPLIER;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT * POOL_SAFETY_MULTIPLIER;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool");
    }
}

void DescriptorSetManager::createSets(VkDevice device) {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(layouts.size());
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets");
    }
}

void DescriptorSetManager::destroy(VkDevice device) {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    descriptorSets.clear();
}

void DescriptorSetManager::updateSet(VkDevice device, uint32_t frameIndex,
                                       VkBuffer uniformBuffer, VkDescriptorImageInfo* imageInfo,
                                       VkDescriptorImageInfo* imageInfoPrev) {
    if (frameIndex >= descriptorSets.size()) {
        std::cerr << "Error: Invalid frameIndex " << frameIndex << " in DescriptorSetManager::updateSet (max: " << descriptorSets.size() - 1 << ")" << std::endl;
        return;
    }

    // Get minUniformBufferOffsetAlignment for proper UBO alignment
    VkPhysicalDeviceProperties properties;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    // Note: In a real system, you'd cache this device property during initialization
    // For now, we'll use the struct size directly (works on most GPUs)
    VkDeviceSize alignment = sizeof(GlobalParamsUBO);
    
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(GlobalParamsUBO);

    // Build dynamic vector of only valid descriptor writes
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    descriptorWrites.reserve(3);

    // UBO binding (always valid)
    VkWriteDescriptorSet uboWrite{};
    uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    uboWrite.dstSet = descriptorSets[frameIndex];
    uboWrite.dstBinding = 0;
    uboWrite.dstArrayElement = 0;
    uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboWrite.descriptorCount = 1;
    uboWrite.pBufferInfo = &bufferInfo;
    descriptorWrites.push_back(uboWrite);

    // Current sampler binding (only if imageInfo provided)
    if (imageInfo) {
        VkWriteDescriptorSet samplerWrite{};
        samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        samplerWrite.dstSet = descriptorSets[frameIndex];
        samplerWrite.dstBinding = 1;
        samplerWrite.dstArrayElement = 0;
        samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerWrite.descriptorCount = 1;
        samplerWrite.pImageInfo = imageInfo;
        descriptorWrites.push_back(samplerWrite);
    }

    // Previous sampler binding (only if imageInfoPrev provided)
    if (imageInfoPrev) {
        VkWriteDescriptorSet samplerPrevWrite{};
        samplerPrevWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        samplerPrevWrite.dstSet = descriptorSets[frameIndex];
        samplerPrevWrite.dstBinding = 2;
        samplerPrevWrite.dstArrayElement = 0;
        samplerPrevWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerPrevWrite.descriptorCount = 1;
        samplerPrevWrite.pImageInfo = imageInfoPrev;
        descriptorWrites.push_back(samplerPrevWrite);
    }

    // Only submit the valid writes
    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

VkDescriptorSet DescriptorSetManager::getSet(uint32_t frameIndex) const {
    if (frameIndex < descriptorSets.size()) {
        return descriptorSets[frameIndex];
    }
    return VK_NULL_HANDLE;
}
