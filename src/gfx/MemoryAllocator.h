#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>

struct MemoryAllocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    uint32_t blockIndex = UINT32_MAX;
};

struct FreeRange {
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
};

struct MemoryBlock {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceSize used = 0;
    std::vector<FreeRange> freeList;
    uint32_t memoryTypeIndex = 0;
    void* mappedData = nullptr;
};

// MemoryAllocator - manages Vulkan device memory with block-based allocation
class MemoryAllocator {
public:
    void init(VkDevice deviceHandle, VkPhysicalDevice physicalDeviceHandle);
    
    MemoryAllocation allocate(VkDeviceSize size, VkDeviceSize alignment, 
                               VkMemoryPropertyFlags properties, VkMemoryRequirements memReqs);
    void deallocate(const MemoryAllocation& alloc);
    void* map(const MemoryAllocation& alloc);
    void unmap(const MemoryAllocation& alloc);
    void cleanup();

private:
    MemoryAllocation allocateFromBlock(MemoryBlock& block, VkDeviceSize size, VkDeviceSize alignment);
    void createBlock(VkDeviceSize size, uint32_t memoryTypeIndex, VkMemoryPropertyFlags properties);
    void mergeFreeRanges(std::vector<FreeRange>& freeList);
    static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::vector<MemoryBlock> blocks;
};
