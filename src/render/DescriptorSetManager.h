#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>

class DescriptorSetManager {
public:
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

    DescriptorSetManager();
    ~DescriptorSetManager();

    // Initialization
    void createLayout(VkDevice device);
    void createPool(VkDevice device);
    void createSets(VkDevice device);
    void destroy(VkDevice device);

    // Update descriptor set for a specific frame
    void updateSet(VkDevice device, uint32_t frameIndex,
                   VkBuffer uniformBuffer, VkDescriptorImageInfo* imageInfo,
                   VkDescriptorImageInfo* imageInfoPrev);

    // Getters
    VkDescriptorSetLayout getLayout() const { return descriptorSetLayout; }
    const std::vector<VkDescriptorSet>& getSets() const { return descriptorSets; }
    VkDescriptorSet getSet(uint32_t frameIndex) const;

private:
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
};
